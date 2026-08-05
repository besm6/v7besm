//
// The sgttyb/termios translation of cmd/sim/tty.cpp.
//
// WHY THESE FOUR FUNCTIONS ARE PURE, AND TESTED HERE RATHER THAN THROUGH THE GUEST.
// Everything else about b6sim's terminal gates needs a terminal: tcgetattr fails on a
// pipe, so a b6sim case run under scripts/run-prog-test.sh can only ever observe the
// ENOTTY path, and a pty-based test would be a test of openpty's portability as much as
// of this code.  The translation itself needs nothing -- a struct termios in, a struct
// sgttyb out -- so it is where the content of the change is, and it is checkable to the
// bit.  cmd/sim/tty.h says why the change was made at all.
//
// WHAT IS DELIBERATELY NOT ASSERTED: the exact host bit patterns.  ICANON and OXTABS are
// different numbers on macOS and Linux and TAB3 does not exist on the first, so every
// case below is written as `set this v7 flag, translate, and see it come back' or as a
// round trip.  A test that named a host constant would be testing <termios.h>.
//
#include "tty.h"

#include <gtest/gtest.h>

#include <cstring>

//
// A canonical-mode terminal to start from, built the way a host would hand one over:
// everything this file does not name stays zero, so a bit that appears in the output
// came from the translation and not from the fixture.
//
static struct termios cooked()
{
    struct termios t;
    std::memset(&t, 0, sizeof(t));
    t.c_iflag      = ICRNL;
    t.c_oflag      = OPOST | ONLCR;
    t.c_cflag      = CS8 | CREAD;
    t.c_lflag      = ICANON | ISIG | ECHO;
    t.c_cc[VERASE] = 010;
    t.c_cc[VKILL]  = 025;
    cfsetispeed(&t, B9600);
    cfsetospeed(&t, B9600);
    return t;
}

// The flags a v7 program would see for that terminal.
static unsigned flags_of(const struct termios &t)
{
    Sgttyb s;
    termios_to_sgttyb(t, s);
    return s.sg_flags;
}

// Set `f' on a cooked terminal and read the flags back.
static unsigned roundtrip(unsigned f)
{
    struct termios t = cooked();
    Sgttyb s;
    termios_to_sgttyb(t, s);
    s.sg_flags = f;
    sgttyb_to_termios(s, t);
    return flags_of(t);
}

// ---------------------------------------------------------------------------
// Reading the terminal
// ---------------------------------------------------------------------------

TEST(Tty, CookedTerminalIsEchoCrmodAndNeitherRawNorCbreak)
{
    unsigned f = flags_of(cooked());
    EXPECT_TRUE(f & SG_ECHO);
    EXPECT_TRUE(f & SG_CRMOD);
    EXPECT_FALSE(f & SG_RAW);
    EXPECT_FALSE(f & SG_CBREAK);
    EXPECT_FALSE(f & SG_XTABS);
    EXPECT_FALSE(f & SG_TANDEM);
}

TEST(Tty, EchoOffIsSeen)
{
    struct termios t = cooked();
    t.c_lflag &= ~(unsigned)ECHO;
    EXPECT_FALSE(flags_of(t) & SG_ECHO);
}

//
// ISIG IS WHAT TELLS RAW FROM CBREAK, on both sides of the gate: kernel/dev/tty.c takes
// the signal characters as data under RAW and as signals under CBREAK, and that is the
// only thing distinguishing two modes that are both `not canonical'.
//
TEST(Tty, NotCanonicalWithSignalsIsCbreak)
{
    struct termios t = cooked();
    t.c_lflag &= ~(unsigned)ICANON;
    unsigned f = flags_of(t);
    EXPECT_TRUE(f & SG_CBREAK);
    EXPECT_FALSE(f & SG_RAW);
}

TEST(Tty, NotCanonicalWithoutSignalsIsRaw)
{
    struct termios t = cooked();
    t.c_lflag &= ~(unsigned)(ICANON | ISIG);
    unsigned f = flags_of(t);
    EXPECT_TRUE(f & SG_RAW);
    EXPECT_FALSE(f & SG_CBREAK);
}

TEST(Tty, EraseAndKillComeFromTheControlCharacters)
{
    struct termios t = cooked();
    t.c_cc[VERASE]   = 0177;
    t.c_cc[VKILL]    = 030;
    Sgttyb s;
    termios_to_sgttyb(t, s);
    EXPECT_EQ(0177u, s.sg_erase);
    EXPECT_EQ(030u, s.sg_kill);
}

