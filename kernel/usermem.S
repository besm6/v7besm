//
// usermem.S -- fault-safe access to user memory.
//
//   int fubyte(caddr_t addr);              int fuword(caddr_t addr);
//   int subyte(caddr_t addr, int value);   int suword(caddr_t addr, int value);
//   int copyin (caddr_t from, caddr_t to, unsigned nbytes);   // user  -> kernel
//   int copyout(caddr_t from, caddr_t to, unsigned nbytes);   // kernel -> user
//
// These six cross the user/kernel boundary.  The kernel runs unmapped (БлП set), so a kernel
// address IS a physical address; the user's pages live in РП, the current process's map.  So
// unlike uarea.S/seg.S there is NO window to steal -- the user's map is already loaded.  The
// copy just toggles БлП per word: read the user word MAPPED, store it to the kernel buffer
// UNMAPPED (copyin), or the reverse (copyout).  That asymmetry -- supervisor instruction fetch
// is never mapped, but supervisor data fetch obeys БлП -- is what makes copyin cheap here.
// See doc/Memory_Mapping.md and kernel/test/mmuhelp.s (the peek/poke this shape came from).
//
// There is NO nofault/trap-recovery path.  Each routine validates its range up front with
// useracc() (utab.c) -- the BESM-6 replacement for v7's ckr/ckw -- and a range that runs into an
// unmapped page returns -1 without ever touching user memory.  (No C caller calls useracc: like
// v7's ckr/ckw, it is invoked from *inside* each routine.)
//
// No drainbrz.  РП is never written, so there is no "drain before the РП write" obligation; and
// copyout's mapped stores are written back through the SAME map that is loaded, so no БРЗ tag
// hazard.  (A context switch, which does reload РП, drains -- that is resume()'s job, not ours.)
//
// The mode-bit discipline is uarea.S's, minus the mod/drain: these run in supervisor mode (they
// are reached during a syscall/trap), so ita/ati 021 name PSW = M[021].  Save PSW on entry and
// restore it exactly on exit, to preserve the caller's БлПр (interrupt-enable); toggle mapping
// with `vtm 02002' (БлП clear = map on, БлЗ+БлПр held) and `vtm 02003' (unmapped, БлЗ+БлПр held).
// Never clear БлЗ.  Interrupts are held off across the copy (БлПр kept set) -- these copies are
// small; the shape stays identical to the proven uflush/uload.  While mapping is on the routine's
// own bss is unaddressable (virtual 076000+ then names the user's page), so the copy is
// REGISTER-ONLY: pointers and counter in index registers, the word in A.
//
// useracc is a `vjm' call, which clobbers r13 and every r8..r14; so the return address, the word
// count and the arguments that must survive the call live in bss temps (read back after it), not
// in registers.  Non-reentrant (the bss temps are static), same as uarea.S/seg.S -- no interrupt
// handler calls these.  r1-r7 are untouched (useracc preserves them for us).
//
// copyin/copyout are WORD-ONLY: they copy nbytes/NBPW whole words.
// Every caller passes word-aligned addresses and a word-multiple byte count (iomove's NBPW guard,
// struct copyouts, icode); an unaligned copy stays on the fubyte/subyte byte path in iomove.  The
// divide by NBPW=6 is a short subtract-6 loop that floors and cannot spin on a non-multiple.
//
// This lives in its own file rather than in besm6.S so that kernel/test/mmutest can link the real
// routines: besm6.o cannot go into a standalone test (its 0500 vector reaches into the C kernel,
// and _start seeds no stack).  Same reason as uarea.S/seg.S/brz.s.

#include "sys/param.h"                   // NBPW

        .text

        .globl  useracc                 // C, in utab.c: useracc(word_addr, word_count, rw)

// ----------------------------------------------------------------------------
// int copyin(caddr_t from, caddr_t to, unsigned nbytes) -- user -> kernel.
// ----------------------------------------------------------------------------
// from (user) and to (kernel) are pushed; nbytes is in A.  Returns 0, or -1 if the user range
// is not fully mapped.
        .globl  copyin
