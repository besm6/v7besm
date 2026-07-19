//
// copyseg / clearseg -- copy and zero one physical page.
//
// void copyseg(unsigned s, unsigned d);   // one page, s -> d
// void clearseg(unsigned d);              // one page, d := 0
//
// The arguments are PAGE-ALIGNED physical word addresses (p_addr, and the stack/data
// page addresses derived from it, always are).  The kernel runs unmapped, so a kernel
// address IS a physical address -- but an unmapped access reaches only the low 32
// Kwords, and the page pool a process is built from lives ABOVE 0100000, out of that
// reach.  So a caddr_t cannot name these pages (its word field is only 15 bits), and
// the copy has to be done through a window, with mapping on -- exactly the bracket
// uarea.s uses, which is the worked example.
//
// The bracket:
//
//      virtual page 1 (02000-03777)  ->  the source page,  s >> 10   (copyseg)
//      virtual page 2 (04000-05777)  ->  the dest   page,  d >> 10   (copyseg)
//      virtual page 1 (02000-03777)  ->  the dest   page,  d >> 10   (clearseg)
//
// Both descriptors live in quartet 0 -- virtual pages 0..3 -- so stealing the window is
// ONE `mod 020', and so is putting it back.  Both window addresses are below 4096, so
// they fit the 12-bit short address field and the copy loop needs no `utc'.
//
// NOT virtual page 0.  A store to virtual address 0 is dropped and a load returns 0 --
// mmu_store()/mmu_load() test `addr == 0' BEFORE translation and before the "already
// physical" tag, so the black hole is in the VIRTUAL address, whatever page 0 is mapped
// to.  A window there would silently lose word 0 of the page.
//
// Interrupts are held off across the whole bracket, the same way and for the same
// reasons as uarea.s: `vtm <mask>' with register field 0 writes БлП, БлЗ and БлПр
// straight from the address field -- all three -- so the plain `vtm 2'/`vtm 3' of
// test/mmuhelp.s would ENABLE interrupts as a side effect.  These run at 02003/02002
// (БлПр set) and put ПСВ back exactly as found (`ita'/`ati' take a 5-bit register
// number in supervisor mode, so M[021] = ПСВ is reachable).
//
// The two drains are not ceremonial, and each covers a different hazard -- and a
// missing one shows up here and nowhere else (task 11):
//
//  * BEFORE stealing the quartet.  The kernel's own stores into the source page (or the
//    caller's, before it called us) were made unmapped, so their dirty БРЗ lines carry
//    PHYSICAL tags.  copyseg reads those same words MAPPED, under a virtual tag -- a
//    different tag, which misses the dirty line and reads stale memory.  (It also
//    satisfies the standing rule: drain before every РП write, or dirty lines are
//    written back through the new map.)
//
//  * AFTER the copy, before restoring the quartet.  The copy's stores were made mapped,
//    so their lines carry virtual tags and are written back through whatever map is
//    loaded when they are finally evicted.  They must go out while the window is still
//    installed.
//
// Both drains run with mapping off, which is what the nine stores to physical 1-7
// require.  The hazard is invisible under default SIMH and fatal under `set mmu cache'.
// See doc/Memory_Mapping.md, and kernel/TODO.md, "Five hardware rules".
//
// Contracts:
//  * `s' and `d' are PAGE-ALIGNED physical word addresses.
//  * On return РП is exactly as it was found: the old quartet 0 is read before the copy
//    and put back after.  The current process's map is otherwise untouched.
//  * Neither is reentrant (the bss temps are static), and neither touches r1-r7 or r15.
//  * copyseg needs no `s == d' guard: no caller ever passes equal addresses, and a page
//    copied to itself through two windows is idempotent anyway.
//
// This lives in its own file rather than in besm6.S so that kernel/test/mmutest can link
// the real routines: besm6.o cannot go into a standalone test (its 0500 vector reaches
// into the C kernel, and _start seeds no stack).  Same reason as brz.s, which it calls.

        .text

UPT    = 35                     // word offset of u_upt in struct user -- mmutest asserts it
PGSZ   = 1024                   // words in a page (sys/param.h)
WSRC   = 02000                  // virtual page 1: the source window (and clearseg's dest)
WDST   = 04000                  // virtual page 2: the dest window

