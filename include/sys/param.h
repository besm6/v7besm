// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// This header is #define-only, so that the assembly sources can #include it too
// and stop hand-copying its constants -- see sys/besm6dev.h and sys/syscall.h, which
// are kept the same way.  Nothing here may expand to C text: no typedef, no
// declaration, and no `sizeof' (b6as has no such operator).  The scalar typedefs that
// used to sit at the end of this file are in sys/types.h.
//
// So this file INCLUDES NOTHING, and needs nothing.  Every other header in this
// directory includes what it uses (sys/dir.h says why); this is the one that stands
// alone by emitting no C text at all, which makes it order-insensitive by
// construction.  Five macro bodies below do name typedefs -- dev_t in NODEV and
// makedev(), ino_t in ROOTINO, daddr_t in SUPERB and itod(), chan_t in CHANOF() --
// but a macro body is only text until it is expanded, and every expansion site is
// inside a function body, where sys/types.h is in scope.
//
// Do not bracket the file in `#ifndef __ASSEMBLER__' to let it grow a typedef: b6cpp
// predefines no such macro and b6cc passes none for a .S, so the bracket would be
// permanently false.  Adding one means changing the compiler driver.
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

// tunable variables

// NBUF AND NMOUNT ARE TIED TOGETHER, so retune them together.  Every mounted
// filesystem holds ONE buffer permanently -- smount() takes it with geteblk() and only
// sumount() gives it back, and iinit() does the same for the root -- so NMOUNT of the
// NBUF buffers can be out of the cache at once and the rest is all the machine has for
// I/O.  geteblk()/getblk() sleep on an empty free list, so undersizing this pair does
// not run slowly, it stops.  16 and 8 leave eight buffers with every slot in use, which
// is what 10 and 2 left.  Raising NBUF lowers the kernel's ceiling: see BUFBASE below.
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
// Ticks/second: the interval timer free-runs at this rate (ГРП bit 40; SIMH CLK_TPS)
// and cannot be programmed.
#define HZ       250
//
// The zone ftime() reports, in minutes westward from Greenwich, and whether daylight
// saving applies here.  v7 shipped 5*60 and 1 -- US Eastern -- and this machine answers
// ZERO to both, deliberately.
//
// There is no clock-calendar a program can read on a BESM-6, so `time' is never seeded
// from a wall clock: it starts at the superblock's s_time and counts ticks from there
// (kernel/README.md, "Known consequences").  A five-hour offset on top of an invented
// epoch is a fiction laid over a fiction, and it would put localtime() an hour and a
// half of arithmetic away from gmtime() for no gain.  Zero also makes the kernel agree
// with b6sim, whose ftime() answers 0/0 (cmd/sim/syscall.cpp), so lib/test/timet.c gives
// the same transcript under both harnesses -- which is the whole point of running it
// under both (kernel/test/libtest, task 25c).
//
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

// The signal numbers, NSIG included, used to be written out here as well as in
// <signal.h>.  They are in <sys/signal.h> now, their one home, which both sides
// include -- see the comment there.

// fundamental constants of the implementation--
// cannot be changed easily

#define NBPW    6            // number of bytes in an integer (sizeof(int))
#define BSIZE   3072         // size of secondary block, in bytes (BSIZEW * NBPW)
#define BSIZEW  512          // size of secondary block, in words
#define NINDIR  512          // daddr_t per indirect block (BSIZEW / 1)
#define NMASK   0777         // NINDIR-1
#define NSHIFT  9            // LOG2(NINDIR)
#define USIZE   1024         // size of the SAVED u-area, in words (one page); see UBASE.
                             //   A context switch copies at most this much and usually about
                             //   half of it -- as far as r15 has reached (kernel/uarea.S)
#define CMASK   0            // default mask for file creation
#define NODEV   (dev_t)(-1)  // no device
#define ROOTINO ((ino_t)2)   // i number of all roots
#define SUPERB  ((daddr_t)1) // block number of the super block