copyin:
        atx     <nbtmp>         // stash nbytes (was in A)
        ita     015             // A := r13, the return address
        atx     <retpc>
        xta     <nbtmp>         // A := nbytes
     13 vjm     divword         // nwords := nbytes / NBPW  (floors)
        // useracc(from_word, nwords, 0): validate the USER side (from)
     15 xta     -2              // A := from
        aax     #077777         // A := from's word address
        xts     <nwords>        // push from_word; A := nwords
        xts                     // push nwords;    A := 0 (rw)
     14 vtm     -3
     13 vjm     useracc
        aox                     // force logical ω without changing A
        uza     ci_flt          // A == 0 -> the range is bad
        ita     021             // save PSW
        ati     011             //   in r9
     15 xta     -2
        aax     #077777
        ati     013             // r11 := from_word  (user source)
     15 xta     -1
        aax     #077777
        ati     014             // r12 := to_word    (kernel dest)
        xta     <nwords>
        ati     012             // r10 := word count
        vtm     02003           // unmapped, interrupts off, entering the copy
ci_lp:
     10 vzm     ci_dn
        vtm     02002           // map on
     11 xta                     // A := user[r11], mapped
        vtm     02003           // map off
     12 atx                     // kernel[r12] := A, unmapped
     11 utm     1
     12 utm     1
     10 utm     -1
        uj      ci_lp
ci_dn:
        ita     011
        ati     021             // restore PSW (interrupts as the caller had them)
        xta     <retpc>
        ati     015             // r13 := return address
     15 utm     -2              // pop the two pushed args (from, to)
        xta                     // A := 0
     13 uj
ci_flt:
        xta     <retpc>
        ati     015
     15 utm     -2
        xta     <mone>          // A := -1
     13 uj

// ----------------------------------------------------------------------------
// int copyout(caddr_t from, caddr_t to, unsigned nbytes) -- kernel -> user.
// ----------------------------------------------------------------------------
        .globl  copyout
copyout:
        atx     <nbtmp>
        ita     015
        atx     <retpc>
        xta     <nbtmp>
     13 vjm     divword
        // useracc(to_word, nwords, 1): validate the USER side (to)
     15 xta     -1              // A := to
        aax     #077777
        xts     <nwords>        // push to_word; A := nwords
        xts     #1              // push nwords;  A := 1 (rw = write)
     14 vtm     -3
     13 vjm     useracc
        aox
        uza     co_flt
        ita     021
        ati     011             // save PSW in r9
     15 xta     -2
        aax     #077777
        ati     013             // r11 := from_word  (kernel source)
     15 xta     -1
        aax     #077777
        ati     014             // r12 := to_word    (user dest)
        xta     <nwords>
        ati     012             // r10 := word count
        vtm     02003           // unmapped, interrupts off
co_lp:
     10 vzm     co_dn
     11 xta                     // A := kernel[r11], unmapped
        vtm     02002           // map on
     12 atx                     // user[r12] := A, mapped
        vtm     02003           // map off
     11 utm     1
     12 utm     1
     10 utm     -1
        uj      co_lp
co_dn:
        ita     011
        ati     021
        xta     <retpc>
        ati     015
     15 utm     -2
        xta
     13 uj
co_flt:
        xta     <retpc>
        ati     015
     15 utm     -2
        xta     <mone>
     13 uj

// ----------------------------------------------------------------------------
// int fuword(caddr_t addr) -- fetch one user word.
// ----------------------------------------------------------------------------
// addr is in A (one parameter, nothing pushed).  Returns the word, or -1 on a bad address.
        .globl  fuword
