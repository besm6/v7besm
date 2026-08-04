/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// dmesg -- print the kernel's message ring.
//
//      dmesg
//
// Task C8's second, and the cheapest of the five: two kctl(2) calls and one kgetsym(3), no
// device, no privilege, no argument.
//
// THE RING IS REAL AND ALWAYS HAS BEEN.  ../TODO.md used to say this program was waiting on
// a kernel that kept one.  It was looking in kernel/prf.c; the ring is in kernel/dev/sc.c,
// where putchar() writes each character of every kernel printf into msgbuf[MSGBUFS] and
// advances msgbufp, wrapping at the end with no flag and no sentinel.  '\r' is filtered out
// on the way in and NUL is dropped, so a NUL in the buffer is a slot that has never been
// written -- which is what makes the "skip the zeros" loop below correct rather than lucky.
//
// msgbufp POINTS AT THE OLDEST CHARACTER, being the next slot to be written, so the ring
// reads forward from there and wraps once.
//
// TWO ARITHMETICS, AND THE SECOND IS THE ONE TO GET RIGHT.
//
//   msgbufp is a char *, so KCTL_GET hands back a FAT pointer: bits 15-1 the word address,
//   bits 47-45 a byte offset stored as a right-shift distance, 5 meaning the word's first
//   byte (doc/Besm6_Data_Representation.md SS7).  ptrword() alone gives the word and would
//   land the cursor up to five characters early; ptrbyte() is the other half and
//   <sys/param.h> has both.  Hence
//
//        i = (ptrword(mp) - base) * NBPW + (5 - ptrbyte(mp))
//
//   with `base' the word address of msgbuf itself, which is what kgetsym(3) returns.  This
//   is the only place in the tree that needs both halves of a fat pointer at once; the
//   kernel's own kctl.c wants ptrword() and no more, since every other exported variable is
//   word-aligned by construction.
//
// TWO THINGS OF v7's ARE DELIBERATELY GONE, and they go together:
//
//   THE /usr/adm/msgbuf HISTORY.  v7 kept a copy of the ring in that file and printed only
//   what had appeared since the last run, walking the two rings in lockstep and printing
//   `...' where they diverged.  The file is not on this image (../../root.manifest), the
//   directory is not either, and the machinery is three quarters of the program.  Its `-'
//   flag went with it.
//
//   pdate(), the once-only date line.  It existed to timestamp that incremental report and
//   has nothing to timestamp without it.  Dropping it also makes the output of this program
//   exactly the contents of the ring and nothing else, which is what lets the b6sim half of
//   its test be a checked-in literal (test/CMakeLists.txt).
//
// NOT SETUID, and it needs nothing: kctl(2) is unprivileged (<sys/kctl.h>).  That is the
// whole reason this one lives in /bin where v7 put it in /etc -- v7's read /dev/kmem.
//
#include <stdio.h>
#include <stdlib.h>

#include <sys/kctl.h>
#include <sys/param.h>

static char ring[MSGBUFS];

int main(void)
{
    int base, mp, i, n;

    base = kgetsym("msgbuf");
    if (base < 0) {
        fputs("dmesg: this kernel exports no msgbuf\n", stderr);
        return 1;
    }
    if (kctl("msgbuf", KCTL_GET, ring, sizeof ring) != (int)sizeof ring) {
        fputs("dmesg: cannot read msgbuf\n", stderr);
        return 1;
    }
    if (kctl("msgbufp", KCTL_GET, &mp, sizeof mp) != (int)sizeof mp) {
        fputs("dmesg: cannot read msgbufp\n", stderr);
        return 1;
    }

    // v7 called this "Namelist mismatch" and meant a /unix that did not match the running
    // kernel.  That cannot happen here -- every address in the table is a link-time
    // relocation of the real declaration -- so a cursor outside the ring means the table is
    // wrong, which is worth saying rather than walking off the end of the copy.
    i = (ptrword(mp) - base) * NBPW + (5 - ptrbyte(mp));
    if (i < 0 || i >= MSGBUFS) {
        fputs("dmesg: msgbufp does not point into msgbuf\n", stderr);
        return 1;
    }

    // Once round the ring from the oldest character.  A NUL is a slot never written.
    for (n = 0; n < MSGBUFS; n++) {
        if (ring[i] != '\0')
            putchar(ring[i]);
        if (++i == MSGBUFS)
            i = 0;
    }
    return 0;
}