// NULL was in the block above and is out of it now, guarded, exactly as
// <stdio.h> guards its own: the compiler's <stddef.h> spells it ((void *)0), and
// a source that includes both would otherwise draw a redefinition error from
// b6cpp.  Whichever is seen first wins; the two agree on every use.  The kernel
// sees only this one, having no <stddef.h> anywhere.
#ifndef NULL
#define NULL 0 // zero pointer
#endif

// The superblock's two caches, sys/filsys.h.  v7's 100 and 50 were sized for a
// 512-BYTE block and were never retuned when a block became 512 WORDS: the struct
// came to 165 words and wasted 68% of the block it sits in.  Filling that block is
// nearly free -- the superblock lives in a geteblk() buffer held for the life of
// the mount, and a buffer is BSIZEW words whether the struct uses them or not.
//
// The split is 2:1 toward free BLOCKS because that is the hot path: every write
// allocates blocks, only creat() allocates inodes.  On a 2000-block drive it takes
// the free-list chain from 40 blocks deep to 7.
//
// NICFREE IS BOUNDED ON TWO SIDES, and both are asserted rather than trusted:
//   - struct fblk is 1 + NICFREE words and must fit a block (sys/fblk.h).
//   - struct filsys must total exactly BSIZEW words (sys/filsys.h).
// alloc() and free() wcopy() between s_free[] and df_free[] sizing the copy from
// the filsys side, so the two NICFREEs are the same constant by necessity.
#define NICINOD 160 // number of superblock inodes
#define NICFREE 320 // number of superblock free blocks

// Superblock magic.  v7 has none, so a garbage block mounts silently and the first
// symptom is getfs()'s "bad count" -- which "repairs" it by zeroing the counts.
// Deliberate and unmistakable in an octal dump; 39 bits, so it fits the 40-bit
// value field with room to spare.  See sbcheck() in kernel/alloc.c.
#define FS_MAGIC 0123456701234

// There is no BSHIFT/BMASK, and there cannot be: a block is BSIZE == 3072 BYTES,
// and 3072 is not a power of two.  v7's pair described a 512-byte block and were
// carried into this port unchanged, so every byte-offset -> block conversion in
// the kernel was silently wrong by a factor of six.  A byte offset now divides and
// takes a remainder by BSIZE explicitly -- once per block crossing, which is noise
// beside the bread() it guards.
//
// The pair below is the WORD-domain one, which is what 9/0777 accidentally already
// spelled: a block is 512 words, and that IS a power of two.  Use it for anything
// counting words; anything counting bytes divides.
#define BWSHIFT 9    // LOG2(BSIZEW): word offset -> block number
#define BWMASK  0777 // BSIZEW-1: word offset -> offset within block

// THE UNIT A BLOCK COUNT IS REPORTED IN, which is not the unit it is stored in.
// df(1M), du(1), quot(1M) and ls(1) all count filesystem blocks internally and print
// KBPB of them per block, so that what they say means something without knowing
// BSIZE -- v7's commands printed filesystem blocks too, but v7's block was 512 bytes
// and this one is six times that.  kernel/main.c and machdep.c report the same way.
//
// KBYTE IS BYTES, and that has to be said out loud: PGSZ and USIZE are also 1024 and
// are 1024 WORDS, which is 6144 bytes and two filesystem blocks.  The three are not
// the same quantity and nothing may alias them.
//
// 1024 has no word-domain spelling here -- it is 170 2/3 words -- so unlike BSIZE
// there is no shift or mask to go with it, and unlike BSIZE it never names anything
// on the disk.  It is a presentation unit and appears only at a printf; cmd/README.md
// SS4 is the rule for a program that reports one.
//
// KBPB is derived rather than written as 3, so that retuning BSIZE cannot leave four
// programs quietly lying.  Each of them asserts BSIZE % KBYTE == 0; the assertion
// cannot live here, this header being #define-only (see its head).
#define KBYTE 1024            // bytes in a REPORTED block
#define KBPB  (BSIZE / KBYTE) // reported blocks per filesystem block: 3