fuword:
        atx     <fpsav>         // stash addr
        ita     015
        atx     <retpc>
        xta     <fpsav>
        aax     #077777         // A := word address
        xts     #1              // push word; A := 1 (count)
        xts                     // push 1;    A := 0 (rw)
     14 vtm     -3
     13 vjm     useracc
        aox
        uza     fw_flt
        xta     <fpsav>
        aax     #077777
        ati     013             // r11 := word
        ita     021
        ati     011             // save PSW
        vtm     02002
     11 xta                     // A := user word, mapped
        vtm     02003
        atx     <wsav>          // stash it (unmapped)
        ita     011
        ati     021             // restore PSW
        xta     <retpc>
        ati     015
        xta     <wsav>          // A := the fetched word
     13 uj
fw_flt:
        xta     <retpc>
        ati     015
        xta     <mone>
     13 uj

// ----------------------------------------------------------------------------
// int fubyte(caddr_t addr) -- fetch one user byte.
// ----------------------------------------------------------------------------
// addr is a fat pointer (doc/Besm6_Data_Representation.md §7): its exponent field is 64 + 8*off,
// so `asx' on it right-shifts the containing word by 8*off, bringing the target byte to bits 8-1.
        .globl  fubyte
fubyte:
        atx     <fpsav>
        ita     015
        atx     <retpc>
        xta     <fpsav>
        aax     #077777
        xts     #1
        xts
     14 vtm     -3
     13 vjm     useracc
        aox
        uza     fb_flt
        xta     <fpsav>
        aax     #077777
        ati     013             // r11 := word
        ita     021
        ati     011
        vtm     02002
     11 xta                     // A := containing word, mapped
        vtm     02003
        asx     <fpsav>         // A >>= 8*off, from the fat pointer's exponent
        aax     #0377           // A := the byte
        atx     <wsav>
        ita     011
        ati     021
        xta     <retpc>
        ati     015
        xta     <wsav>
     13 uj
fb_flt:
        xta     <retpc>
        ati     015
        xta     <mone>
     13 uj

// ----------------------------------------------------------------------------
// int suword(caddr_t addr, int value) -- store one user word.
// ----------------------------------------------------------------------------
// addr is pushed; value is in A.  Returns 0, or -1 on a bad address.
        .globl  suword
suword:
        atx     <vsav>          // stash value
        ita     015
        atx     <retpc>
     15 xta     -1              // A := addr
        aax     #077777
        atx     <fpsav>         // stash word address
        xta     <fpsav>
        xts     #1              // push word; A := 1 (count)
        xts     #1              // push 1;    A := 1 (rw = write)
     14 vtm     -3
     13 vjm     useracc
        aox
        uza     sw_flt
        xta     <fpsav>
        ati     013             // r11 := word
        ita     021
        ati     011
        xta     <vsav>          // A := value
        vtm     02002
     11 atx                     // user[word] := value, mapped
        vtm     02003
        ita     011
        ati     021
        xta     <retpc>
        ati     015
     15 utm     -1              // pop the pushed arg (addr)
        xta                     // A := 0
     13 uj
sw_flt:
        xta     <retpc>
        ati     015
     15 utm     -1
        xta     <mone>
     13 uj

// ----------------------------------------------------------------------------
// int subyte(caddr_t addr, int value) -- store one user byte.
// ----------------------------------------------------------------------------
// addr is a fat pointer; value's low byte is inserted into byte `off' of the containing word by a
// read-modify-write, keeping the other five bytes.  bytemask[off] is that byte's field; the new
// word is  (old ^ (old & mask)) | scatter(value, mask).
        .globl  subyte
