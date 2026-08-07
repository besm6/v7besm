//
// uflush / uload -- save and restore the u-area across a context switch.
//
// void uflush(unsigned paddr);         // the live u-area (074000) -> the process's home
// void uload (unsigned paddr);         // the process's home -> the live u-area (074000)
//
// The kernel runs unmapped, so a kernel address IS a physical address -- and an unmapped
// access reaches only the low 32 Kwords.  The live u-area is at the top of that reach,
// physical 074000 (page 30); a process's home copy sits at the head of its image at p_addr,
// which is a page ABOVE 0100000 and cannot be named by an unmapped access at all.  The only
// way to reach it is to spend a virtual page on a window and turn mapping on.
//
// So both routines are the same bracket:
//
//      virtual page 1 (02000-03777)  ->  the process's u home,  paddr >> 10
//      virtual page 2 (04000-05777)  ->  the live u-area,       UBASE >> 10
//
// AT MOST ONE PAGE IS COPIED, and the u-area is two pages long.  074000-075777 is the saved
// half -- `struct user' and the kernel stack that has to survive a context switch -- and it
// is what USIZE names and what a process image reserves at p_addr.  076000-077777 is stack
// OVERFLOW: the stack may grow into it and run there correctly, but nothing copies it, so a
// process that reaches sleep() or swtch() with r15 up there loses those frames to whatever
// runs next.  Task 25a: the page exists to absorb the nested interrupt frame that used to
// push r15 past 0100000 and wrap it to zero, and an interrupt handler never sleeps, so it
// always unwinds before the process can leave the CPU.  The full statement of the hazard is
// at UBASE in <sys/param.h>; do not spread it around.
//
// ONLY THE LIVE PART IS MOVED (task 30).  The saved page is 1024 words and half of it or
// more is dead: `struct user' is ~142, and above it the kernel stack grows UP from
// ustkbase, so everything above r15 is a frame that has already returned.  Measured on this
// machine, as u_stkdepth after the run: 516 words at the shell's first prompt, 392 at the
// end of the whole libc suite, against a worst case of 738 (the deepest resume() ever
// measured, r15 = 075302, plus SLACK).  So uflush measures r15, copies that many words, and
// RECORDS THE COUNT IN THE HOME PAGE at u_stkdepth (word USTKD); uload reads it back
// through the window before its own copy.  The count travels inside the page it describes,
// which is what makes it impossible for uload to name words that no uflush ever produced.
//
//   * uflush writes the count through the WINDOW, into the home -- not into the live page.
//     Two reasons, and both matter: it keeps uflush a pure READER of the live u-area, which
//     is what makes it safe to call from C; and `u' is not UBASE everywhere this file is
//     linked (kernel/test/mmutest declares `struct user u' as ordinary bss), so `<u+USTKD>'
//     would not name the page being copied.
//
//   * THE COUNT MUST COVER THE DEEPEST FRAME THAT CAN EVER BE RESUMED from this home, not
//     merely the one current when uflush ran.  That is a rule about CALL DEPTH, not about
//     interrupts: an interrupt frame is built above the interrupted r15 and an interrupt
//     handler never sleeps, so it has always unwound before anyone resumes.  The rule is
//
//         uflush(H) may only be called from a frame at least as deep as every label armed
//         in H that will later be resumed through.
//
//     Every caller obeys it, two of them with ZERO margin: newproc() (kernel/slp.c) does
//     save(u.u_ssav) and uflush(a1) from the same frame, and both take one argument, which
//     the ABI passes in the accumulator with no push -- so the two r15 are the same word.
//     swtch()'s save(u.u_rsav)/resume() pair has one word of margin, the pushed paddr.
//     SLACK exists because that margin is zero, not because the count is wrong.  A future
//     caller flushing somebody else's home from a shallower frame -- which xswap()'s
//     `p_addr == uhome' test deliberately permits -- would truncate it silently.
//
//   * TWO FALLBACKS, both landing on the whole page, which is what this file did before.
//     r15 outside [UBASE, UBASE + 2*USIZE) means the caller is not running on the u-page
//     stack at all: the standalone tests, whose crt0 seeds r15 into low bss.  r15 in the
//     OVERFLOW page (or SLACK pushing the count over) clamps to USIZE, because a longer
//     copy would run off the end of virtual page 1 and into the live window.  Task 25a's
//     hazard is unchanged by that clamp, in kind and in threshold.
//
// Both descriptors live in quartet 0 -- u.u_upt[0], virtual pages 0..3 -- so stealing the
// window is ONE `mod 020', and so is putting it back.  Both window addresses are below 4096,
// so they fit the 12-bit short address field and the copy loop needs no `utc'.
//
// NOT virtual page 0.  A store to virtual address 0 is dropped and a load returns 0 --
// mmu_store()/mmu_load() test `addr == 0' BEFORE translation and before the "already
// physical" tag, so the black hole is in the VIRTUAL address, whatever page 0 is mapped to.
// A window there would silently lose word 0 of the u-area: u_rsav[0], a saved register,
// quietly zeroed on every context switch.
//
// Interrupts are held off across the whole bracket.  `vtm <mask>' with register field 0
// writes БлП, БлЗ and БлПр straight from the address field -- all three, so the plain
// `vtm 2'/`vtm 3' of test/mmuhelp.s would ENABLE interrupts as a side effect.  Here that is
// fatal: an interrupt taken during uload's copy builds its frame on the kernel stack, which
// lives in the very page being overwritten, and the copy then walks over the handler's saved
// registers.  So the bracket runs at 02003/02002 (БлПр set) and puts PSW back exactly as it
// was found -- `ati'/`ita' take a 5-bit register number in supervisor mode, so M[021] = PSW
// is reachable.
//
// The two drains are not ceremonial, and each covers a different hazard:
//
//  * BEFORE stealing the quartet.  The kernel's own stores into the u-area (save() writing
//    u_rsav, ...) were made unmapped, so their dirty БРЗ lines carry PHYSICAL tags.  The copy
//    below reads those same words MAPPED, under a virtual tag -- a different tag, which misses
//    the dirty line and reads stale memory.  (It also satisfies the standing rule: drain before
//    every РП write, or dirty lines are written back through the new map.)
//
//  * AFTER the copy, before restoring the quartet.  The copy's stores were made mapped, so
//    their lines carry virtual tags and are written back through whatever map is loaded when
//    they are finally evicted.  They must go out while the window is still installed.
//
// Both drains run with mapping off, which is what the nine stores to physical 1-7 require.
// The hazard is invisible under default SIMH and fatal under `set mmu cache'.
// See doc/Memory_Mapping.md, and kernel/README.md, "The u-area invariant".
//
// Contracts:
//  * `paddr' is a PAGE-ALIGNED physical word address (p_addr always is).
//  * On return РП is exactly as it was found: the old quartet 0 is read before the copy and
//    put back after.  The incoming process's map is installed by the sureg() that every
//    resume() target already calls.
//  * uload DESTROYS its caller's kernel-stack frame -- the stack is in the page it overwrites.
//    It is callable only from assembly that keeps its state in registers, i.e. resume().
//    uflush only READS the live u page -- the count it writes goes to the HOME -- and is
//    safe to call from C, from a frame as deep as the labels in the page (see above).
//  * Neither is reentrant (parg/upt0 are static), and neither touches r1-r7 or r15.  They
//    use r8 (return address), r9 (PSW), r10 (the loop) and, in uflush, r12 (the count).
//
// This lives in its own file rather than in besm6.S so that kernel/test/mmutest can link the
// real routines: besm6.o cannot go into a standalone test (its 0500 vector reaches into the C
// kernel, and _start seeds no stack).  Same reason as brz.s, which it calls.

