// ============================================================================
// The context switch: save() and resume()
// ============================================================================
//
// int  save(label_t lbl);            -- setjmp-like; returns 0 direct, 1 when resumed
// void resume(int paddr, label_t lbl) -- longjmp-like; does not return to its caller
//
// These live here, and not in besm6.S, for the reason brz.s/uarea.S/seg.S/usermem.S do:
// besm6.o cannot enter a standalone test -- its 0500 vector reaches into the C kernel and
// its _start seeds no stack -- and kernel/test/uswtch links the REAL switch, not a copy.
// idle() is not here at all: with spl now masking БлПр (intr.c) it is ordinary C.
//
// WHAT resume() SWITCHES, AND WHAT IT DOES NOT
//
// It switches the U-AREA.  It never writes РП -- resume() is NOT the address-space switch,
// which is what every surviving v7 comment in the tree still gets wrong.  Two things put it
// that way:
//
//   * The kernel runs unmapped (БлП = БлЗ = 1), so a kernel address IS a physical address
//     and reloading РП would change nothing the kernel can see.  The map is reloaded by
//     sureg(), which every landing site that goes on to return to user already calls on the
//     save()-returned-nonzero arm (slp.c, text.c).  The two landing sites without one are
//     correct as they stand: slp.c's second save() falls into the process-search loop and
//     resumes someone else, and proc 0 has no user map worth loading.
//
//   * The u-area is a fixed PHYSICAL page at 076000, not a fixed virtual one, so it has to
//     be COPIED: out to the outgoing process's home, in from the incoming one's.  That is
//     uflush()/uload() (uarea.S), and it is the price of an unmapped kernel.
//
// The label pointer survives the swap by being a constant: u.u_qsav is 076000+n in EVERY
// process, so the pointer may be captured before the copy and dereferenced after it, by
// which time its CONTENTS are the incoming process's.  That is the whole trick.
//
// See kernel/TODO.md ("The u-area invariant") and doc/Memory_Mapping.md.  The invariant's
// C-side rules -- who must flush, and when -- are written up once, at xswap() in text.c.

        .text

// ----------------------------------------------------------------------------
// int save(label_t lbl)
// ----------------------------------------------------------------------------
//
// Stores the nine registers a kernel thread of control needs and returns 0.  When some
// other context later calls resume(paddr, lbl), execution reappears here returning 1.
//
// The label slots, written down once (label_t is int[10]; slot 9 is reserved, unused):
//
//      0..6    r1..r7          the callee-saved set
//      7       r13             the return address into save()'s caller
//      8       r15             the kernel stack pointer
//      9       -- unused --
//
// NOT the mode register R, even though the hardware neither saves nor restores it.  The ABI
// fixes R = 7 at every function entry and exit, so the R that resume() is entered with is by
// construction the R that the save() it reappears in is entitled to return with; there is no
// ω-mode to carry across.  (15a's "the hardware never saves R" is true and points the other
// way: it applies to the GATES, which interrupt arbitrary code mid-function.  A switch
// happens at a call boundary in both contexts.)
//
// Mechanically a leaf.  One parameter, so it arrives in the accumulator and nothing is
// pushed: r15 at entry is already the caller's and r13 is already the caller's return
// address.  No b$save prologue, and no `15 utm' on the way out (a callee whose arguments
// were all in registers pops nothing).
//
// The stores go into the live u-area UNMAPPED, so their dirty БРЗ lines carry physical
// tags -- which is exactly what uarea.S's first drain covers.  Nothing extra is owed here.

        .globl  save
save:
        ati     14               // r14 := &label.  DECIMAL 14: a bare literal is decimal and
                                 //   a leading 0 makes it octal, so the `ati 012' idiom
                                 //   elsewhere in the kernel means r10, not r12.  No
                                 //   `aax #077777' first: `ati' keeps only the low 15 bits
                                 //   anyway, so a fat-pointer marker cannot survive it.  r14
                                 //   is the argument-count register and is dead by now.
        ita     1
     14 atx     0                // slot 0 := r1
        ita     2
     14 atx     1
        ita     3
     14 atx     2
        ita     4
     14 atx     3
        ita     5
     14 atx     4
        ita     6
     14 atx     5
        ita     7
     14 atx     6                // slot 6 := r7
        ita     13
     14 atx     7                // slot 7 := r13, the caller's return address
        ita     15
     14 atx     8                // slot 8 := r15, the kernel stack pointer
        xta                      // A := 0 -- the direct return
     13 uj

