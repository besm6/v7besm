//
// uflush / uload -- save and restore the u-area across a context switch.
//
// void uflush(unsigned paddr);         // the live u-area (076000) -> the process's home
// void uload (unsigned paddr);         // the process's home -> the live u-area (076000)
//
// The kernel runs unmapped, so a kernel address IS a physical address -- and an unmapped
// access reaches only the low 32 Kwords.  The live u-area is the last page of that reach,
// physical 076000 (page 31); a process's home copy sits at the head of its image at p_addr,
// which is a page ABOVE 0100000 and cannot be named by an unmapped access at all.  The only
// way to reach it is to spend a virtual page on a window and turn mapping on.
//
// So both routines are the same bracket:
//
//      virtual page 1 (02000-03777)  ->  the process's u home,  paddr >> 10
//      virtual page 2 (04000-05777)  ->  physical page 31, the live u-area
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
// registers.  So the bracket runs at 02003/02002 (БлПр set) and puts ПСВ back exactly as it
// was found -- `ati'/`ita' take a 5-bit register number in supervisor mode, so M[021] = ПСВ
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
// See doc/Memory_Mapping.md, and kernel/TODO.md, "The u-area invariant".
//
// Contracts:
//  * `paddr' is a PAGE-ALIGNED physical word address (p_addr always is).
//  * On return РП is exactly as it was found: the old quartet 0 is read before the copy and
//    put back after.  The incoming process's map is installed by the sureg() that every
//    resume() target already calls.
//  * uload DESTROYS its caller's kernel-stack frame -- the stack is in the page it overwrites.
//    It is callable only from assembly that keeps its state in registers, i.e. resume().
//    uflush only READS the u page and is safe to call from C.
//  * Neither is reentrant (parg/upt0 are static), and neither touches r1-r7 or r15.
//
// This lives in its own file rather than in besm6.S so that kernel/test/mmutest can link the
// real routines: besm6.o cannot go into a standalone test (its 0500 vector reaches into the C
// kernel, and _start seeds no stack).  Same reason as brz.s, which it calls.

        .text

UPT    = 33                    // word offset of u_upt in struct user -- mmutest asserts it
USIZE  = 1024                   // words in the u-area (sys/param.h)
WHOME  = 02000                  // virtual page 1: the window on the process's u home
WLIVE  = 04000                  // virtual page 2: the window on the live u-area

// The РП descriptor bits of a quartet, as in utab.c's RPBITS(k): the low five bits of the page
// number sit at 5k+1..5k+5, and its bits 6-10 are scattered one per byte above bit 28.  `aux'
// deposits a left-aligned source into the mask's set bits, which is what utab.c does too.
//
//      RPHOME  .46|.42|.38|.34|.30|.[6:10]   k=1: virtual page 1, the whole descriptor
//      RPLIVE  .[11:15]                      k=2: virtual page 2 -> physical 31 (037 << 10)
//
// These are written out at each use rather than equated: a symbol's value is only 24 bits, so
// `RPHOME = .46 | ...' would silently truncate to its low half (verified -- the mask came out
// 01740, the scattered bits gone).  An inline `#(expr)' is evaluated in the full 48.

// ----------------------------------------------------------------------------
// void uflush(unsigned paddr) -- the live u-area out to the process's home.
// ----------------------------------------------------------------------------
        .globl  uflush
uflush:
     13 mtj     8               // r8 := the return address; drainbrz's `vjm' clobbers r13
        atx     <parg>          // stash paddr -- still unmapped, so this is our own word
        ita     021             // A := ПСВ
        ati     011             // r9 := ПСВ, to be put back exactly on the way out
        vtm     02003           // БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
        xta     <u+UPT>         // A := u.u_upt[0].  РП cannot be read back, so save it.
        atx     <upt0>
     13 vjm     drainbrz        // flush БРЗ through the CURRENT map, before we change it
        xta     <parg>
        asn     64-28           // A := paddr << 28 == (paddr >> 10) << 38, left-aligned.
                                //   `asn' shifts by (addr & 0177) - 64, and a POSITIVE count
                                //   shifts RIGHT -- so a left shift by 28 is 64-28, not 64+28.
        aux     #(.46|.42|.38|.34|.30|.[6:10])   // scattered into virtual page 1's descriptor
        aox     #.[11:15]       // + virtual page 2 -> physical 31, the live u-area
        mod     020             // РП0..3 := the window quartet
        vtm     02002           // clear БлП: data now goes through РП.  Registers only below:
                                //   the kernel's own data is not addressable while it is on.
        vtm     1-USIZE, 10
uf1: 10 xta     WLIVE+USIZE-1   // A := the live u-area's word
     10 atx     WHOME+USIZE-1   // ...store it into the process's home
     10 vlm     uf1
        vtm     02003           // set БлП: back to physical addressing
     13 vjm     drainbrz        // write the copy back through the window, before it goes away
        xta     <upt0>
        mod     020             // restore РП0..3
        ita     011
        ati     021             // restore ПСВ (interrupts as the caller had them)
      8 mtj     13
     13 uj

// ----------------------------------------------------------------------------
// void uload(unsigned paddr) -- the process's home in over the live u-area.
// ----------------------------------------------------------------------------
// The same bracket; only the two instructions of the copy loop are exchanged.  Note that on
// return the live u-area -- and with it the kernel stack this routine's caller is standing on
// -- belongs to the NEW process.  See the contract above.
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
        aox     #.[11:15]
        mod     020
        vtm     02002
        vtm     1-USIZE, 10
ul1: 10 xta     WHOME+USIZE-1   // A := the process's home word
     10 atx     WLIVE+USIZE-1   // ...store it into the live u-area
     10 vlm     ul1
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
