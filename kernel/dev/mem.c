// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// 	Memory special file
// 	minor device 0 is physical memory
// 	minor device 1 is kernel memory
// 	minor device 2 is EOF/RATHOLE
//
// A BYTE OFFSET, like every other file.  The offset names a byte of physical (minor 0) or
// kernel (minor 1) memory: word `u_offset / NBPW', byte `u_offset % NBPW' within it.  So
// /dev/mem is phymem * 6 bytes long and /dev/kmem is KREACH * 6, reading kernel word W
// means lseek(fd, W * 6L, 0), and a partial-word read is expressible.  One b$div per chunk
// buys that, which is nothing beside the copy it guards.
//
// TWO PATHS, AND THE ADDRESS PICKS ONE.  The kernel runs unmapped, so a kernel address IS a
// physical address -- but an unmapped access reaches only the low 32 Kwords (KREACH), and a
// caddr_t's word field is 15 bits besides:
//
//   * below KREACH the word can simply be named.  `(caddr_t)(int *)pa' is the conversion
//     that builds the fat pointer -- int -> int * is a bit copy of the word address, int *
//     -> char * sets the marker and byte offset 5, the word's first byte
//     (doc/Besm6_Data_Representation.md section 7) -- and iomove() does the rest.  This is
//     the whole of /dev/kmem, the live u-area at 074000 included.
//
//   * at or above it -- where every process image lives, and where /dev/mem earns its
//     keep -- the words are moved through copyphys() (kernel/seg.S), the mapped window
//     copyseg() opens, into the bounce buffer below.  A write pre-reads the same words
//     first, so the bytes it was not asked to change survive; there are no sub-word stores
//     on this machine, so a partial word is a read-modify-write however it is spelled.
//
// Word 0 is a black hole either way: a load from address 0 yields 0 and a store to it is
// dropped, before translation and before the "already physical" tag (kernel/seg.S says
// where).  It holds an interrupt vector, so nothing is lost but the illusion of reading it.

#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/dir.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/types.h"
#include "sys/user.h"

// Words moved per bracket on the windowed path.  Each costs two БРЗ drains and two РП
// writes, so this trades kernel bss against brackets per block: 64 words is eight of them
// per 512-word block read.
#define MEMBCNT 64

// The bounce buffer.  Static, not a kernel-stack array: the stack above `struct user' is
// 884 words and this path has no claim on 64 of them.  Not reentrant, and it does not need
// to be -- nothing between the two copyphys() calls sleeps, and no interrupt handler reads
// a memory device.
static int membuf[MEMBCNT];

static void mmrw(int dev, int flag)
{
    // All int, deliberately: an unsigned compare, subtract or add on this machine is a call
    // (b$ult, b$usub, ...), and every quantity here is a word address or a count, well
    // inside the 41 bits an int has.  See the note at the units macros in <sys/param.h>.
    int pa, limit, end;
    int o, n, w;

    switch (minor(dev)) {
    case 0:
        limit = phymem; // /dev/mem: all of core, in words
        break;
    case 1:
        limit = KREACH; // /dev/kmem: what an unmapped access can name
        break;
    case 2: // /dev/null: reads EOF, writes vanish
        if (flag == B_WRITE)
            u.u_count = 0;
        return;
    default:
        u.u_error = ENXIO;
        return;
    }

    while (u.u_error == 0 && u.u_count != 0) {
        pa = u.u_offset / NBPW;
        o  = u.u_offset % NBPW;
        if (pa >= limit) {
            // Off the end: a read is at EOF and stops short, a write has nowhere to put
            // what it was given and says so, as v7's own memory driver does.
            if (flag == B_WRITE)
                u.u_error = ENXIO;
            return;
        }
        if (pa < KREACH) {
            // Direct.  One iomove() for as much as is both asked for and addressable; for
            // /dev/mem that stops at KREACH and the windowed path below takes the rest.
            end = limit < KREACH ? limit : KREACH;
            n   = min(u.u_count, (end - pa) * NBPW - o);
            iomove((caddr_t)(int *)pa + o, n, flag);
            continue; // iomove() advanced u_base, u_offset and u_count
        }
        // Windowed.  Chop against the buffer and against BOTH page boundaries: copyphys()
        // opens one window per side, so neither run may cross a page.
        w = min(MEMBCNT, limit - pa);
        w = min(w, PGSZ - (pa & (PGSZ - 1)));
        w = min(w, PGSZ - (ptrword(membuf) & (PGSZ - 1)));
        n = min(u.u_count, w * NBPW - o);
        // ...and then down to the words the bytes actually touch.  On a write that is what
        // bounds the read-modify-write: the words a partial edge lands in, and no others.
        w = btow(o + n);
        // Read; for a write this is the pre-read that preserves the untouched bytes.
        copyphys(pa, ptrword(membuf), w);
        iomove((caddr_t)membuf + o, n, flag);
        if (flag == B_WRITE && u.u_error == 0)
            copyphys(ptrword(membuf), pa, w);
    }
}

void mmread(int dev)
{
    mmrw(dev, B_READ);
}

void mmwrite(int dev)
{
    mmrw(dev, B_WRITE);
}