// ----------------------------------------------------------------------------
// void resume(int paddr, label_t lbl)
// ----------------------------------------------------------------------------
//
//      mask; if (paddr != uhome) { if (uhome) uflush(uhome); uload(paddr); uhome = paddr; }
//      restore r1-r7, r13, r15 from lbl; A := 1; unmask; jump.
//
// Four things make it sharper than it reads:
//
//  * `paddr' DOES NOT FIT AN INDEX REGISTER.  It is a physical word address over 512 Kwords
//    -- 19 bits -- and index registers are 15.  Both arguments are parked in static cells,
//    exactly as uarea.S parks parg/upt0.  Non-reentrant, like everything on this path.
//
//  * ITS OWN ARGUMENTS ARE ON THE DOOMED STACK.  `paddr' arrives pushed at r15-1, inside the
//    u page uload() is about to overwrite, so both arguments must be in their cells BEFORE
//    the flush -- and the label may only be dereferenced AFTER the load, which is precisely
//    what makes it name the incoming process.
//
//  * THE MASK SPANS BOTH COPIES, NOT EACH ONE.  uflush/uload each hold БлПр internally and
//    put PSW back as they found it; an interrupt landing in the gap between them would build
//    a frame on a kernel stack that has just been flushed and is about to be overwritten.
//    So resume() sets БлПр itself across the pair and restores the entry PSW at the end.
//    The state it restores belongs to the OUTGOING caller, which is safe because every
//    landing site re-establishes its own level (sleep()'s splx(s), the spl6()/spl0() in
//    swtch()'s loop).
//
//  * IT CALLS uload() FROM ASSEMBLY, as that routine's contract requires: uload destroys its
//    caller's frame, so it keeps all its state in r8/r9/r10 and static cells and never
//    touches r15.  Which is also why r8/r9/r10 are off limits here, across both calls.
//
// resume() never returns, so the pushed argument is never popped and r13 is dead on entry;
// r15 comes from the label.

        .globl  resume
resume:
        atx     <rlbl>           // park lbl (it arrived in the accumulator)
     15 xta     -1               // A := paddr, the one pushed argument
        atx     <rpaddr>
        ita     021              // A := PSW
        atx     <rpsw>           //   bank the caller's, to be restored exactly
        vtm     02003            //   БлП|БлЗ|БлПр: interrupts off across BOTH copies.  The
                                 //   mode write (register field 0); БлП/БлЗ are already set,
                                 //   this path being ordinary unmapped kernel code (psw.s).
        xta     <rpaddr>
        aex     <uhome>
        uza     rfast            // paddr == uhome -> the live u-area is already the right one
        xta     <uhome>
        uza     rload            // uhome == NOUHOME -> the old home is gone; nothing to flush
        xta     <uhome>
     14 vtm     -1
     13 vjm     uflush           // live u-area -> the outgoing process's home
rload:
        xta     <rpaddr>
     14 vtm     -1
     13 vjm     uload            // the incoming process's home -> live u-area.  From here on
                                 //   the label, and the kernel stack, are the NEW process's.
        xta     <rpaddr>
        atx     <uhome>
rfast:
        xta     <rlbl>
        ati     14               // r14 := &label (decimal 14), dead as the arg-count register
     14 xta     0
        ati     1                // r1 := slot 0
     14 xta     1
        ati     2
     14 xta     2
        ati     3
     14 xta     3
        ati     4
     14 xta     4
        ati     5
     14 xta     5
        ati     6
     14 xta     6
        ati     7                // r7 := slot 6
     14 xta     7
        ati     13               // r13 := the return address inside the matching save()
     14 xta     8
        ati     15               // r15 := the new process's kernel stack pointer
        xta     <rpsw>
        ati     021              // restore the entry PSW (and with it БлПр)
        xta     #(1)             // A := 1 -- the matching save() returns nonzero
     13 uj                       // ... and lands there, not in our caller

// ============================================================================
// Data
// ============================================================================

        .data

// int uhome -- the p_addr whose u-area is the one currently live at UBASE.  The live u-area
// is at 076000 and the copy in the process's image is stale between switches; resume()
// flushes and reloads when it finds uhome != the incoming p_addr.  NOUHOME (0) means the
// live u-area belongs to no in-core image, because the image it belonged to has just been
// freed -- see the rules at xswap() in text.c.  No image ever lives at physical 0, so 0 is
// also the right thing for this cell to hold during boot, before main() seeds it.
        .globl  uhome
uhome:  .word 0                  // set to proc[0].p_addr at boot by main() (kernel/main.c)

        .bss

rpaddr: . = . + 1                // the incoming p_addr, held across the uflush/uload calls
rlbl:   . = . + 1                // the label pointer, dereferenced only after the uload
rpsw:   . = . + 1                // the caller's PSW, put back before the final jump