TEST(Tty, NoParityGenerationIsAnyp)
{
    // Every line here is eight bits with no parity, which v7 spells ANYP -- both bits.
    EXPECT_EQ((unsigned)SG_ANYP, flags_of(cooked()) & SG_ANYP);
}

TEST(Tty, ParityIsReportedWhenTheHostGeneratesIt)
{
    struct termios t = cooked();
    t.c_cflag |= PARENB;
    EXPECT_EQ((unsigned)SG_EVENP, flags_of(t) & SG_ANYP);
    t.c_cflag |= PARODD;
    EXPECT_EQ((unsigned)SG_ODDP, flags_of(t) & SG_ANYP);
}

TEST(Tty, TandemIsInputFlowControl)
{
    struct termios t = cooked();
    t.c_iflag |= IXOFF;
    EXPECT_TRUE(flags_of(t) & SG_TANDEM);
}

// ---------------------------------------------------------------------------
// Speeds
// ---------------------------------------------------------------------------

TEST(Tty, SpeedsAreV7IndicesNotBaudRates)
{
    struct termios t = cooked();
    cfsetispeed(&t, B300);
    cfsetospeed(&t, B1200);
    Sgttyb s;
    termios_to_sgttyb(t, s);
    EXPECT_EQ(7u, s.sg_ispeed); // B300 is index 7 in include/sys/ttyio.h
    EXPECT_EQ(9u, s.sg_ospeed); // B1200 is index 9
}

TEST(Tty, SpeedSurvivesARoundTrip)
{
    struct termios t = cooked();
    cfsetispeed(&t, B300);
    cfsetospeed(&t, B300);
    Sgttyb s;
    termios_to_sgttyb(t, s);
    struct termios back = cooked();
    sgttyb_to_termios(s, back);
    EXPECT_EQ((speed_t)B300, cfgetispeed(&back));
    EXPECT_EQ((speed_t)B300, cfgetospeed(&back));
}

//
// EXTA and EXTB have no host spelling and nothing above B9600 has a v7 one, so the ends
// of the table saturate rather than wrapping into the two extension slots -- which would
// make a speed comparison answer nonsense instead of `as fast as I can say'.
//
TEST(Tty, SpeedsOutsideTheTableSaturateAtB9600)
{
    Sgttyb s;
    s.sg_ispeed      = 15; // EXTB
    s.sg_ospeed      = 15;
    struct termios t = cooked();
    sgttyb_to_termios(s, t);
    EXPECT_EQ((speed_t)B9600, cfgetospeed(&t));
}

// ---------------------------------------------------------------------------
// Writing the terminal
// ---------------------------------------------------------------------------

TEST(Tty, EachModeSurvivesARoundTrip)
{
    EXPECT_TRUE(roundtrip(SG_ECHO | SG_ANYP) & SG_ECHO);
    EXPECT_TRUE(roundtrip(SG_CRMOD | SG_ANYP) & SG_CRMOD);
    EXPECT_TRUE(roundtrip(SG_CBREAK | SG_ANYP) & SG_CBREAK);
    EXPECT_TRUE(roundtrip(SG_RAW) & SG_RAW);
    EXPECT_TRUE(roundtrip(SG_TANDEM | SG_ANYP) & SG_TANDEM);
}

TEST(Tty, ClearingAModeClearsIt)
{
    EXPECT_FALSE(roundtrip(SG_ANYP) & SG_ECHO);
    EXPECT_FALSE(roundtrip(SG_ANYP) & SG_CRMOD);
    EXPECT_FALSE(roundtrip(SG_ANYP) & SG_CBREAK);
    EXPECT_FALSE(roundtrip(SG_ANYP) & SG_RAW);
    EXPECT_FALSE(roundtrip(SG_ANYP) & SG_TANDEM);
}

//
// THE MODE novi(1) AND more(1) BOTH ASK FOR: RAW, no ECHO, no CRMOD.  cmd/novi's
// term_open() is `flags &= ~(ECHO|CRMOD); flags |= RAW', and until this translation
// existed b6sim answered it with success and did nothing at all, which is why
// cmd/novi/README.md records having to raw the pty from outside.
//
TEST(Tty, TheEditorsMode)
{
    struct termios t = cooked();
    Sgttyb s;
    termios_to_sgttyb(t, s);
    s.sg_flags &= ~(unsigned)(SG_ECHO | SG_CRMOD);
    s.sg_flags |= SG_RAW;
    sgttyb_to_termios(s, t);

    EXPECT_FALSE(t.c_lflag & ICANON);
    EXPECT_FALSE(t.c_lflag & ISIG);
    EXPECT_FALSE(t.c_lflag & ECHO);
    EXPECT_FALSE(t.c_iflag & ICRNL);
    EXPECT_EQ(1, t.c_cc[VMIN]);
    EXPECT_EQ(0, t.c_cc[VTIME]);

    unsigned f = flags_of(t);
    EXPECT_TRUE(f & SG_RAW);
    EXPECT_FALSE(f & SG_ECHO);
    EXPECT_FALSE(f & SG_CRMOD);
}

