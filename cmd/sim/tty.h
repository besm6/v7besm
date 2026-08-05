//
// Terminal parameters for the user-level BESM-6 simulator: the guest's `struct sgttyb'
// and `struct tchars' against the host's termios.
//
// WHY THIS IS A FILE OF ITS OWN.  b6sim's stty(2), gtty(2) and the tty half of ioctl(2)
// used to be a lie -- gtty zero-filled, stty returned success and changed nothing, and
// ioctl was an unconditional success whatever was asked of it.  A program that clears
// ECHO went on being echoed and could not tell; worse, a program that asks "am I on a
// terminal?" through ioctl(TIOCGETP) was told yes by a pipe, which is how a pager comes
// to block on a descriptor nothing will ever type at.  cmd/sim/kernel.h's rule is that a
// fiction is worse than an empty field, because a tool cannot tell it from a
// measurement, and this was three fictions.
//
// The translation below is the whole content of the change, so it lives where it can be
// read at once and TESTED WITHOUT A TERMINAL -- test/tty_test.cpp drives these four
// functions directly.  Everything that needs guest memory (the two-word marshalling) or
// a descriptor (tcgetattr/tcsetattr) stays in syscall.cpp.
//
// THE MODES ARE v7's, from include/sys/ttyio.h, and are spelled SG_* here because four
// of the eight -- ECHO above all -- are also <termios.h> macros.  The kernel's own
// meanings are kernel/dev/tty.c's and this file agrees with them, not with 4BSD's.
//
#ifndef B6SIM_TTY_H
#define B6SIM_TTY_H

#include <termios.h>

#include <cstdint>

//
// The guest's `struct sgttyb' (include/sys/ttyio.h) in host form.  ON THE GUEST IT IS
// TWO WORDS, NOT FIVE: a char MEMBER of a struct occupies one byte at a byte-granular
// offset (doc/Besm6_Data_Representation.md section 4), so the four chars fill bytes 0..3
// of word 0, bytes 4 and 5 are the trailing pad, and sg_flags is word 1.  Whole objects
// round up to a word; members do not, and reading the first rule as if it covered the
// second is the mistake this project has now made four times.  The marshalling in
// syscall.cpp is where that layout is written out.
//
struct Sgttyb {
    uint8_t sg_ispeed{ 0 };
    uint8_t sg_ospeed{ 0 };
    uint8_t sg_erase{ 0 };
    uint8_t sg_kill{ 0 };
    unsigned sg_flags{ 0 };
};

//
// ... and `struct tchars', six chars in six bytes: exactly one word, no pad.
//
struct Tchars {
    uint8_t t_intrc{ 0 };
    uint8_t t_quitc{ 0 };
    uint8_t t_startc{ 0 };
    uint8_t t_stopc{ 0 };
    uint8_t t_eofc{ 0 };
    uint8_t t_brkc{ 0 };
};

//
// The mode flags of include/sys/ttyio.h.  Keep in step with that header by hand: it is
// the one home for these on the guest side, and b6sim cannot include it (cmd/sim's
// CMakeLists.txt says why include/ is off this library's search path).
//
enum {
    SG_TANDEM = 01,
    SG_CBREAK = 02,
    SG_LCASE  = 04,
    SG_ECHO   = 010,
    SG_CRMOD  = 020,
    SG_RAW    = 040,
    SG_ODDP   = 0100,
    SG_EVENP  = 0200,
    SG_ANYP   = 0300,
    SG_XTABS  = 006000,
};

// Host termios -> the guest's view of it.
void termios_to_sgttyb(const struct termios &t, Sgttyb &s);

// ... and back.  `t' is MODIFIED IN PLACE, starting from the descriptor's live settings,
// so that everything v7 has no word for -- VMIN/VTIME under canonical input, the control
// characters other than erase and kill, the flow-control details -- survives a
// round trip instead of being flattened to whatever a blank termios happens to hold.
void sgttyb_to_termios(const Sgttyb &s, struct termios &t);

// The six special characters, both ways.  Same in-place rule for the second.
void termios_to_tchars(const struct termios &t, Tchars &c);
void tchars_to_termios(const Tchars &c, struct termios &t);

//
// Remember `fd's current settings, once, and arrange for them to be put back however
// b6sim exits.  Every path that changes a descriptor calls this first; see tty.cpp for
// which exits are covered and how.
//
void tty_remember(int fd);

// Put every remembered descriptor back.  Idempotent; safe from a signal handler.
void tty_restore_all();

#endif // B6SIM_TTY_H
