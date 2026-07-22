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
 *
 * The two controllers of each kind are ADJACENT, so a driver says `ctlr + EXT_DRUM1' rather
 * than branching on ctlr to reach two constants: a variable plus a constant is what the
 * compiler folds into the instruction's own address field, leaving one `wtc' beside it
 * (doc/Intrinsics.md §8 -- and the operand order matters, see dev/mb.c).  The `...4' names
 * below are therefore documentation of the map more than they are call sites.
 */
#define EXT_DRUM1    01    /* drum 1: exchange control word -- starts the transfer */
#define EXT_DRUM2    02    /* drum 2: likewise */
#define EXT_DISK3    03    /* disk controller 3: exchange control word -- sets up only */
#define EXT_DISK4    04    /* disk controller 4: likewise */
#define EXT_DISKCTL3 023   /* disk controller 3: commands; the track address transfers */
#define EXT_DISKCTL4 024   /* disk controller 4: likewise */
#define EXT_PRPCLR   030   /* clear ПРП: PRP &= ACC -- a ZERO bit clears */
#define EXT_MPRP     034   /* write МПРП, the mask of ПРП (24 bits) */
#define EXT_PRPHI    04030 /* read ПРП bits 13-24 */
#define EXT_PRPLO    04034 /* read ПРП bits 1-12; bits 1-5 always read as 1 */
#define EXT_DISKSTAT3 04003 /* disk controller 3: read the status register */
#define EXT_DISKSTAT4 04004 /* disk controller 4: likewise */
#define EXT_IOERR    04035 /* опрос триггера ОШМ: drum|disk|tape error masks, OR'd */

/*
 * Bits of the word EXT_IOERR returns.  ONE mask, shared by the drums, the disks and the
 * tapes -- which is why it lives here and not in sys/besm6disk.h: that header owns one
 * device family's accumulator layout, this one owns the numbers every device competes for.
 *
 * A bit stands when a command was issued to a unit that is NOT ATTACHED; the device then
 * transfers nothing and raises no completion interrupt, so a driver that does not look
 * here waits forever.  A successful command clears the bit again.
 */
#define IOERR_DRUM(n) (0100 >> (n)) /* drum n (0, 1): 033 1 / 033 2 found nothing there */
#define IOERR_DISK(n) (020 >> (n))  /* disk controller n likewise -- task 18b.5 */
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
 * The mass-storage completion bits.  All four are WIRED: they are live wires from the
 * device rather than flip-flops in ГРП, so MOD_GRPCLR cannot lower them -- only giving
 * the device a new command does.  extintr()'s fallback arm, which dismisses the highest
 * pending bit so that an unhandled source cannot spin, would therefore spin on any of
 * them; it probes for exactly this and disarms the bit in МГРП instead.
 *
 * "Free" means IDLE, not "an exchange just finished": the bit is up whenever no transfer
 * is running, so none of these may be armed in МГРП outside a live exchange.  A driver
 * brackets one exchange with mgrpon()/mgrpoff() (kernel/intr.c); kernel/test/ugrp is the
 * test that holds both halves of this comment to account.
 *
 * The complete wired set covers the tape channels too -- doc/Besm6_Peripherals.md,
 * "Wired bits".  Only the four this kernel can raise are named here, plus one it cannot:
 * see GRP_CHAN5_FREE below.
 */
#define GRP_DRUM1_FREE 01000000000000000U /* 46: drum 1 exchange finished (wired) */
#define GRP_DRUM2_FREE 00400000000000000U /* 45: drum 2 exchange finished (wired) */
#define GRP_CHAN3_FREE 00000002000000000  /* 29: disk controller 3 finished (wired) */
#define GRP_CHAN4_FREE 00000001000000000  /* 28: disk controller 4 finished (wired) */

/*
 * A wired bit belonging to a device this kernel does NOT drive: tape channel 5.  It is here
 * for kernel/test/ugrp, whose part 1 forges a wired bit to prove extintr()'s fallback probe
 * disarms rather than spins on one -- which needs a bit with no handler, or the test
 * exercises the handler instead of the probe.  That test has now been evicted twice: it
 * started on GRP_DRUM1_FREE, moved to GRP_CHAN3_FREE when task 18b.3 gave the drum bits a
 * handler, and moved here when 18b.4 gave the disk bits one.  Nothing in this kernel can
 * raise a tape bit, so there is no third eviction coming.
 */
#define GRP_CHAN5_FREE 00000000400000000 /* 27: tape channel 5 free (wired, undriven) */

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
 * Bits of SPSW, the saved program status / mode word (register 027), read back
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

/*
 * Bits of PSW, the LIVE mode word (register 021).  The low two and БлПр sit where they do in
 * SPSW -- `выпр' copies one into the other -- but the rest do not: where SPSW reports how the
 * trap was taken, PSW carries the halt-on-fault switches.  Only the bits this kernel names are
 * here.  БлПр is owned by __besm6_maskpsw() -- setipl() in kernel/intr.c is the only writer, and
 * the gates in kernel/besm6.S emit the same instruction inline -- and __besm6_getpsw() is how C
 * reads it back, PSW being the one machine register that CAN be read back.
 */
#define PSW_MMAP_DISABLE 00001  /* БлП  - data mapping disabled */
#define PSW_PROT_DISABLE 00002  /* БлЗ  - data protection disabled */
#define PSW_INTR_HALT    00004  /* ПоП  - halt, do not vector, on an internal interrupt */
#define PSW_CHECK_HALT   00010  /* ПоК  - halt, do not vector, on an instruction check */
#define PSW_INTR_DISABLE 02000  /* БлПр - external interrupts disabled */

/*
 * The standing invariant: THIS KERNEL RUNS UNMAPPED WITH PROTECTION OFF (БлП = БлЗ = 1).
 *
 * `уиа' (vtm) with REGISTER FIELD 0 is a mode-word write in supervisor mode: M[0] always reads 0,
 * so the register half of the instruction is a no-op and the hardware spends it on PSW instead,
 * taking БлП, БлЗ and БлПр straight from the address field and writing all three at once.  That is
 * __besm6_maskpsw(), one instruction, and it is a MASKED write -- ПоП, ПоК and the write-watch bit
 * are not in the mask and do not move -- disturbing neither the accumulator nor ω.
 *
 * So every mask handed to __besm6_maskpsw() carries PSW_KERNEL and re-asserts the invariant on the
 * way past.  That is the point of it, not a hazard.  THE PRECONDITION IT BUYS: __besm6_maskpsw()
 * may only be issued from ordinary unmapped kernel context, never from inside a MAPPED bracket --
 * it would slam БлП back on and pull the mapping out from under the copy.  The brackets in
 * kernel/uarea.S, kernel/seg.S and kernel/usermem.S issue their own `vtm 02002'/`vtm 02003' and
 * bank the whole word with `ita'/`ati' precisely because they must preserve a БлПр they do not
 * know.  See doc/Intrinsics.md §3.3 and doc/Memory_Mapping.md.
 */
#define PSW_KERNEL (PSW_MMAP_DISABLE | PSW_PROT_DISABLE)

#endif /* _SYS_BESM6DEV_H */