// The РП descriptor bits of a quartet, as in utab.c's RPBITS(k): the low five bits of the
// page number sit at 5k+1..5k+5, and its bits 6-10 are scattered one per byte above bit 28.
// `aux' deposits a left-aligned source into the mask's set bits, which is what utab.c does.
//
//      k=1 (virtual page 1)  .46|.42|.38|.34|.30|.[6:10]
//      k=2 (virtual page 2)  .47|.43|.39|.35|.31|.[11:15]
//
// Written out at each use rather than equated: a symbol's value is only 24 bits, so an
// equate would silently truncate the scattered high bits (verified in uarea.s).  An inline
// `#(expr)' is evaluated in the full 48.

// ----------------------------------------------------------------------------
// void copyseg(unsigned s, unsigned d) -- copy one page, s -> d.
// ----------------------------------------------------------------------------
        .globl  copyseg
copyseg:
     13 mtj     8               // r8 := the return address; drainbrz's `vjm' clobbers r13
        atx     <segd>          // stash d (in A) -- still unmapped, so this is our own word
     15 xta     -1              // A := s, the pushed argument
        atx     <segs>          // stash s
        ita     021             // A := ПСВ
        ati     011             // r9 := ПСВ, to be put back exactly on the way out
        vtm     02003           // БлП|БлЗ|БлПр: unmapped, unprotected, INTERRUPTS OFF
        xta     <u+UPT>         // A := u.u_upt[0].  РП cannot be read back, so save it.
        atx     <oldq>
     13 vjm     drainbrz        // flush БРЗ through the CURRENT map, before we change it
        xta     <segs>
        asn     64-28           // A := s << 28 == (s >> 10) << 38, left-aligned.
                                //   `asn' shifts by (addr & 0177) - 64, and a POSITIVE
                                //   count shifts RIGHT -- so a left shift by 28 is 64-28.
        aux     #(.46|.42|.38|.34|.30|.[6:10])   // source scattered into virtual page 1
        atx     <segq>          // stash the partial quartet
        xta     <segd>
        asn     64-28           // dest page << 38, left-aligned: see the note above
        aux     #(.47|.43|.39|.35|.31|.[11:15])  // dest scattered into virtual page 2
        aox     <segq>          // combine the two windows into one quartet
        mod     020             // РП0..3 := the window quartet
        vtm     02002           // clear БлП: data now goes through РП.  Registers only below:
                                //   the kernel's own data is not addressable while it is on.
        vtm     1-PGSZ, 10
cp1: 10 xta     WSRC+PGSZ-1     // A := the source page's word, through window 1
     10 atx     WDST+PGSZ-1     // ...store it into the dest page, through window 2
     10 vlm     cp1
        vtm     02003           // set БлП: back to physical addressing
     13 vjm     drainbrz        // write the copy back through the window, before it goes away
        xta     <oldq>
        mod     020             // restore РП0..3
        ita     011
        ati     021             // restore ПСВ (interrupts as the caller had them)
      8 mtj     13
     15 utm     -1              // pop the pushed argument (s)
     13 uj

// ----------------------------------------------------------------------------
// void clearseg(unsigned d) -- zero one page.
// ----------------------------------------------------------------------------
// The same bracket, one window: the dest at virtual page 1.  A is cleared to zero with a
// bare `xta' (virtual address 0 is the black hole -- a load from it returns 0) before the
// store loop, while mapping is still off.
        .globl  clearseg
clearseg:
     13 mtj     8
        atx     <segd>          // stash d (in A)
        ita     021
        ati     011
        vtm     02003
        xta     <u+UPT>         // the current process's quartet 0, saved before the copy
        atx     <oldq>
     13 vjm     drainbrz
        xta     <segd>
        asn     64-28           // dest page << 38, left-aligned
        aux     #(.46|.42|.38|.34|.30|.[6:10])   // dest scattered into virtual page 1
        mod     020
        xta                     // A := 0 (address 0 is the black hole), while unmapped
        vtm     02002
        vtm     1-PGSZ, 10
cl1: 10 atx     WSRC+PGSZ-1     // zero the dest page, through window 1
     10 vlm     cl1
        vtm     02003
     13 vjm     drainbrz        // the stores were made mapped: flush before the map changes
        xta     <oldq>
        mod     020
        ita     011
        ati     021
      8 mtj     13
     13 uj                      // one parameter (d, in A): nothing pushed to pop

        .bss
segs:   . = . + 1               // the s argument, held across the drainbrz calls
segd:   . = . + 1               // the d argument
segq:   . = . + 1               // the window quartet we build
oldq:   . = . + 1               // the quartet we stole, to put back