subyte:
        atx     <vsav>          // stash value
        ita     015
        atx     <retpc>
     15 xta     -1              // A := addr (fat pointer)
        atx     <fpsav>         // stash the whole fat pointer (need its offset field)
        aax     #077777         // A := word address
        xts     #1              // push word; A := 1 (count)
        xts     #1              // push 1;    A := 1 (rw = write)
     14 vtm     -3
     13 vjm     useracc
        aox
        uza     sb_flt
        xta     <fpsav>
        asn     64+44           // A >>= 44: the offset field (bits 47-45) lands in bits 3-1
        aax     #7
        ati     012             // r10 := off (0..5), the bytemask index
        xta     <fpsav>
        aax     #077777
        ati     013             // r11 := word
        ita     021
        ati     011             // save PSW
        vtm     02002
     11 xta                     // A := old word, mapped
        vtm     02003
        atx     <wsav>          // old word (unmapped from here on)
        utc     bytemask        // C := &bytemask[0]  (no modreg on the utc itself)
     10 xta                     // A := bytemask[off]  (EA = r10 + C)
        atx     <msav>
        xta     <wsav>
        aax     <msav>          // A := old & mask  (the old byte, in place)
        aex     <wsav>          // A := (old & mask) ^ old  (old word, target byte cleared)
        atx     <csav>
        xta     <vsav>
        asn     64-40           // A := value << 40  (low byte -> bits 48-41)
        aux     <msav>          // A := value's byte scattered into the mask's field
        aox     <csav>          // A := cleared | new-byte  = the new word
        vtm     02002
     11 atx                     // user[word] := new word, mapped
        vtm     02003
        ita     011
        ati     021             // restore PSW
        xta     <retpc>
        ati     015
     15 utm     -1
        xta                     // A := 0
     13 uj
sb_flt:
        xta     <retpc>
        ati     015
     15 utm     -1
        xta     <mone>
     13 uj

// ----------------------------------------------------------------------------
// divword -- internal: nwords := (byte count in A) / NBPW, flooring.
// ----------------------------------------------------------------------------
// A floor divide with no divide instruction: subtract NBPW until the remainder reaches zero, or
// would go negative -- detected as bit 15 of the 15-bit remainder becoming set after the `utm'.
// It terminates for ANY input (so a non-word-multiple count, which only the pre-existing broken
// iomove guard can produce, cannot spin); the byte count is assumed to fit 15 bits, which every
// caller's count does (iomove chunks by BSIZE = 3072 bytes; struct/icode copies are smaller).
// Runs unmapped out of r10 (remaining) and r12 (quotient); returns via r13 (the vjm linkage).
divword:
        ati     012             // r10 := byte count (remaining)
        vtm     0, 12           // r12 := 0 (quotient)
dw_lp:
     10 vzm     dw_dn           // remaining == 0 -> exact multiple, done
     10 utm     -NBPW           // remaining -= NBPW
     12 utm     1               // quotient += 1
        ita     012             // A := remaining
        aax     #040000         // bit 15: set iff the subtraction underflowed
        uza     dw_lp           // clear -> still >= 0, keep dividing
     12 utm     -1              // set  -> underflow: drop the partial word
dw_dn:
        ita     014             // A := quotient
        atx     <nwords>
     13 uj

// ----------------------------------------------------------------------------
// Constants and scratch.
// ----------------------------------------------------------------------------
        .data
// The C integer -1: sign bit 41 plus 40 magnitude bits, exponent field zero -- the exact bit
// pattern the compiler's `-1' literal has, so `fubyte(...) == -1' at a caller matches.  A bare
// `.word -1' would be all 48 ones, which is NOT the int -1 and would fail that compare.
mone:   .word   .[1:41]

// bytemask[k] selects byte k of a word: byte 0 = bits 1-8 (the LSB byte), byte 5 = bits 41-48.
bytemask:
        .word   .[1:8]
        .word   .[9:16]
        .word   .[17:24]
        .word   .[25:32]
        .word   .[33:40]
        .word   .[41:48]

        .bss
nbtmp:  . = . + 1               // byte count, across divword
nwords: . = . + 1               // word count, across useracc
retpc:  . = . + 1               // return address, across the useracc call
fpsav:  . = . + 1               // the address argument (fat pointer)
vsav:   . = . + 1               // the value argument (su/subyte)
wsav:   . = . + 1               // the fetched / old word
msav:   . = . + 1               // subyte: the byte mask
csav:   . = . + 1               // subyte: the word with the target byte cleared
