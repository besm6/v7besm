/*
 * BESM-6 machine registers reached by the `ext` (033 «увв») and `mod` (002 «рег»)
 * instructions -- see doc/Besm6_Peripherals.md for the full address map and
 * doc/Intrinsics.md for the __besm6_ext()/__besm6_mod() intrinsics that issue them.
 *
 * Addresses and bit values are OCTAL; bits are numbered right-to-left from 1.
 *
 * This header is #define-only so that kernel/besm6.S can include it too.
 */
#ifndef _SYS_BESM6DEV_H
#define _SYS_BESM6DEV_H

/*
 * Addresses of the 002 «рег» instruction (__besm6_mod).
 */
#define MOD_MGRP   036  /* write МГРП, the mask of ГРП (48 bits) */
#define MOD_GRPCLR 037  /* clear ГРП: GRP &= ACC -- a ZERO bit clears */
#define MOD_GRP    0237 /* read ГРП */

/*
 * Addresses of the 033 «увв» instruction (__besm6_ext).  Bit 04000 selects a read.
 */
#define EXT_PRPCLR   030   /* clear ПРП: PRP &= ACC -- a ZERO bit clears */
#define EXT_MPRP     034   /* write МПРП, the mask of ПРП (24 bits) */
#define EXT_PRPHI    04030 /* read ПРП bits 13-24 */
#define EXT_PRPLO    04034 /* read ПРП bits 1-12; bits 1-5 always read as 1 */
#define EXT_READY2   04102 /* read READY2, the peripheral ready flags */
#define EXT_CONS1    0174  /* Consul 1: print the character in bits 1-8 */
#define EXT_CONS2    0175  /* Consul 2: print the character in bits 1-8 */
#define EXT_CONS1_RD 04174 /* Consul 1: read the typed character */
#define EXT_CONS2_RD 04175 /* Consul 2: read the typed character */

/*
 * Bits of ГРП, the main interrupt register.  An external interrupt (vector 0501)
 * fires while GRP & MGRP is non-zero and the PSW does not block interrupts.
 * The values need the U suffix: bit 42 and up do not fit in a signed int, which
 * holds only 41 of the machine's 48 bits.
 */
#define GRP_TIMER 00010000000000000U /* 40: interval timer tick */
#define GRP_SLAVE 00001000000000000U /* 37: see below */

/*
 * The fault bits of ГРП: how an INTERNAL interrupt (vector 0500) says what went wrong.
 * They are not an external-interrupt source -- the fault vectors on its own -- so the 0500
 * gate's trap() reads ГРП live to find the cause, then dismisses the bit with MOD_GRPCLR so
 * that an unmasked bit cannot fire afterwards as a spurious external interrupt.
 *
 * The faulting VIRTUAL PAGE rides in bits 5-9, but only for a data violation: an
 * instruction-protection fault reports no page (the handler has the saved PC instead).
 * See doc/Memory_Mapping.md, "Protection violations and how they are reported".
 */
#define GRP_OPRND_PROT 02000000 /* 20: data access to a closed page ("число в чужом листе") */
#define GRP_INSN_CHECK 0040000  /* 15: word not tagged as an instruction; also a jump to 0 */
#define GRP_INSN_PROT  0020000  /* 14: instruction fetch from a zero-descriptor page */
#define GRP_ILL_INSN   0010000  /* 13: privileged instruction attempted in user mode */
#define GRP_BREAKPOINT 0004000  /* 12: address-break match (М034/М035) -- TODO 17 single-step */
#define GRP_PAGE_MASK  0000760  /* 9-5: the faulting virtual page; recover it with >> 4 */
#define GRP_PAGE_SHIFT 4

/*
 * Bits of ПРП, the peripheral interrupt register (24 bits, masked by МПРП).
 *
 * ПРП has no interrupt line of its own: the processor raises GRP_SLAVE whenever
 * PRP & MPRP is non-zero, so a peripheral interrupt arrives as an ordinary ГРП
 * interrupt and the handler must read ПРП to find out which device it was.  The
 * bit is re-raised before every instruction while the ПРП bit is still up, so a
 * handler must clear the ПРП bit BEFORE dismissing GRP_SLAVE, or it will storm.
 *
 * None of the Consul bits are wired, so all four can be dismissed with EXT_PRPCLR.
 */
#define PRP_CONS1_INPUT 04000 /* 12: Consul 1 -- a character was typed */
#define PRP_CONS2_INPUT 02000 /* 11: Consul 2 -- a character was typed */
#define PRP_CONS1_DONE  01000 /* 10: Consul 1 -- printing finished */
#define PRP_CONS2_DONE  0400  /*  9: Consul 2 -- printing finished */

/*
 * Bits of READY2 (EXT_READY2) -- the polled alternative to the ПРП interrupt.
 * The Consul lowers its ready bit while it prints and the hardware raises it again
 * when the character is out.  Note that Consul 2 is bit 6, not bit 7.
 */
#define CONS1_READY 0200 /* 8: Consul 1 is idle */
#define CONS2_READY 040  /* 6: Consul 2 is idle */

/*
 * Bits of СПСВ, the saved program status / mode word (register 027), read back
 * after a trap.  РежЭ|РежПр == 0 iff the interrupted context was user mode
 * (doc/Unix_Context_Switch.md §3).  Octal, bits numbered right-to-left from 1.
 */
#define SPSW_MMAP_DISABLE 00001  /* БлП  - data mapping disabled */
#define SPSW_PROT_DISABLE 00002  /* БлЗ  - data protection disabled */
#define SPSW_EXTRACODE    00004  /* РежЭ - extracode mode */
#define SPSW_INTERRUPT    00010  /* РежПр - interrupt mode */
#define SPSW_MOD_RK       00020  /* ПрИК(РК) - the instruction loaded into the RK register
                                    must be modified by register M[16] */
#define SPSW_MOD_RR       00040  /* ПрИК(РР) - the instruction in the RR register
                                    was executed with modification */
#define SPSW_RIGHT_INSTR  00400  /* ПрК - right instruction indicator */
#define SPSW_NEXT_RK      01000  /* ГД./ДК2 - the instruction following the one that caused
                                    the interrupt has been loaded into the RK register */
#define SPSW_INTR_DISABLE 02000  /* БлПр - external interrupts disabled */

#endif /* _SYS_BESM6DEV_H */
