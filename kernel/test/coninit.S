// coninit.S -- the program that stands in for /etc/init while task 23 is being brought
// up: it opens the console, echoes typed lines back, and signs off through /dev/tty.
//
// This is the ONLY way the console can be exercised as a CONTROLLING TERMINAL.  sctest
// next door drives the same typewriter, but from a standalone image with no kernel
// underneath -- it reaches the `ext' instructions and nothing above them.  What has never
// run is scopen() through cdevsw[0], ttyopen()'s u.u_ttyp/u.u_ttyd acquisition, the
// interrupt path scintr() -> ttyinput() -> canon(), ttwrite()'s output queue, and the
// /dev/tty indirection in kernel/dev/sys.c.  All of that needs a USER PROCESS asking for
// it, and the only user process this kernel has at boot is the one the icode execs.
//
// So the root image carries this as /etc/init (kernel/test/root.manifest), the icode's
// `$77 SYS_exec' finds it, and test/console.ini types at it.  Task 24 replaces it with a
// real v7-shaped init and this file goes away.
//
// IT LINKS NOTHING -- no crt0, no libc.  Not for economy: libc under the real kernel is
// task 25's shakeout, and task 23 must not fail for its reasons.  Everything here is the
// bare $77 gate, so a failure is the console path's or nobody's.
//
// THE CALLING CONVENTION IS THE GATE'S (doc/Besm6_Calling_Conventions.md, kernel/syscall.c):
// arguments 1..n-1 are pushed below r15 with `xts', the LAST one is left in the
// accumulator, `$77 N' takes the trap, and the result comes back in the accumulator with
// errno in r14 -- zero on success, which is what `14 v1m' tests.  The gate pops the n-1
// pushed words itself, so nothing here adjusts r15.
//
// exece() has already seeded r15 above the argument block at 070000 and setregs() left
// every other register zero -- including R, the AU mode word -- so `ntr 7' is the first
// instruction, exactly as lib/libc/csu/crt0.s does it for a C program.
//
// Run: cd kernel && make test  (test/console.ini), or `make run' and type at it.

#include <sys/syscall.h>

O_RDWR = 2
BUFSZ  = 64                     // bytes read per line; canon() delivers at most one line

        .text
        .globl  _start

// ---------------------------------------------------------------------------------
// Open the console.  It must come back as fd 0: process 1 inherits proc[0]'s empty
// u_ofile[], so this is the first descriptor anything has ever opened, and everything
// below names fd 0 outright rather than carrying it in a cell.  A failed open is -1,
// which is also non-zero, so the one test covers both.
// ---------------------------------------------------------------------------------
_start:
        ntr     7               // R := RREG_C -- ω = logical plus the NTR 3 suppression
        xta     conname         // arg 1: char * "/dev/console"
        xts     #O_RDWR         // arg 2 (the last, in A); pushes arg 1
        $77     SYS_open
        u1a     nocons          // fd != 0: no console, and no way to say so

        xta     #0              // write(0, banner, BANLEN)
        xts     banner
        xts     #BANLEN
        $77     SYS_write

// ---------------------------------------------------------------------------------
// The echo loop.  Every line comes back through canon(), so erase (#), kill (@) and
// the ^D that ends the loop are the KERNEL's work, not this program's -- which is the
// point of the exercise.  Reading 0 bytes is end of file: a line consisting of just
// t_eofc leaves canon() with nothing to hand over.
// ---------------------------------------------------------------------------------
loop:
        xta     #0              // write(0, prompt, PRMLEN)
        xts     prompt
        xts     #PRMLEN
        $77     SYS_write

        xta     #0              // read(0, buf, BUFSZ)
        xts     bufp
        xts     #BUFSZ
        $77     SYS_read
        atx     nread           // ATX keeps A, so the tests below still see the count
     14 v1m     rderr           // errno non-zero: the read failed
        uza     eof             // zero bytes: ^D

        xta     #0              // write(0, buf, nread) -- echo the canonical line
        xts     bufp
        xts     nread
        $77     SYS_write
        uj      loop

// ---------------------------------------------------------------------------------
// End of input.  Sign off through /dev/tty rather than fd 0: the message can only come
// out if syopen()/sywrite() (kernel/dev/sys.c) resolved u.u_ttyd to this same terminal,
// so its arrival on the console IS the /dev/tty proof.
// ---------------------------------------------------------------------------------
eof:
        xta     ttyname         // open("/dev/tty", O_RDWR)
        xts     #O_RDWR
        $77     SYS_open
        atx     ttyfd
     14 v1m     notty

        xta     ttyfd           // write(ttyfd, okmsg, OKLEN)
        xts     okmsg
        xts     #OKLEN
        $77     SYS_write

        xta     #0              // exit(0) -- does not return
        $77     SYS_exit

// ---------------------------------------------------------------------------------
// Failure tails.  Each says which one it was and then parks, so a broken run stops
// somewhere `ex PC' can name (coninit.nm) instead of wandering off.  `nocons' has no
// console to speak through and can only park.
// ---------------------------------------------------------------------------------
nocons: uj      nocons

rderr:  xta     #0
        xts     errmsg
        xts     #ERRLEN
        $77     SYS_write
        uj      spin

notty:  xta     #0
        xts     ttymsg
        xts     #TTYLEN
        $77     SYS_write

spin:   uj      spin

// ---------------------------------------------------------------------------------
// The pointers are FAT -- marker in bit 48, byte offset 5 in bits 47-45, naming byte #0
// of the word -- because that is what namei() walks with fubyte() and what iomove()
// steps through with the compiler's own b$pinc.  The parentheses are load-bearing: `|'
// binds LOOSER than `+', so without them the relocatable term reaches `|' and b6as says
// "too complex expression".  Same idiom as `iname' in kernel/besm6.S.
//
// EACH STRING'S LENGTH IS SPELLED OUT beside it, because b6as rejects `label - label'
// (there is no PC-relative addressing and no way to subtract two relocatable terms).
// Change a string and change its length in the same edit.
//
// The two PATH strings carry an explicit \0.  `.ascii' zero-pads to a word boundary, so a
// string whose length is already a multiple of six gets no terminator at all -- and
// "/dev/console" is exactly twelve characters.  namei() would then walk off the end.
// ---------------------------------------------------------------------------------
        .data
conname: .word  (.48 | .47 | .45) + constr
ttyname: .word  (.48 | .47 | .45) + ttystr
bufp:    .word  (.48 | .47 | .45) + buf
banner:  .word  (.48 | .47 | .45) + banstr
prompt:  .word  (.48 | .47 | .45) + prmstr
okmsg:   .word  (.48 | .47 | .45) + okstr
errmsg:  .word  (.48 | .47 | .45) + errstr
ttymsg:  .word  (.48 | .47 | .45) + ttystr2

nread:   .word  0              // bytes the last read() returned
ttyfd:   .word  0              // the /dev/tty descriptor

constr:  .ascii "/dev/console\0"
ttystr:  .ascii "/dev/tty\0"

banstr:  .ascii "console init\n"
BANLEN = 13
prmstr:  .ascii "line> "
PRMLEN = 6
okstr:   .ascii "console ok\n"
OKLEN  = 11
errstr:  .ascii "read failed\n"
ERRLEN = 12
ttystr2: .ascii "no /dev/tty\n"
TTYLEN = 12

// BUFSZ bytes, six to a word: 11 words hold 66.  exec() clears the whole image before
// reading the data segment in, so this starts zeroed.
        .bss
buf:     . = . + 11
