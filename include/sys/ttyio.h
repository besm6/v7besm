// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// The terminal interface -- the structures and numbers, and their one home.
//
// Both sides of the KERNEL gate read this file: a program fills in a `struct sgttyb'
// and names a TIOC* command, and the kernel's ttioccomm() reads the same structure
// out of user space and switches on the same number (kernel/dev/tty.c).  <sgtty.h>
// adds the three prototypes the user side calls through and leaves everything else
// to this file; <sys/tty.h> adds the clist and tty structures, the internal state
// bits and the driver interface.  Nothing here commits a translation unit to either
// side, which is why it can be the one home.
//
// v7 wrote all of this out twice, in <sgtty.h> for the user and in <sys/tty.h> for
// the kernel, and this port inherited both -- sixteen mode flags, thirty ioctl
// commands, and two structures under a second set of member names apiece (`struct
// ttiocb' for sgttyb, `struct tc' for tchars).  They are gone, on the precedent of
// <sys/errno.h> and <sys/signal.h>.  The two copies had drifted in v7 itself, in
// both of the ways that matter here:
//
//   - XTABS was 06000 in <sgtty.h> and 006000 in <sys/tty.h>.  b6cpp rejects a macro
//     redefinition unless the replacement text is character-identical, so a
//     translation unit naming both headers did not compile.  006000 is what stands
//     below, matching TBDELAY beside it.
//   - ('t' << 8) | 16 had two NAMES: TIOCTSTP in <sgtty.h> and TIOCFLUSH in
//     <sys/tty.h>.  Only TIOCFLUSH survives -- it is the one the kernel implements
//     and the one tty(4) documents -- on the precedent set for the signal numbering,
//     where v7's kernel spellings went rather than becoming aliases, so that nothing
//     in this tree has two names for one thing.  Nothing here used TIOCTSTP; whoever
//     ports cmd/stty should know it is not coming back.
//
// A third collision was worse than either, and left no diagnostic to read: <sys/tty.h>
// defined accessor macros t_intrc..t_brkc for the members of its `struct tc', and
// those rewrote the MEMBER DECLARATIONS of <sgtty.h>'s `struct tchars', whose members
// are spelled exactly that.  With one structure there is nothing to accessorize, and
// the macros are gone too.

#ifndef _SYS_TTYIO_H
#define _SYS_TTYIO_H

// Structure for the stty and gtty system calls, and for TIOCSETP/TIOCSETN/TIOCGETP.
struct sgttyb {
    char sg_ispeed; // input speed
    char sg_ospeed; // output speed
    char sg_erase;  // erase character
    char sg_kill;   // kill character
    int sg_flags;   // mode flags
};

// List of special characters, for TIOCSETC/TIOCGETC.
struct tchars {
    char t_intrc;  // interrupt
    char t_quitc;  // quit
    char t_startc; // start output
    char t_stopc;  // stop output
    char t_eofc;   // end-of-file
    char t_brkc;   // input delimiter (like nl)
};

// Modes
#define TANDEM   01
#define CBREAK   02
#define LCASE    04
#define ECHO     010
#define CRMOD    020
#define RAW      040
#define ODDP     0100
#define EVENP    0200
#define ANYP     0300
// The three delay fields are inert: still settable, but ttyoutput() generates no delays
// on an eight-bit line (kernel/dev/tty.c).  TBDELAY is not one -- it is XTABS, and tab
// expansion is live.
#define NLDELAY  001400
#define TBDELAY  006000
#define XTABS    006000
#define CRDELAY  030000
#define VTDELAY  040000
#define BSDELAY  0100000
#define ALLDELAY 0177400

// Delay algorithms
#define CR0  0
#define CR1  010000
#define CR2  020000
#define CR3  030000
#define NL0  0
#define NL1  000400
#define NL2  001000
#define NL3  001400
#define TAB0 0
#define TAB1 002000
#define TAB2 004000
#define FF0  0
#define FF1  040000
#define BS0  0
#define BS1  0100000

// Speeds.  A Consul-254 has no baud rate, so the kernel neither reads nor acts on
// sg_ispeed/sg_ospeed; the names are here because programs still set them, and
// cmd/getty's speed table -- thirteen entries in v7 -- is one entry here.
#define B0    0
#define B50   1
#define B75   2
#define B110  3
#define B134  4
#define B150  5
#define B200  6
#define B300  7
#define B600  8
#define B1200 9
#define B1800 10
#define B2400 11
#define B4800 12
#define B9600 13
#define EXTA  14
#define EXTB  15

// tty ioctl commands.  Of these the kernel answers TIOCGETD, TIOCSETD, TIOCHPCL,
// TIOCEXCL, TIOCNXCL, TIOCGETP, TIOCSETP, TIOCSETN, TIOCFLUSH, TIOCSETC, TIOCGETC and
// the two DIOC*P; the rest belong to hardware and line disciplines this machine has
// none of, and reach ttioccomm()'s default arm.
#define TIOCGETD  (('t' << 8) | 0)
#define TIOCSETD  (('t' << 8) | 1)
#define TIOCHPCL  (('t' << 8) | 2)
#define TIOCMODG  (('t' << 8) | 3)
#define TIOCMODS  (('t' << 8) | 4)
#define TIOCGETP  (('t' << 8) | 8)
#define TIOCSETP  (('t' << 8) | 9)
#define TIOCSETN  (('t' << 8) | 10)
#define TIOCEXCL  (('t' << 8) | 13)
#define TIOCNXCL  (('t' << 8) | 14)
#define TIOHMODE  (('t' << 8) | 15)
#define TIOCFLUSH (('t' << 8) | 16)
#define TIOCSETC  (('t' << 8) | 17)
#define TIOCGETC  (('t' << 8) | 18)
#define DIOCLSTN  (('d' << 8) | 1)
#define DIOCNTRL  (('d' << 8) | 2)
#define DIOCMPX   (('d' << 8) | 3)
#define DIOCNMPX  (('d' << 8) | 4)
#define DIOCSCALL (('d' << 8) | 5)
#define DIOCRCALL (('d' << 8) | 6)
#define DIOCPGRP  (('d' << 8) | 7)
#define DIOCGETP  (('d' << 8) | 8)
#define DIOCSETP  (('d' << 8) | 9)
#define DIOCLOSE  (('d' << 8) | 10)
#define DIOCTIME  (('d' << 8) | 11)
#define DIOCRESET (('d' << 8) | 12)
#define FIOCLEX   (('f' << 8) | 1)
#define FIONCLEX  (('f' << 8) | 2)
#define MXLSTN    (('x' << 8) | 1)
#define MXNBLK    (('x' << 8) | 2)

#endif // _SYS_TTYIO_H
