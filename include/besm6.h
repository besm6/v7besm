/*
 * <besm6.h> — compiler intrinsics for the BESM-6 machine instructions.
 *
 * The odd one out in this directory: every other header here describes the
 * language or v7, and changes when we change.  This one describes the COMPILER,
 * and is a copy of the external c-compiler's own — a back end that grows a tenth
 * intrinsic makes this file stale and nothing will say so.  Re-copy it from
 * libc/besm6/include/ over there when the intrinsics change.
 *
 * The BESM-6 has no I/O address space, no memory-mapped device registers and no
 * channel programs: every peripheral is reached by one of two supervisor
 * instructions, and the machine's bit-manipulation instructions have no C
 * equivalent at all.  The intrinsics below expose them directly, so that a
 * kernel or a driver can be written in C instead of assembly.  They are
 * specified in doc/Intrinsics.md.
 *
 * Status: all nine are lowered — Tier 1 (ext, mod, the halt), all five Tier-2
 * bit manipulations, and Tier 3 (the extracode).  Each becomes a single inline
 * machine instruction, never a call.
 *
 * This header declares those nine and nothing else.  Readable wrappers for the
 * registers a given program cares about — a popcount, an spl(), the ГРП bit
 * names — are the caller's own business; they are one #define each, and what
 * they should be named depends on the program.
 *
 * Every intrinsic that carries a machine word takes and returns `unsigned`,
 * never `int`.  A BESM-6 word is 48 bits, but a signed int on this target holds
 * only 41 of them; a device control word or a ГРП value (whose bit 48 is live)
 * would not survive the trip.  See doc/Besm6_Data_Representation.md.
 *
 * Absolute machine addresses need no intrinsic: the BESM-6 is word-addressed,
 * so a C pointer is a word index and a `volatile unsigned *` reaches low memory
 * directly.  There is also no atomic instruction in the ISA — mutual exclusion
 * on this machine is interrupt masking.
 *
 * Addresses and opcodes here are OCTAL, as in every BESM-6 document, and bits
 * are numbered right-to-left from 1 (bit 1 = LSB, bit 48 = MSB).
 */
#ifndef _BESM6_H
#define _BESM6_H

/* ---- Tier 1: privileged — reaching the hardware ---- */
/*
 * These two mirror the hardware exactly: the accumulator is both the input and
 * the output, and the direction of the transfer lives in the address, not in
 * the instruction (one bit of the address means "read" — 04000 for ext, 0200
 * for mod).  ADDR is the verbatim address from the peripherals map, read bit
 * included; on a read address the incoming accumulator is ignored, so pass 0.
 *
 * Both are side-effecting and never eliminable, even when the result is unused:
 * they are the machine's only I/O.
 */

/*
 * 033 ext (увв) — the peripherals: drums, disks, tape, printer, punches, card
 * equipment, terminals.  A := acc; ext addr; result := A.
 */
unsigned __besm6_ext(unsigned addr, unsigned acc);

/*
 * 002 mod (рег) — the CPU-internal registers: the cache БРЗ, the page registers
 * РП, the protection register РЗ, the interrupt register ГРП and its mask МГРП,
 * the mode bits РУУ.  A := acc; mod addr; result := A.
 *
 * Two surprises worth restating, both about ГРП, the interrupt register.  002
 * 037 clears it by writing a mask in which a ZERO bit clears (GRP &= ACC |
 * GRP_WIRED_BITS), so to dismiss one interrupt you write the COMPLEMENT of its
 * bit.  And the wired bits — the "device free" and "exchange done" bits of the
 * mass-storage channels — are live wires, not flip-flops; they cannot be
 * cleared that way at all, and go down only when the device is itself given a
 * new command.
 */
unsigned __besm6_mod(unsigned addr, unsigned acc);

/*
 * 033 format 2, stop (стоп) — halt the processor.  Legal in user mode, and NOT
 * _Noreturn: the halt is resumable.  A human operator presses the console's
 * continue button and execution carries on at the next instruction, so this is
 * an ordinary void call and the code after it is reachable.
 *
 * CODE is a halt reason, encoded in the instruction's own 15-bit address field
 * — it must therefore be a compile-time constant in 0..077777.  It identifies
 * the halt site on the operator's console and in a trace or dump; note that our
 * two simulators (dubna, b6sim) ignore it and simply end the simulation, so
 * nothing after a stop runs under them.
 */
void __besm6_stop(unsigned code);

/* ---- Tier 2: bit manipulation ---- */
/*
 * Three of these add their result to X with END-AROUND CARRY: a 48-bit unsigned
 * add in which a carry out of bit 48 comes back into bit 1.  That is the
 * machine's own integer add, and it is not C's `+` on unsigned, which wraps
 * mod 2^48.  Pass x = 0 to get the plain value.
 */

/*
 * 020 apx (сбр) — gather.  Collect the bits of A selected by MASK, in source
 * order, ALIGNED TO THE MSB: popcount(mask) bits occupy result bits 48 down.
 * This is the opposite alignment from x86's PEXT; to right-align, follow with
 * >> (48 - popcount(mask)).
 */
unsigned __besm6_apx(unsigned a, unsigned mask);

/*
 * 021 aux (рзб) — scatter, the exact inverse of apx.  Each 1-bit of MASK,
 * scanned from bit 48 down, consumes one bit of A taken from A's MSB downward
 * and deposits it at that position; 0-bits of MASK yield 0.
 */
unsigned __besm6_aux(unsigned a, unsigned mask);

/*
 * 022 acx (чед) — population count: popcount(a) added to X, end-around carry.
 */
unsigned __besm6_acx(unsigned a, unsigned x);

/*
 * 023 anx (нед) — highest set bit: the POSITION of A's highest set bit numbered
 * FROM THE MSB (bit 48 -> 1, bit 1 -> 48), added to X, end-around carry.  If A
 * is zero the result is just X — there is no distinguished "not found" value,
 * so the caller must test for zero first.
 */
unsigned __besm6_anx(unsigned a, unsigned x);

/*
 * 013 arx (слц) — cyclic add: a added to X, end-around carry.  Useful for
 * checksums.
 */
unsigned __besm6_arx(unsigned a, unsigned x);

/* ---- Tier 3: extracodes ---- */
/*
 * 050-077 — invoke extracode OP: M[016] := ea; A := acc; result := A.
 * Extracodes execute in user mode; they are how a program asks the operating
 * system for a privileged operation, and the Unix v7 syscall trap $77 N rides
 * on exactly this mechanism.
 *
 * OP is the opcode — it becomes an immediate field of the instruction word, so
 * it must be a compile-time constant in 050..077; the compiler evaluates it at
 * typecheck and diagnoses anything else.  EA may be constant or computed.
 *
 * Note the ABI consequence: an extracode sets M[016] — that is, r14 — from the
 * effective address, so code around this intrinsic must treat r14 as clobbered.
 * It is caller-saved, so this is legal.
 */
unsigned __besm6_extracode(int op, unsigned ea, unsigned acc);

#endif /* _BESM6_H */
