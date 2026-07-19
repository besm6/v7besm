//
// crt0w.s -- startup, vectors and the register-level test helper for `uswtch',
// the standalone SIMH test of save()/resume()/idle() and the u-area invariant.
//
// Unlike crt0u.S/crt0t.S/crt0s.S/crt0c.S this forges no user mode: everything under test
// here runs in the kernel's own world.  What it does instead is put the machine into the
// shape the switch assumes, which the other tests never had to:
//
//   * `u' IS THE REAL u-AREA, at physical 076000.  It has to be: uarea.s windows the live
//     u-area as physical page 31 (a hardcoded descriptor), so a `u' declared as ordinary
//     bss the way crt0c.S declares it would have uflush() copying the wrong page.  `u' is
//     therefore an absolute symbol here exactly as in kernel/besm6.S.
//
//   * MAIN RUNS ON THE u-PAGE STACK, at 076400 -- past struct user (~140 words) and with
//     ~768 words of room below 0100000.  This matters: resume() reloads the u page out from
//     under its own caller, so a test running on some other stack would never exercise the
//     thing that makes the switch delicate.
//
// The 0501 gate is crt0.s's short save, not the kernel's FULSAV: extintr() is an ordinary C
// function that preserves r1-r7 and r15, so the accumulator and r8-r14 are all that is left
// to save.  No frame is built, hence `intrframe' is a plain cell (uswtch.c defines it) and
// this test's clock() stub ignores it.
//

        u     = 076000            // must agree with UBASE in include/sys/param.h
        ustk  = 076400            // main()'s stack, inside the u page and past struct user

        .globl  u

// ----------------------------------------------------------------------------
// Interrupt vectors
// ----------------------------------------------------------------------------

        .const
        . = . - 010 + 0500
        uj      fault           // 0500: internal interrupt -- a bug, in this test
      : uj      intrgate        // 0501: external interrupt (ГРП) -- leg C's tick

        .text
        .globl  _start
_start:
     15 vtm     ustk            // the u-page stack: see the header
        vtm     02003           // ПСВ := БлП|БлЗ|БлПр -- unmapped, unprotected, and
                                //   interrupts OFF.  Leg C opens them itself, through the
                                //   real intrinit()/spl0(), so that what it proves is that
                                //   THOSE work and not that the crt0 left the door ajar.
     13 vjm     main            // main()'s status stays in the accumulator

// void halt(int status) -- the single argument arrives in the accumulator, which is exactly
// where the .ini reads it.  _start falls into it, so a main() that returns halts too.
        .globl  halt
halt:   stop
        uj      halt            // resumed?  halt again

// An internal interrupt means we faulted; halt somewhere the .ini can recognise.
fault:  stop    07777
        uj      fault

// ----------------------------------------------------------------------------
// int regtest(label_t lab) -- leg A: do all nine slots survive a save()/resume()?
// ----------------------------------------------------------------------------
// Loads r1-r7 with sentinels, save()s, resume()s through the FAST path (paddr == uhome, so
// nothing is copied and this leg tests the register file alone), and on the nonzero return
// parks r1-r7 and r15 in cells for uswtch.c to check.  Returns 0; the caller reads the
// cells.  r13 is not checked directly and does not need to be: it is the address the
// resume() jumps back to, so if it were wrong control would never reach rt_back at all.
//
// The caller's r1-r7 are banked and put back, because they are callee-saved and this
// routine spends them.  So is r13 -- the first `13 vjm save' below destroys it.

        .globl  regtest
regtest:
        atx     <rt_lab>        // the label pointer arrived in the accumulator
        ita     13
        atx     <rt_ret>        // bank our own return address: `vjm save' is about to eat it
        ita     1
        atx     <rt_s1>
        ita     2
        atx     <rt_s2>
        ita     3
        atx     <rt_s3>
        ita     4
        atx     <rt_s4>
        ita     5
        atx     <rt_s5>
        ita     6
        atx     <rt_s6>
        ita     7
        atx     <rt_s7>

        xta   #(01111)          // sentinels: 15-bit, index registers being 15 bits
        ati     1
        xta   #(02222)
        ati     2
        xta   #(03333)
        ati     3
        xta   #(04444)
        ati     4
        xta   #(05555)
        ati     5
        xta   #(06666)
        ati     6
        xta   #(07777)
        ati     7

        xta     <rt_lab>
     14 vtm     -1
     13 vjm     save
        u1a     rt_back         // nonzero -> we came back through resume()

        xta     <uhome>         // first return: hand ourselves straight back
        xts     <rt_lab>        //   push paddr, leave lab in A -- the two-argument shape
     14 vtm     -2
     13 vjm     resume          // does not return; reappears at the save() above

rt_back:
        ita     1
        atx     <rt_r1>
        ita     2
        atx     <rt_r2>
        ita     3
        atx     <rt_r3>
        ita     4
        atx     <rt_r4>
        ita     5
        atx     <rt_r5>
        ita     6
        atx     <rt_r6>
        ita     7
        atx     <rt_r7>
        ita     15
        atx     <rt_r15>

        xta     <rt_s1>         // put the caller's registers back
        ati     1
        xta     <rt_s2>
        ati     2
        xta     <rt_s3>
        ati     3
        xta     <rt_s4>
        ati     4
        xta     <rt_s5>
        ati     5
        xta     <rt_s6>
        ati     6
        xta     <rt_s7>
        ati     7
        xta     <rt_ret>
        ati     13
        xta                     // A := 0
     13 uj

// ----------------------------------------------------------------------------
// intrgate -- external interrupt entry, calls extintr() in C
// ----------------------------------------------------------------------------
// crt0.s's stub verbatim.  extintr() preserves r1-r7 and r15 (that is the ABI) and the
// hardware saved the PC in M[033], so the accumulator and r8-r14 are all this has to keep.
// Interrupts stay blocked throughout -- the hardware sets БлПр at the vector and `выпр'
// restores it from СПСВ -- so one static save area is enough.

intrgate:
        atx     <sa>
        ita     010
        atx     <s8>
        ita     011
        atx     <s9>
        ita     012
        atx     <s10>
        ita     013
        atx     <s11>
        ita     014
        atx     <s12>
        ita     015
        atx     <s13>
        ita     016
        atx     <s14>

     13 vjm     extintr

        xta     <s14>
        ati     016
        xta     <s13>
        ati     015
        xta     <s12>
        ati     014
        xta     <s11>
        ati     013
        xta     <s10>
        ati     012
        xta     <s9>
        ati     011
        xta     <s8>
        ati     010
        xta     <sa>
      3 ij                      // выпр

        .bss

sa:     . = . + 1
s8:     . = . + 1
s9:     . = . + 1
s10:    . = . + 1
s11:    . = . + 1
s12:    . = . + 1
s13:    . = . + 1
s14:    . = . + 1

rt_lab: . = . + 1               // regtest's argument
rt_ret: . = . + 1               // ...and its own return address

rt_s1:  . = . + 1               // the caller's r1-r7, banked
rt_s2:  . = . + 1
rt_s3:  . = . + 1
rt_s4:  . = . + 1
rt_s5:  . = . + 1
rt_s6:  . = . + 1
rt_s7:  . = . + 1

        .globl  rt_r1, rt_r2, rt_r3, rt_r4, rt_r5, rt_r6, rt_r7, rt_r15
rt_r1:  . = . + 1               // what came back, for uswtch.c to check
rt_r2:  . = . + 1
rt_r3:  . = . + 1
rt_r4:  . = . + 1
rt_r5:  . = . + 1
rt_r6:  . = . + 1
rt_r7:  . = . + 1
rt_r15: . = . + 1