#include "sys/param.h"          // USIZE, UBASE

        .text

UPT    = 34                    // word offset of u_upt in struct user -- mmutest asserts it
USTKD  = UPT + 8                // word offset of u_stkdepth, immediately after u_upt[8]:
                                //   the two can only drift together, and mmutest checks both
SLACK  = 32                     // words of margin above r15.  Insurance, not correctness --
                                //   the count is exact by construction; see the header
WHOME  = 02000                  // virtual page 1: the window on the process's u home
WLIVE  = 04000                  // virtual page 2: the window on the live u-area

// The РП descriptor bits of a quartet, as in utab.c's RPBITS(k): the low five bits of the page
// number sit at 5k+1..5k+5, and its bits 6-10 are scattered one per byte above bit 28.  `aux'
// deposits a left-aligned source into the mask's set bits, which is what utab.c does too.
//
//      RPHOME  .46|.42|.38|.34|.30|.[6:10]   k=1: virtual page 1, the whole descriptor
//      RPLIVE  #(UBASE)                      k=2: virtual page 2 -> the live u-area's page
//
// RPLIVE is a constant, since the live u-area is at a fixed physical address -- and it is
// UBASE itself, which is not a coincidence worth re-deriving.  The k=2 field is bits 11-15,
// i.e. (page & 037) << 10, and its bits 6-10 (the ones scattered above bit 28) are zero for
// any page under 64.  UBASE is page-aligned and below 0100000, so its page number is under
// 32: (UBASE >> 10) << 10 == UBASE, and no scattered bits are needed.  Written that way, the
// descriptor cannot drift from the geometry -- it used to be spelled `.[11:15]', which is
// 076000, and moving UBASE a page down in task 25a would have left it silently windowing the
// OVERFLOW page while the copy loop read the saved one.
//
// The masks are written out at each use rather than equated: a symbol's value is only 24 bits,
// so `RPHOME = .46 | ...' would silently truncate to its low half (verified -- the mask came
// out 01740, the scattered bits gone).  An inline `#(expr)' is evaluated in the full 48.