// The on-disk inode, sys/ino.h.  Sixteen words -- eight of metadata, then eight
// disk addresses -- so INOPB of them tile a 512-word block exactly, with no
// padding and no straddling.  The 8/8 split is deliberate: `dp + 8' is the address
// array, at a power-of-two offset a hand-written mkfs can rely on.
//
// NADDR is here rather than in sys/inode.h (where v7 kept it) so that the on-disk
// struct and the in-core one cannot disagree about how many addresses there are.
//
// NLEVEL is 2, not v7's 3, because the third level is UNREACHABLE on this machine:
// one EC-5052 drive is 2000 blocks, and at NINDIR == 512 the single indirect block
// already spans 518 blocks while the double spans 262662 -- the whole volume, 130
// times over.  Dropping it is what frees the slots to make the inode 16 words.
#define NADDR    8   // disk addresses in an inode: 6 direct, 1 indirect, 1 double
#define NLEVEL   2   // levels of indirection
#define INOPB    32  // inodes per block: BSIZEW / 16
#define INOSHIFT 5   // LOG2(INOPB)
#define INOMASK  037 // INOPB-1

// The directory entry, sys/dir.h: one word of i-number and three of name, so
// DIRPB of them tile a block exactly and the offset arithmetic keeps the shifts
// v7 had.  DIRSIZ was 24 here (4 words, 5 to an entry), which divides 512 no
// better than it divides anything else.
//
// CHANGING DIRSIZ MOVES u_upt.  struct user holds both u_dbuf[DIRSIZ] and a
// struct direct (u_dent) AHEAD of the shadow page table, and uarea.S and seg.S
// hardcode that table's word offset as UPT -- b6as has no offsetof().  Going from
// 24 to 18 took a word off each, so UPT went 35 -> 33 in kernel/uarea.S,
// kernel/seg.S and kernel/test/mmutest.c.  mmutest's check 13 asserts it, which is
// how this was caught; keep all four in step.
#define DIRSIZ   18   // max characters per directory name (3 words)
#define DIRWORDS 4    // words in a struct direct
#define DIRENTSZ 24   // bytes in a struct direct (DIRWORDS * NBPW)
#define DIRPB    128  // directory entries per block: BSIZEW / DIRWORDS
#define DIRSHIFT 7    // LOG2(DIRPB)
#define DIRMASK  0177 // DIRPB-1
#define INFSIZE  138 // size of per-proc info for users
// Chars in a clist block: 30 is five words exactly, chars packing six to a word, so a
// `struct cblock' is the link word plus five and nothing is wasted.  v7's companion
// CROUND is GONE -- it existed so that a clist cursor could find its own block, and its
// enclosing block's base, by masking the low bits of a BYTE address.  A `char *' here is
// a fat pointer (marker in bit 48, byte offset in bits 47-45), so a mask of its integer
// value names bits of the WORD address and says nothing about the byte within it.
// kernel/prim.c keeps indices instead; the header there is the full account.
#define CBSIZE   30

// Some macros for units conversion
// Note the absence of `unsigned' below.  On this machine an unsigned add,
// subtract, multiply, divide or ordering test is a CALL -- b$uadd, b$udiv,
// b$ult and friends -- because the additive unit reads bits 48-42 as an
// exponent and a full 48-bit value carries data there.  The signed spellings
// are single inline instructions.  See doc/Besm6_Runtime_Library.md.  Every
// quantity below is a count or a 15-bit address, so it fits the 41-bit signed
// range with room to spare, and `int' is both cheaper and sufficient.
// Each casts its argument to int first, and that cast is load-bearing: the
// commonest argument is a sizeof, which is unsigned, and an unsigned argument
// would drag the whole expression -- and every b$uadd/b$udiv it implies -- back
// in.  See btow(sizeof ...) in alloc.c, nami.c and main.c.
// bytes to words (six chars pack into one 48-bit word)
#define btow(x) (((int)(x) + 5) / 6)

