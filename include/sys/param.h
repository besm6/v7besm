// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// This header is #define-only, so the assembly sources can #include it too: nothing
// here may expand to C text -- no typedef, no declaration, no `sizeof'.  It includes
// nothing and needs nothing.  Five macro bodies do name typedefs (dev_t, ino_t,
// daddr_t, chan_t), but a body is only text until expanded, and every expansion site
// is inside a function, where sys/types.h is in scope.  Do not bracket the file in
// `#ifndef __ASSEMBLER__': b6cpp predefines no such macro, so the bracket would be
// permanently false.
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

// tunable variables

// NBUF and NMOUNT are tied together, so retune them together: every mounted filesystem
// holds one buffer permanently (smount()/iinit() take it, sumount() gives it back), and
// geteblk()/getblk() sleep on an empty free list rather than run slowly.  Raising NBUF
// lowers the kernel's ceiling -- see BUFBASE below.
#define NBUF    16             // size of buffer cache (min 10, and > NMOUNT)
#define NINODE  24             // number of in core inodes (min 24)
#define NFILE   50             // number of in core file structures
#define NMOUNT  8              // number of mountable file systems (one is the root's)
#define MAXMEM  (NPAGE * PGSZ) // max core per process, in words
#define MAXUPRC 25             // max processes per user
#define SSIZE   PGSZ           // initial stack size (words)
#define SINCR   PGSZ           // increment of stack (words)
#define NOFILE  20             // max open files per process
#define CANBSIZ 256            // max size of typewriter line
#define CMAPSIZ 50             // size of core allocation area
#define SMAPSIZ 50             // size of swap allocation area
#define NCALL   20             // max simultaneous time callouts
#define NPROC   150            // max number of processes
#define NTEXT   40             // max number of pure texts
#define NCLIST  100            // max total clist size

// Not a tunable -- the machine has two -- but it dimensions a table kctl(2) exports
// (kernel/kctl.c, <sys/kctl.h>), so a program walking sc[] needs it.
#define NSC     2              // number of Consul typewriters

// The instrumented block devices and their slots in dk_busy, dk_numb[], dk_wds[]
// (<sys/systm.h>).  A slot index is also a bit number in dk_busy, and kernel/clock.c
// bills ticks to dk_time[(dk_busy & 07) + state*8], so the busy set must fit three bits;
// kernel/main.c asserts it.  One slot per controller, not per drive.
#define NDK     2              // instrumented block devices
#define DK_MD   0              // the disks -- kernel/dev/md.c, all MDNUNIT units together
#define DK_MB   1              // the drums -- kernel/dev/mb.c, the paging store
#define NDKTIME 32             // slots in dk_time[]: 4 CPU states x 8 I/O states

// Ticks/second: the interval timer free-runs at this rate (ГРП bit 40; SIMH CLK_TPS)
// and cannot be programmed.
#define HZ       250

// Both are zero deliberately.  There is no clock-calendar on a BESM-6, so `time' starts
// at the superblock's s_time rather than a wall clock, and zero also matches b6sim's
// ftime() (cmd/sim/syscall.cpp) so lib/test/timet.c reads the same under both harnesses.
#define TIMEZONE 0        // Minutes westward from Greenwich
#define DSTFLAG  0        // Daylight Saving Time applies in this locality
#define MSGBUFS  128      // Characters saved from error messages
#define NCARGS   5120     // # characters in exec arglist

// priorities
// probably should not be
// altered too much

#define PSWP   0
#define PINOD  10
#define PRIBIO 20
#define PZERO  25
#define NZERO  20
#define PPIPE  26
#define PWAIT  30
#define PSLEP  40
#define PUSER  50

// The signal numbers, NSIG included, live in <sys/signal.h>, which both sides include.

// fundamental constants of the implementation--
// cannot be changed easily

#define NBPW    6            // number of bytes in an integer (sizeof(int))
#define BSIZE   3072         // size of secondary block, in bytes (BSIZEW * NBPW)
#define BSIZEW  512          // size of secondary block, in words
#define NINDIR  512          // daddr_t per indirect block (BSIZEW / 1)
#define NMASK   0777         // NINDIR-1
#define NSHIFT  9            // LOG2(NINDIR)
#define USIZE   1024         // size of the SAVED u-area, in words (one page); see UBASE.
                             //   A context switch copies at most this much, as far as
                             //   r15 has reached (kernel/uarea.S)
#define CMASK   0            // default mask for file creation
#define NODEV   (dev_t)(-1)  // no device
#define ROOTINO ((ino_t)2)   // i number of all roots

// The superblock is the first block of the volume: this port has no boot block, SIMH
// loading the a.out image.  Uncast, so cmd/fsutil/params.cpp can assert the number
// without daddr_t in scope.
#define SUPERB 0 // block number of the super block