// ----------------------------------------------------------------------------
// void uflush(unsigned paddr) -- the live u-area out to the process's home.
// ----------------------------------------------------------------------------
        .globl  uflush
uflush:
     13 mtj     8               // r8 := the return address; drainbrz's `vjm' clobbers r13
        atx     <parg>          // stash paddr -- still unmapped, so this is our own word
        ita     021             // A := PSW
        ati     011             // r9 := PSW, to be put back exactly on the way out
        vtm     02003           // БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF

        // How much of the page is live.  Measured with БлПр already up, so nothing can move
        // the stack between the measurement and the copy.  Index-register numbers are
        // DECIMAL here (`ati 011' elsewhere in this file is octal, i.e. r9 -- see switch.s).
        ita     15              // A := r15, zero-extended from its 15 bits
        aax     #(~(2*USIZE-1)) // strip the offset within the two u-area pages...
        aex     #(UBASE)        // ...zero iff r15 is inside [UBASE, UBASE + 2*USIZE)
        u1a     uffull          //   outside it: a standalone test, stack elsewhere entirely
        ita     15
        aax     #(2*USIZE-1)    // off := r15 - UBASE, 0..2047 (UBASE is 2*USIZE-aligned)
        ati     10
     10 utm     SLACK           // ... + the margin, IN THE REGISTER.  Not `arx': that leaves
                                //   ω MULTIPLICATIVE and would poison the test two lines on.
        ita     10
        aax     #(~(USIZE-1))   // USIZE or more?  r15 in the overflow page, or the margin
        uza     ufgo            //   pushed it over.  Clamp: a longer copy would run off the
                                //   end of virtual page 1 and into the live window.
uffull:
        vtm     USIZE, 10       // r10 := the whole saved page, which is what this used to do