// words to bytes
#define wtob(x) ((int)(x) * 6)

// round a word count up to a whole page
#define pground(x) (((int)(x) + PGSZ - 1) & ~(PGSZ - 1))

// words to disk blocks (a block is BSIZE == 512 words)
#define wtodb(x) ((x) >> 9)

// Taking a char * apart.  A char or void pointer is a FAT POINTER, one word: bit 48 is the
// marker, bits 47-45 the byte offset as a right-shift distance (5 = byte #0, the word's
// first, down to 0 = byte #5), bits 15-1 the word address.  See doc/Besm6_Data_Representation.md
// section 7.  The word field being only 15 bits is why a caddr_t cannot name physical
// memory above 32767 -- hence struct buf's b_paddr.
//
// `aax #077777' is the same idiom in assembly; see usermem.s (fubyte).
//
// Both take the pointer apart through `unsigned' -- they have to, because the
// fields they want live above bit 41 -- but both hand back an `int', so that
// the arithmetic done on the result afterwards stays inline.
#define ptrword(p) (int)((unsigned)(p) & 077777)     // bits 15-1: the word address
#define ptrbyte(p) (int)(((unsigned)(p) >> 44) & 07) // bits 47-45: 5 = the word's first byte

// Derive the n'th distinct sleep channel from an object.  This exists because
// pipe.c and fio.c want two or three separate channels per inode, and used to
// spell them `(caddr_t)ip + 1' and `(caddr_t)ip + 2' -- BYTE arithmetic on a
// fat pointer, which walks the byte-offset field (5 -> 4 -> 3) and leaves the
// word address alone.  The three channels were therefore distinguished purely
// by bits 47-45, which no longer survive the thin chan_t.  Offsetting by whole
// words gives three genuinely different addresses inside the same object.
#define CHANOF(p, n) ((chan_t)((int *)(p) + (n)))

// Inumber to disk address, and to the offset within that block.  The `2*INOPB - 1'
// bias is v7's, and it places inode 1 at block 2 offset 0 -- the i-list starts
// after the boot block and the superblock.  Both are written in terms of INOPB so
// that resizing the inode cannot leave them behind, as the hardcoded >>3 and &07
// were left behind when the struct stopped being 64 bytes.
// inumber to disk address
#define itod(x) (daddr_t)(((x) + 2 * INOPB - 1) >> INOSHIFT)

// inumber to disk offset
#define itoo(x) (int)(((x) + 2 * INOPB - 1) & INOMASK)

// Major part of a device.  This used to shift through `unsigned', which made
// major(NODEV) come out as (2^48-1)>>8 rather than -1.  That accidentally
// armed every `major(dev) >= n' bounds test against a negative dev, so the
// three tests that matter -- bio.c, sys3.c, fio.c -- now reject a negative
// major explicitly.  Do not put the cast back without revisiting them.
#define major(x) (int)((x) >> 8)

// minor part of a device
#define minor(x) (int)((x) & 0377)

// make a device number
#define makedev(x, y) (dev_t)((x) << 8 | (y))

// Machine-dependent bits and macros
#define PGSH 10   // LOG2(PGSZ)
#define PGSZ 1024 // words per page

#define NPAGE    32 // virtual pages per process