//
// RAW OUTRANKS CRMOD AND XTABS ON THE OUTPUT SIDE.  kernel/dev/tty.c's ttyoutput()
// short-circuits into the queue before the tab expansion when the line is RAW, so a RAW
// line does no output processing whatever -- which is what lets novi place its own \r\n.
//
TEST(Tty, RawTurnsOutputProcessingOff)
{
    struct termios t = cooked();
    Sgttyb s;
    termios_to_sgttyb(t, s);
    s.sg_flags = SG_RAW | SG_CRMOD | SG_XTABS;
    sgttyb_to_termios(s, t);
    EXPECT_FALSE(t.c_oflag & OPOST);
}

TEST(Tty, RawForcesEightBitsAndNoParity)
{
    struct termios t = cooked();
    t.c_cflag |= PARENB | PARODD;
    Sgttyb s;
    termios_to_sgttyb(t, s);
    s.sg_flags = SG_RAW;
    sgttyb_to_termios(s, t);
    EXPECT_FALSE(t.c_cflag & PARENB);
    EXPECT_EQ((unsigned)CS8, t.c_cflag & CSIZE);
}

//
// getpass(3)'s whole method: read the flags, clear ECHO, write them back, and put the
// saved word back afterwards.  What must survive is everything getpass never named.
//
TEST(Tty, ClearingEchoLeavesTheRestAlone)
{
    struct termios t = cooked();
    t.c_cc[VINTR]    = 003;
    t.c_cc[VMIN]     = 4; // a value v7 has no word for at all

    Sgttyb saved;
    termios_to_sgttyb(t, saved);
    Sgttyb s = saved;
    s.sg_flags &= ~(unsigned)SG_ECHO;
    sgttyb_to_termios(s, t);

    EXPECT_FALSE(t.c_lflag & ECHO);
    EXPECT_TRUE(t.c_lflag & ICANON);
    EXPECT_EQ(003, t.c_cc[VINTR]);
    EXPECT_EQ(4, t.c_cc[VMIN]); // untouched: canonical input does not use it

    sgttyb_to_termios(saved, t);
    EXPECT_TRUE(t.c_lflag & ECHO);
}

TEST(Tty, EraseAndKillAreWrittenThrough)
{
    struct termios t = cooked();
    Sgttyb s;
    termios_to_sgttyb(t, s);
    s.sg_erase = 0177;
    s.sg_kill  = 030;
    sgttyb_to_termios(s, t);
    EXPECT_EQ(0177, t.c_cc[VERASE]);
    EXPECT_EQ(030, t.c_cc[VKILL]);
}

// ---------------------------------------------------------------------------
// The six special characters
// ---------------------------------------------------------------------------

TEST(Tty, TcharsRoundTripExceptTheDelimiter)
{
    struct termios t = cooked();
    t.c_cc[VINTR]    = 0177;
    t.c_cc[VQUIT]    = 034;
    t.c_cc[VSTART]   = 021;
    t.c_cc[VSTOP]    = 023;
    t.c_cc[VEOF]     = 004;

    Tchars c;
    termios_to_tchars(t, c);
    EXPECT_EQ(0177u, c.t_intrc);
    EXPECT_EQ(034u, c.t_quitc);
    EXPECT_EQ(021u, c.t_startc);
    EXPECT_EQ(023u, c.t_stopc);
    EXPECT_EQ(004u, c.t_eofc);

    // t_brkc is v7's SECOND line delimiter and termios has no such thing, so it reports
    // 0377 -- v7's own spelling of `no character', the one cmd/getty plants.
    EXPECT_EQ(0377u, c.t_brkc);

    struct termios back = cooked();
    tchars_to_termios(c, back);
    EXPECT_EQ(0177, back.c_cc[VINTR]);
    EXPECT_EQ(034, back.c_cc[VQUIT]);
    EXPECT_EQ(004, back.c_cc[VEOF]);
}