ufgo:
     10 mtj     12              // r12 := the count; r10 is spent by the loop below

        xta     <u+UPT>         // A := u.u_upt[0].  РП cannot be read back, so save it.
        atx     <upt0>
     13 vjm     drainbrz        // flush БРЗ through the CURRENT map, before we change it.
                                //   (It touches no index register, so r10/r12 survive it.)
        xta     <parg>
        asn     64-28           // A := paddr << 28 == (paddr >> 10) << 38, left-aligned.
                                //   `asn' shifts by (addr & 0177) - 64, and a POSITIVE count
                                //   shifts RIGHT -- so a left shift by 28 is 64-28, not 64+28.
        aux     #(.46|.42|.38|.34|.30|.[6:10])   // scattered into virtual page 1's descriptor
        aox     #(UBASE)        // + virtual page 2 -> the live u-area: see RPLIVE above
        mod     020             // РП0..3 := the window quartet
        vtm     02002           // clear БлП: data now goes through РП.  Registers only below:
                                //   the kernel's own data is not addressable while it is on.
        // Descending, because the count is variable: `vlm' counts UP to zero and would need
        // the loop's address field to be base+count-1.  r10 enters holding the count, is
        // decremented before each word, so this touches indices count-1..0 and leaves at 0.
uf1: 10 utm     -1
     10 xta     WLIVE           // A := the live u-area's word
     10 atx     WHOME           // ...store it into the process's home
     10 v1m     uf1
        ita     12
        atx     WHOME+USTKD     // the count, INTO THE HOME, where uload reads it back.  Keep
                                //   it next to the loop: it is part of what was just copied.
        vtm     02003           // set БлП: back to physical addressing
     13 vjm     drainbrz        // write the copy back through the window, before it goes away
        xta     <upt0>
        mod     020             // restore РП0..3
        ita     011
        ati     021             // restore PSW (interrupts as the caller had them)
      8 mtj     13
     13 uj

// ----------------------------------------------------------------------------
// void uload(unsigned paddr) -- the process's home in over the live u-area.
// ----------------------------------------------------------------------------
// The same bracket; only the two instructions of the copy loop are exchanged, and the count
// is read out of the home page instead of measured.  Note that on return the live u-area --
// and with it the kernel stack this routine's caller is standing on -- belongs to the NEW
// process.  See the contract above.
        .globl  uload
uload:
     13 mtj     8
        atx     <parg>
        ita     021
        ati     011
        vtm     02003
        xta     <u+UPT>         // the OUTGOING process's quartet 0, saved before the copy
        atx     <upt0>
     13 vjm     drainbrz
        xta     <parg>
        asn     64-28           // left by 28: see the note in uflush
        aux     #(.46|.42|.38|.34|.30|.[6:10])
        aox     #(UBASE)
        mod     020
        vtm     02002
        xta     WHOME+USTKD     // A := the count the matching uflush recorded
        vtm     02003           // ...and mapping OFF again for the four instructions below.
                                //   This moves БлП only -- РП keeps the window -- and the
                                //   constant pool, which the window makes unreachable, is
                                //   what the range test needs.  Nothing has been WRITTEN
                                //   since the leading drain, so no БРЗ line is in flight.
        ati     10              // r10 := the count, truncated to 15 bits
        aax     #(~(USIZE-1))   // USIZE or more?  uflush's own clamp arms write exactly
        u1a     ulfull          //   USIZE, and anything larger is a home nobody flushed.
     10 v1m     ulgo            // Zero likewise -- and zero is FATAL to the loop below, where
                                //   `utm -1' would wrap r10 to 077777 and copy 32768 words.
ulfull:
        vtm     USIZE, 10       // either way, the whole page: what this used to do always
ulgo:
        vtm     02002           // clear БлП again for the copy
ul1: 10 utm     -1
     10 xta     WHOME           // A := the process's home word
     10 atx     WLIVE           // ...store it into the live u-area
     10 v1m     ul1
        vtm     02003
     13 vjm     drainbrz        // the copy was made mapped: flush it before the map changes
        xta     <upt0>
        mod     020
        ita     011
        ati     021
      8 mtj     13
     13 uj

        .bss
parg:   . = . + 1               // the paddr argument, held across the drainbrz calls
upt0:   . = . + 1               // the quartet we stole, to put back