// The u-area occupies the last TWO pages of the kernel space, 074000-077777, but only
// the FIRST of them is per-process state: `struct user' at the bottom (~142 words) and
// the kernel stack growing UP through it.  USIZE words from UBASE are what every process
// image reserves at p_addr and the CEILING on what uflush()/uload() copy on a context
// switch; what they actually copy is everything below r15, since the stack grows up and
// the words above it are frames that have returned (task 30, kernel/uarea.S).
//
// The page above the saved one, 076000-077777, is OVERFLOW: the stack may grow into it
// and code running there is perfectly correct, but the words are in no process image
// and are NOT copied by a context switch.  That is the whole of task 25a.  Before it,
// the u-area was one page at 076000 and the stack had ~880 words below the 0100000
// ceiling -- where a 15-bit r15 wraps to 0 and the kernel writes its frames over the
// interrupt vectors.  /bin/sh as process 1 peaked 109 words short of that, so one
// nested interrupt frame killed the boot.  Moving UBASE down a page leaves the saved
// stack the same size and puts 1024 words of room above it, which is exactly what an
// interrupt frame needs: an interrupt handler never sleeps, so it always unwinds out of
// the overflow page before the process can leave the CPU.
//
// THE HAZARD, stated once: a process that reaches sleep() or swtch() with r15 above
// 076000 loses those frames, because uflush() copies at most USIZE words -- it clamps
// there -- and another process then runs deep on the same physical page.  Task 30 left
// that threshold exactly where it was.  There is no fault and no
// diagnostic.  It takes a path 884+ words deep AT A SLEEP POINT to do it -- 109 words
// deeper than anything measured -- and kernel/TODO.md task 31 says how it would be
// detected.  See kernel/uarea.S and include/sys/user.h.
#define UBASE    074000 // the u-area: the last two pages of the kernel space
#define USTKPAGE 28     // first page of the user stack (070000)

// The top of the unmapped reach: one past the last word an unmapped access can
// name -- and, not by coincidence, the reach of a C pointer's 15-bit word field.  The
// kernel space ends exactly here, at the top of the u-area's overflow page (machdep.c
// asserts that geometry), so this is derived rather than written down twice.  Physical
// memory above it exists and is where every process image lives; reaching it takes a
// mapped window (kernel/seg.S).
#define KREACH 0100000

// BESM-6 a.out magic numbers and image base -- must match cross/besm6/b.out.h
// (the kernel build cannot reach cross/, so these are a documented duplicate).
// getxfile() (kernel/sys1.c) reads the 8-word header into u.u_exdata, whose
// sizeof is HDRSZ == BADDR words.
#define FMAGIC 02044252323200407U // impure: one writable region from word BADDR
#define NMAGIC 02044252323200410U // pure:   read-only const+text, page-aligned data
#define BADDR  8                  // HDRSZ/W: image begins at word 8 (header hole 0..7)

// The buffer cache, and the ceiling it puts on the kernel image.
//
// buffers[NBUF][BSIZE] is not bss: like the u-area it is a fixed physical area, carved
// out of the top of the kernel's unmapped space just under UBASE -- so it moved down with
// UBASE in task 25a, and the kernel image lost a page of ceiling with it.  kernel/besm6.S names
// it (`buffers = u - NBUF*BSIZEW'); main.c declares it `extern'.  It lives outside bss
// because the drum and disk controllers transfer whole zones to a physical address, so
// the buffers must sit at an address the kernel can name without translation.
//
// KEND is therefore BUFBASE, not UBASE: const + text + data + bss must all end below it.
// `make' prints `b6size -w unix' so the total can be checked against this line.  Both are
// derived, so raising NBUF moves the ceiling down and cannot silently disagree with it.
//
// AND IT HAS BEEN RAISED, from 10 to 16, to pay for NMOUNT going from 2 to 8: the ceiling
// went 062000 -> 054000 with it, 3072 words, plus 78 more of bss for the six extra buf
// headers in conf.c.  What is left under KEND is about 1800 words, and that is now the
// tightest thing in this file -- the next NBUF, not the next page of code, is what will
// run the image into the buffers.
#define BUFBASE (UBASE - NBUF * BSIZEW) // base of buffers[][]: 054000 at NBUF == 16
#define KEND    BUFBASE                 // the kernel image must end below this

#endif // _SYS_PARAM_H
