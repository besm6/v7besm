/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

/*
 * tunable variables
 */

#define NBUF     10             /* size of buffer cache (min 10) */
#define NINODE   24             /* number of in core inodes (min 24) */
#define NFILE    50             /* number of in core file structures */
#define NMOUNT   2              /* number of mountable file systems */
#define MAXMEM   (NPAGE * PGSZ) /* max core per process, in words */
#define MAXUPRC  25             /* max processes per user */
#define SSIZE    PGSZ           /* initial stack size (words) */
#define SINCR    PGSZ           /* increment of stack (words) */
#define NOFILE   20             /* max open files per process */
#define CANBSIZ  256            /* max size of typewriter line */
#define CMAPSIZ  50             /* size of core allocation area */
#define SMAPSIZ  50             /* size of swap allocation area */
#define NCALL    20             /* max simultaneous time callouts */
#define NPROC    150            /* max number of processes */
#define NTEXT    40             /* max number of pure texts */
#define NCLIST   100            /* max total clist size */
#define HZ       250            /* Ticks/second: the interval timer free-runs at this
                                   rate (ГРП bit 40; SIMH CLK_TPS) and cannot be
                                   programmed.  Was 60 in the x86 port. */
#define TIMEZONE (5 * 60)       /* Minutes westward from Greenwich */
#define DSTFLAG  1              /* Daylight Saving Time applies in this locality */
#define MSGBUFS  128            /* Characters saved from error messages */
#define NCARGS   5120           /* # characters in exec arglist */

/*
 * priorities
 * probably should not be
 * altered too much
 */

#define PSWP   0
#define PINOD  10
#define PRIBIO 20
#define PZERO  25
#define NZERO  20
#define PPIPE  26
#define PWAIT  30
#define PSLEP  40
#define PUSER  50

/*
 * signals
 * dont change
 */

#define NSIG 17

#define SIGHUP  1  /* hangup */
#define SIGINT  2  /* interrupt (rubout) */
#define SIGQUIT 3  /* quit (FS) */
#define SIGINS  4  /* illegal instruction */
#define SIGTRC  5  /* trace or breakpoint */
#define SIGIOT  6  /* iot */
#define SIGEMT  7  /* emt */
#define SIGFPT  8  /* floating exception */
#define SIGKIL  9  /* kill, uncatchable termination */
#define SIGBUS  10 /* bus error */
#define SIGSEG  11 /* segmentation violation */
#define SIGSYS  12 /* bad system call */
#define SIGPIPE 13 /* end of pipe */
#define SIGCLK  14 /* alarm clock */
#define SIGTRM  15 /* Catchable termination */

/*
 * fundamental constants of the implementation--
 * cannot be changed easily
 */

#define NBPW    sizeof(int)               /* number of bytes in an integer */
#define BSIZE   3072                      /* size of secondary block (bytes, 6144 for besm) */
#define BSIZEW  512                       /* size of secondary block, in words (BSIZE / NBPW) */
#define NINDIR  (BSIZE / sizeof(daddr_t)) /* number of indirect blocks */
#define BMASK   0777                      /* BSIZE-1 */
#define BSHIFT  9                         /* LOG2(BSIZE) */
#define NMASK   0177                      /* NINDIR-1 */
#define NSHIFT  7                         /* LOG2(NINDIR) */
#define USIZE   1024                      /* size of the u-area, in words (one page) */
#define NULL    0                         /* zero pointer */
#define CMASK   0                         /* default mask for file creation */
#define NODEV   (dev_t)(-1)               /* no device */
#define ROOTINO ((ino_t)2)                /* i number of all roots */
#define SUPERB  ((daddr_t)1)              /* block number of the super block */
#define DIRSIZ  24                        /* max characters per directory (4 words) */
#define NICINOD 100                       /* number of superblock inodes */
#define NICFREE 50                        /* number of superblock free blocks */
#define INFSIZE 138                       /* size of per-proc info for users */
#define CBSIZE  28                        /* number of chars in a clist block */
#define CROUND  037                       /* clist rounding: sizeof(int *) + CBSIZE - 1*/

/*
 * Some macros for units conversion
 */
/* bytes to words (six chars pack into one 48-bit word) */
#define btow(x) (((unsigned)(x) + 5) / 6)

/* words to bytes */
#define wtob(x) ((x) * 6)

/* round a word count up to a whole page */
#define pground(x) (((unsigned)(x) + PGSZ - 1) & ~(PGSZ - 1))

/* words to disk blocks (a block is BSIZE == 512 words) */
#define wtodb(x) ((x) >> 9)

/* inumber to disk address */
#define itod(x) (daddr_t)((((unsigned)(x) + 15) >> 3))

/* inumber to disk offset */
#define itoo(x) (int)(((x) + 15) & 07)

/* major part of a device */
#define major(x) (int)(((unsigned)(x) >> 8))

/* minor part of a device */
#define minor(x) (int)((x) & 0377)

/* make a device number */
#define makedev(x, y) (dev_t)((x) << 8 | (y))

typedef struct {
    int r[1];
} *physadr;
typedef long daddr_t;
typedef char *caddr_t;
typedef unsigned short ino_t;
typedef long time_t;
typedef int label_t[10]; /* r1-r7, r13, r15 */
typedef short dev_t;
typedef long off_t;

/*
 * Machine-dependent bits and macros
 */
#define PGSH 10   /* LOG2(PGSZ) */
#define PGSZ 1024 /* words per page */

#define NPAGE    32     /* virtual pages per process */
#define UBASE    076000 /* the u-area: the last page of the kernel space */
#define KEND     076000 /* the kernel image must end below this */
#define USTKPAGE 28     /* first page of the user stack (070000) */