// Guarded exactly as <stdio.h> guards its own: the compiler's <stddef.h> spells NULL
// ((void *)0), and b6cpp would draw a redefinition error.  Whichever is seen first wins.
#ifndef NULL
#define NULL 0 // zero pointer
#endif

// The superblock's two caches (sys/filsys.h), sized to fill the block and split 2:1
// toward free blocks, the hot path.  NICFREE is bounded on two sides, both asserted:
// struct fblk is 1 + NICFREE words and must fit a block (sys/fblk.h), and struct filsys
// must total exactly BSIZEW words (sys/filsys.h).
#define NICINOD 160 // number of superblock inodes
#define NICFREE 320 // number of superblock free blocks

// Superblock magic.
#define FS_MAGIC 0xBE50006F11E5U

// There is no BSHIFT/BMASK, and cannot be: BSIZE == 3072 bytes is not a power of two,
// so a byte offset divides and takes a remainder by BSIZE explicitly.  The pair below is
// the word-domain one -- 512 words IS a power of two.  Use it for anything counting words.
#define BWSHIFT 9    // LOG2(BSIZEW): word offset -> block number
#define BWMASK  0777 // BSIZEW-1: word offset -> offset within block

// The unit a block count is reported in, which is not the unit it is stored in: df(1M),
// du(1), quot(1M), ls(1), kernel/main.c and machdep.c all print KBPB of these per
// filesystem block.  KBYTE is BYTES -- PGSZ and USIZE are also 1024 but are WORDS, and
// nothing may alias them.  KBPB is derived so that retuning BSIZE cannot leave four
// programs quietly lying; each asserts BSIZE % KBYTE == 0, which this header cannot.
// cmd/README.md §4 is the rule for a program that reports one.
#define KBYTE 1024            // bytes in a REPORTED block
#define KBPB  (BSIZE / KBYTE) // reported blocks per filesystem block: 3

// The on-disk inode (sys/ino.h) is sixteen words -- eight of metadata, then eight disk
// addresses -- so INOPB of them tile a block exactly, and `dp + 8' is the address array.
// NADDR is here rather than in sys/inode.h so the on-disk and in-core structs cannot
// disagree.  NLEVEL is 2, not v7's 3: at NINDIR == 512 the single indirect already spans
// 518 blocks and a drive is 2000, so the third level is unreachable.
#define NADDR    8   // disk addresses in an inode: 6 direct, 1 indirect, 1 double
#define NLEVEL   2   // levels of indirection
#define INOPB    32  // inodes per block: BSIZEW / 16
#define INOSHIFT 5   // LOG2(INOPB)
#define INOMASK  037 // INOPB-1

// The directory entry (sys/dir.h): one word of i-number and three of name, so DIRPB of
// them tile a block exactly.
//
// CHANGING DIRSIZ MOVES u_upt.  struct user holds u_dbuf[DIRSIZ] and a struct direct
// ahead of the shadow page table, whose word offset UPT is hardcoded (b6as has no
// offsetof()) in kernel/uarea.S, kernel/seg.S and kernel/test/mmutest.c, where check 13
// asserts it.  A fourth copy is asserted by nothing: cmd/sim/kernel.h's klayout, whose
// U_COMM and U_WORDS this also shifts.  Keep all four in step.
#define DIRSIZ   18   // max characters per directory name (3 words)
#define DIRWORDS 4    // words in a struct direct
#define DIRENTSZ 24   // bytes in a struct direct (DIRWORDS * NBPW)
#define DIRPB    128  // directory entries per block: BSIZEW / DIRWORDS
#define DIRSHIFT 7    // LOG2(DIRPB)
#define DIRMASK  0177 // DIRPB-1

// Chars in a clist block: 30 is five words exactly, so a `struct cblock' is the link word
// plus five.  v7's CROUND is gone -- a `char *' here is a fat pointer, so masking its
// integer value names bits of the WORD address; kernel/prim.c keeps indices instead.
#define CBSIZE   30

// Some macros for units conversion.  Note the absence of `unsigned': an unsigned add,
// compare or divide is an out-of-line call on this machine (doc/Besm6_Runtime_Library.md),
// and every quantity below fits the 41-bit signed range.  Each casts its argument to int
// first, and that cast is load-bearing -- the commonest argument is a sizeof, which is
// unsigned and would drag the whole expression back out of line.
// bytes to words (six chars pack into one 48-bit word)
#define btow(x) (((int)(x) + 5) / 6)

// words to bytes
#define wtob(x) ((int)(x) * 6)

// round a word count up to a whole page
#define pground(x) (((int)(x) + PGSZ - 1) & ~(PGSZ - 1))

// words to disk blocks (a block is BSIZE == 512 words)
#define wtodb(x) ((x) >> 9)

