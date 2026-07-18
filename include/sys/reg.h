/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

#include "sys/besm6dev.h" /* SPSW_* mode-word bits */

/*
 * Location of the user's saved registers on the kernel-stack trap frame.
 * Usage is u.u_ar0[XX].  u.u_ar0 points at frame word 0 (the accumulator), so
 * every index below is a plain, non-negative word index -- unlike the x86,
 * whose u_ar0 was anchored mid-frame at EAX and reached the lower registers
 * with negative offsets.  sizeof(int) == 6 == one 48-bit word, so an index is
 * a word index and the struct-field order below matches the index order.
 *
 * The layout follows Dubna's canonical per-process register-save block
 * (doc/Context_Switch.md sec 2), with ONE deliberate departure -- the two
 * return slots are collapsed into one:
 *
 *   0 ACC | 1 R | 2 Y | 3 RET | 4 СПСВ | 5 М16 | 6..20 М15..М1
 *
 * Dubna keeps IRET (interrupt/fault, resumed via `3 ij') and ERET
 * (extracode, resumed via `2 ij') as separate slots, but a given frame is built
 * by exactly one gate, so only one return address is ever live in it.  We store
 * whichever applies in the single RET slot: a fault/interrupt frame holds IRET
 * (and its gate resumes `3 ij'), an extracode frame holds ERET (`2 ij').  The
 * gate that built the frame knows which `ij' to use; readers just use RET.
 *
 * The register file is stored DESCENDING -- М16 (the C register / address
 * modifier) at offset 5, down to М1 at offset 20, so r15 is at 6 and r1 at 20.
 * ГРП is NOT framed: the fault cause is read live via __besm6_mod(MOD_GRP,0) in
 * trap() (task 15c), as Dubna does.  errno needs no slot -- it is r14 (М14), an
 * alias below.
 *
 * Both asynchronous gates fill this frame and share one epilogue: `intrgate'
 * (0501) and `trapgate' (0500) in besm6.S, which leave through `intret'.  The
 * 0577 syscall gate (task 15d) will join them, normalising ERET into IRET on
 * the way in so it can reuse the same exit.
 */

#define ACC  0 /* accumulator: primary syscall result (was EAX) */
#define RREG 1 /* R   -- ALU mode word (omega + the NTR suppress bits) */
#define RMR  2 /* Y (РМР) -- younger-bits register */
#define RET  3 /* return address: IRET (И33) for a fault, ERET (И32) for an extracode */
#define SPSW 4 /* СПСВ -- saved mode word (М027) */
#define CREG 5 /* М16 = C register M[16] (M[020]), the address modifier */

/* general registers -- stored DESCENDING, М15..М1 at offsets 6..20 */
#define R15 6  /* stack pointer */
#define R14 7  /* argument-count / errno register (BESM-6 syscall ABI) */
#define R13 8
#define R12 9
#define R11 10
#define R10 11
#define R9  12
#define R8  13
#define R7  14
#define R6  15
#define R5  16
#define R4  17
#define R3  18
#define R2  19
#define R1  20

#define NREGFRAME 21 /* words in the trap frame */

/*
 * Semantic aliases used by the C readers.  These are the BESM-6 syscall ABI
 * (doc/Besm6_Calling_Conventions.md, cmd/sim/syscall.cpp), NOT x86 renames:
 * a syscall result is ACC, an error is errno-in-r14 (0 on success) with no
 * carry flag.  R_VAL2's slot is provisional -- task 15d fixes the convention.
 */
#define R_ERRNO R14 /* syscall errno; 0 == success.  There is no carry bit. */
#define R_VAL2  R13 /* TODO 15d: second syscall result (pipe/getpid/fork) */

/*
 * The saved mode word СПСВ carries РежЭ|РежПр (RUU_EXTRACODE|RUU_INTERRUPT,
 * octal 014); both bits clear iff the interrupted context was user mode.  This
 * replaces the x86 CS-ring test.  See doc/Context_Switch.md and doc/Memory_Mapping.md.
 */
#define SPSW_MODE      (SPSW_EXTRACODE | SPSW_INTERRUPT) /* РежЭ | РежПр */
#define USERMODE(spsw) (((spsw) & SPSW_MODE) == 0)

/*
 * BASEPRI(): were we interrupted at base priority?  The x86 compared a saved
 * priority level in the frame; the BESM-6 has no per-frame priority -- the
 * current spl is the МГРП mask managed globally in intr.c.  The real test
 * belongs to clock()'s retarget, task 15e; until then this reads "at base
 * priority" so the callout path is never spuriously skipped.  clock() does not
 * fire until 15e wires GRP_TIMER into extintr(), so nothing depends on it yet.
 */
#define BASEPRI(x) (0) /* TODO 15e: test the global spl / МГРП mask */

struct trap {
    int acc;  /*  0  ACC */
    int rreg; /*  1  R */
    int rmr;  /*  2  Y */
    int ret;  /*  3  RET (IRET for a fault, ERET for an extracode) */
    int spsw; /*  4  saved program status word */
    int creg; /*  5  MOD = C register M[16] */
    int r15;  /*  6  М15  (SP) */
    int r14;  /*  7  М14  (errno / arg count) */
    int r13;  /*  8  М13 */
    int r12;  /*  9  М12 */
    int r11;  /* 10  М11 */
    int r10;  /* 11  М10 */
    int r9;   /* 12  М9 */
    int r8;   /* 13  М8 */
    int r7;   /* 14  М7 */
    int r6;   /* 15  М6 */
    int r5;   /* 16  М5 */
    int r4;   /* 17  М4 */
    int r3;   /* 18  М3 */
    int r2;   /* 19  М2 */
    int r1;   /* 20  М1 */
};
