// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// A clist structure is the head
// of a linked list queue of characters.
// The characters are stored in blocks containing a link word and CBSIZE chars.
// The routines getc and putc
// manipulate these structures.

#ifndef _SYS_TTY_H
#define _SYS_TTY_H

#include <sys/ttyio.h> // struct sgttyb, struct tchars, the modes and the TIOC* commands
#include <sys/types.h> // dev_t, caddr_t -- included, not assumed; see sys/dir.h

// What this header holds is the kernel's half: the character queues, the tty structure
// the drivers hang off cdevsw[], its internal state, and the driver interface.  The half
// a program can see -- the mode flags, the ioctl command numbers and the two structures
// they name -- was written out a second time here until <sys/ttyio.h> became its one
// home, on the precedent of <sys/errno.h>.  The head comment there is the account,
// including the two places v7's own two copies had drifted.  Both this header and
// <sgtty.h> may now appear in one translation unit, and lib/test/headers.c does that.

// The block itself is private to kernel/prim.c: nothing else has any business
// knowing how the characters are laid out inside one.
struct cblock;

// v7 carried two `char *' cursors here and found the enclosing block, and the
// block boundary, by masking their byte addresses.  That cannot be done on this
// machine -- a `char *' is a fat pointer whose byte offset lives in bits 47-45,
// not in the low bits of an address -- so the cursors are a block pointer plus an
// index into it.  See the header of kernel/prim.c.
struct clist {
    int c_cc;            // character count
    struct cblock *c_hd; // block holding the next char out; NULL when empty
    struct cblock *c_tl; // block holding the next free slot; NULL when empty
    int c_hix;           // index in c_hd of the next char out
    int c_tix;           // index in c_tl of the next free slot
};

// A tty structure is needed for
// each UNIX character device that
// is used for normal terminal IO.
// The routines in tty.c handle the
// common code associated with
// these structures.
// The definition and device dependent
// code is in each driver. (kl.c dc.c dh.c)

struct tty {
    struct clist t_rawq;           // input chars right off device
    struct clist t_canq;           // input chars after erase and kill
    struct clist t_outq;           // output list to device
    void (*t_oproc)(struct tty *); // routine to start output
    void (*t_iproc)(struct tty *); // routine to start input
    int t_addr;                    // device/line number -- NOT an address: this machine has no
                                   // device address space, devices are named by 033 register
                                   // numbers.  See doc/Besm6_Peripherals.md.
    dev_t t_dev;                   // device number
    int t_flags;                   // mode, settable by ioctl call
    int t_state;                   // internal state, not visible externally
    int t_pgrp;                    // process group name
    char t_delct;                  // number of delimiters in raw q
    char t_line;                   // line discipline
    char t_col;                    // printing column of device
    char t_erase;                  // erase character
    char t_kill;                   // kill character
    char t_char;                   // character temporary
    char t_ispeed;                 // input speed
    char t_ospeed;                 // output speed
    union {
        struct tchars t_tc; // v7 spelled this `struct tc', a second copy of tchars
        struct clist t_ctlq;
    } t_un;
};

#ifdef KERNEL
// The special characters of the current tty, in the driver's own `tp'.  It names the
// union MEMBER rather than the union, which is what lets the six t_intrc..t_brkc
// accessor macros v7 had here go: with one structure there is nothing left to rename,
// and those macros would have rewritten struct tchars' own member declarations.  Kernel
// side only -- it is a lowercase macro naming a caller's local variable.
#define tun tp->t_un.t_tc
#endif

#define TTIPRI 28
#define TTOPRI 29

// Default special characters.  NOT v7's: v7 erased with `#' and killed with `@', both of
// them typeable text, and interrupted with DEL, which is what a keyboard's own erase key
// sends.  These are what every terminal since has meant by the four.
#define CERASE 0177 // Erase a character: ctl-? (DEL)
#define CEOT   004
#define CKILL  025  // Kill the line: ctl-u
#define CQUIT  034  // Quit: ctl-\ (FS)
#define CINTR  003  // Interrupt: ctl-c
#define CSTOP  023  // Stop output: ctl-s
#define CSTART 021  // Start output: ctl-q
#define CBRK   0377

// limits
#define TTHIWAT 100
#define TTLOWAT 50
#define TTYHOG  256

// Hardware bits
#define DONE    0200
#define IENABLE 0100

// Internal state bits
#define TIMEOUT 01      // Delay timeout in progress -- nothing sets it now (no delays);
                        // kept for v7's numbering and for pstat(8).
#define WOPEN   02      // Waiting for open to complete
#define ISOPEN  04      // Device is open
#define FLUSH   010     // outq has been flushed during DMA
#define CARR_ON 020     // Software copy of carrier-present
#define BUSY    040     // Output in progress
#define ASLEEP  0100    // Wakeup when output done
#define XCLUDE  0200    // exclusive-use flag against open
#define TTSTOP  0400    // Output stopped by ctl-s
#define HUPCLS  01000   // Hang up upon last close
#define TBLOCK  02000   // tandem queue blocked
#define DKCMD   04000   // datakit command channel
#define DKMPX   010000  // datakit user-multiplexed mode
#define DKCALL  020000  // datakit dial mode
#define DKLINGR 040000  // datakit lingering close mode
#define CNTLQ   0100000 // interpret t_un as clist

#ifdef KERNEL
int ttread(struct tty *tp);
void ttwrite(struct tty *tp);
void ttstart(struct tty *tp);
int ttioccomm(int com, struct tty *tp, caddr_t addr, dev_t dev);
void flushtty(struct tty *tp);
void ttyinput(int c, struct tty *tp);
void ttyopen(dev_t, struct tty *);
void ttyclose(struct tty *tp);
void ttychars(struct tty *tp);
void nulldstop(struct tty *);
void nulltclose(struct tty *);
void nulltioctl(int, struct tty *, caddr_t);
int q_to_b(struct clist *q, char *cp, int cc);
int b_to_q(char *cp, int cc, struct clist *q);
int getc(struct clist *p);
int putc(int c, struct clist *p);
#endif

#endif // _SYS_TTY_H