// Taking a char * apart.  A char or void pointer is a fat pointer, one word: bit 48 is
// the marker, bits 47-45 the byte offset as a right-shift distance (5 = the word's first
// byte, 0 = its last), bits 15-1 the word address -- doc/Besm6_Data_Representation.md
// section 7.  The 15-bit word field is why a caddr_t cannot name physical memory above
// 32767; hence struct buf's b_paddr.  `aax #077777' is the same idiom in assembly, see
// usermem.s (fubyte).  Both go through `unsigned' -- the fields live above bit 41 -- and
// both hand back an `int', so the arithmetic afterwards stays inline.
#define ptrword(p) (int)((unsigned)(p) & 077777)     // bits 15-1: the word address
#define ptrbyte(p) (int)(((unsigned)(p) >> 44) & 07) // bits 47-45: 5 = the word's first byte

// Derive the n'th distinct sleep channel from an object: pipe.c and fio.c want two or
// three per inode, and spelled them `(caddr_t)ip + 1' -- byte arithmetic on a fat
// pointer, which walks only bits 47-45 and does not survive the thin chan_t.
#define CHANOF(p, n) ((chan_t)((int *)(p) + (n)))

// Inumber to disk address, and to the offset within that block.  The `INOPB - 1' bias
// places inode 1 at block 1 offset 0 -- the i-list starts just past the superblock; v7's
// extra block was its boot block, see SUPERB.  Both are written in terms of INOPB so that
// resizing the inode cannot leave them behind.  Inode 1 is unallocatable (ialloc() refuses
// anything below ROOTINO), so slot 0 of the i-list is dead space -- v7's arrangement, kept.
// inumber to disk address
#define itod(x) (daddr_t)(((x) + INOPB - 1) >> INOSHIFT)

// inumber to disk offset
#define itoo(x) (int)(((x) + INOPB - 1) & INOMASK)

// Major part of a device.  Not shifted through `unsigned', which would make major(NODEV)
// come out as (2^48-1)>>8 rather than -1 and arm every `major(dev) >= n' bounds test
// against a negative dev.  bio.c, sys3.c and fio.c reject a negative major explicitly;
// do not put the cast back without revisiting them.
#define major(x) (int)((x) >> 8)

// minor part of a device
#define minor(x) (int)((x) & 0377)

// make a device number
#define makedev(x, y) (dev_t)((x) << 8 | (y))

// Machine-dependent bits and macros
#define PGSH 10   // LOG2(PGSZ)
#define PGSZ 1024 // words per page

#define NPAGE    32 // virtual pages per process

// The u-area occupies the last two pages of the kernel space, 074000-077777, but only the
// first is per-process state: `struct user' at the bottom and the kernel stack growing up
// through it.  USIZE words from UBASE are what a process image reserves at p_addr and the
// ceiling on what uflush()/uload() copy; the page above is overflow, usable by a running
// stack but in no process image and never copied.  The hazard, stated once: a process that
// reaches sleep() or swtch() with r15 above 076000 loses those frames, with no fault and no
// diagnostic.  It takes a path 884+ words deep at a sleep point, 109 deeper than anything
// measured.  See kernel/uarea.S and include/sys/user.h.
#define UBASE    074000 // the u-area: the last two pages of the kernel space
#define USTKPAGE 28     // first page of the user stack (070000)

// One past the last word an unmapped access can name -- and the reach of a C pointer's
// 15-bit word field.  Derived: the kernel space ends at the top of the u-area's overflow
// page, and machdep.c asserts that geometry.  Physical memory above it exists, holds every
// process image, and takes a mapped window to reach (kernel/seg.S).
#define KREACH 0100000

// BESM-6 a.out magic numbers and image base -- must match cross/besm6/b.out.h (the kernel
// build cannot reach cross/, so these are a documented duplicate).  The two spellings must
// stay byte-for-byte identical: a native toolchain program can include both headers in one
// translation unit, and b6cpp rejects a redefinition whose text differs at all.
#define FMAGIC 02044252323200407U // impure: one writable region from word BADDR
#define NMAGIC 02044252323200410U // pure:   read-only const+text, page-aligned data
#define BADDR  8                  // HDRSZ/W: image begins at word 8 (header hole 0..7)

// The buffer cache, and the ceiling it puts on the kernel image.  buffers[NBUF][BSIZE] is
// not bss: like the u-area it is a fixed physical area just under UBASE, because the drum
// and disk controllers transfer whole zones to a physical address.  kernel/besm6.S names it
// (`buffers = u - NBUF*BSIZEW'); main.c declares it `extern'.
//
// KEND is therefore BUFBASE, not UBASE: const + text + data + bss must all end below it,
// and `make' prints `b6size -w unix' so the total can be checked.  Both are derived, so
// raising NBUF moves the ceiling down and cannot silently disagree with it -- at NBUF 16
// about 940 words are left, which is the tightest thing in this file.
#define BUFBASE (UBASE - NBUF * BSIZEW) // base of buffers[][]: 054000 at NBUF == 16
#define KEND    BUFBASE                 // the kernel image must end below this

#endif // _SYS_PARAM_H
