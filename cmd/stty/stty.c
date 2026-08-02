/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// stty -- set the modes of the terminal on the standard output.
//
//      stty            report the current modes
//      stty option...  set them
//
// Task C6.  THE PORT IS THE CUT: v7's stty is a table-driven program and most of its two
// tables describe a terminal this machine has not got.  ../getty/README.md made the same
// cut to the same driver a task earlier -- its thirteen-entry speed table came out with one
// entry left -- and everything below was measured against kernel/dev/tty.c's ttioccomm()
// and ttyoutput() rather than assumed.  ../../include/sys/ttyio.h lists which TIOC* the
// kernel answers at all; kernel/dev/sc.c is the driver, and its ioctl is one line long
// (every command a Consul honours is ttioccomm()'s).
//
// WHAT WENT, AND WHY EACH ONE:
//
//   speeds[], prspeed(), speed[], and the `gspeed' entry.  sg_ispeed/sg_ospeed are inert
//   storage: ttioccomm() stores them and hands them back, nothing between reads them, and
//   scopen() leaves them at B0 because `struct tty sc[NSC]' is bss.  A Consul-254 is a
//   parallel character-at-a-time typewriter -- one whole character per `ext' instruction --
//   so there is no line whose rate could be set.  With the table went the speed lines of
//   prmodes(), which would otherwise have reported `speed 0 baud' on every terminal for
//   ever.
//
//   even, -even, odd, -odd.  Parity is not carried: the Consul line is eight bits of data
//   end to end and partab[]'s parity bit went when the byte path did (task C11).
//
//   cr0-cr3, nl0-nl3, tab0-tab2, ff0, ff1, bs0, bs1, and the delay() calls in prmodes().
//   ttyoutput()'s tail is column bookkeeping and nothing else -- it generates no delays,
//   because on an eight-bit line a queued byte above 0177 is DATA and cannot also be a
//   delay count, and because there is no carriage here to wait for.  With them went the
//   terminal shorthands built out of them: 33, tty33, 37, tty37, 05, vt05, tn, tn300, ti,
//   ti700, tek.  Every one of those named a machine that is not this one.
//
//   NOT TBDELAY, which is XTABS and is live: ttyoutput() expands a tab to the next multiple
//   of eight by column.  `tabs' and `-tabs' stay, and they are the reason the delay mask
//   could not simply be deleted wholesale.
//
//   LCASE, lcase, -LCASE, -lcase.  This is the one the kernel DOES implement, in both
//   directions -- ttyinput() folds A-Z and ttyoutput() generates the backslash escapes --
//   and it comes out anyway, which is ../getty/README.md's finding rather than this
//   program's: the Consul's own code (GOST-10859) has no lower-case Latin at all, which is
//   why sc.c runs the line raw and speaks ASCII, and this machine's text is UTF-8 end to
//   end.  An stty that could turn LCASE on would be offering to fold that away, and the
//   letters it would fold are half of what the console prints.
//
//   hup.  TIOCHPCL sets the HUPCLS bit and NOTHING EVER TESTS IT: scclose() calls ttyclose()
//   and looks at nothing.  It was the program's only ioctl() call, so the cut takes the
//   whole gate with it and this program now reaches the terminal through gtty/stty alone.
//
// WHAT STAYED, all of it live in canon(), ttyinput(), ttyrend() or ttyoutput(): raw, -raw,
// cooked, nl, -nl, echo, -echo, tabs, -tabs, cbreak, -cbreak, ek, erase, kill.
//
// TANDEM IS LEFT UNREACHABLE, deliberately.  The flag is honoured -- ttyblock() queues the
// stop character when the input queue passes TTYHOG/2 -- but v7's stty never had an entry
// for it, the cut above is subtractive on getty's precedent, and on a typewriter with no
// keyboard buffer to stop it means as little as the delays do.  ../TODO.md carries it as a
// loose end rather than this file inventing a name for it.
//
// TIOCTSTP IS NOT COMING BACK.  ('t' << 8) | 16 had two names in v7, TIOCTSTP in <sgtty.h>
// and TIOCFLUSH in <sys/tty.h>; the two headers were folded onto <sys/ttyio.h> and only the
// name the kernel implements survived.  Nothing here uses either, the `hup' entry having
// taken the last ioctl with it.
//
// TWO FIXES BEYOND THE CUT:
//
//   - `stty erase' and `stty kill' with the keyword LAST walked argv past its terminator:
//     `if (eq("erase")) { if (**++argv == '^') ... argc--; }' reads *argv[argc] and then
//     dereferences it.  Both are bounded against argc here.
//
//   - gtty()'s return was ignored, so on anything that is not a terminal v7 reported the
//     modes of a zeroed bss structure -- a plausible-looking `speed 0 baud, erase = '^@''
//     that asserts nothing.  It is an error now, which is also what makes this program
//     honest under a harness that has no terminal at all.
//
// NOT SETUID: gtty/stty are gated on nothing but having the descriptor open.
//
#include <sgtty.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const struct {
    const char *string;
    int set;
    int reset;
} modes[] = {
    //
    // ONE CAPABILITY PER LINE.  This table is the whole of what the program can do, and
    // README.md's account of what came out of it reads against these rows; clang-format
    // would pack them three to a line and make the two unreadable together.
    //
    // clang-format off
    { "raw",     RAW,    0      },
    { "-raw",    0,      RAW    },
    { "cooked",  0,      RAW    },
    { "-nl",     CRMOD,  0      },
    { "nl",      0,      CRMOD  },
    { "echo",    ECHO,   0      },
    { "-echo",   0,      ECHO   },
    { "-tabs",   XTABS,  0      },
    { "tabs",    0,      XTABS  },
    { "cbreak",  CBREAK, 0      },
    { "-cbreak", 0,      CBREAK },
    { 0,         0,      0      },
    // clang-format on
};

static char *arg;
static struct sgttyb mode;

static int eq(const char *string)
{
    int i;

    if (!arg)
        return 0;
    i = 0;
    for (;;) {
        if (arg[i] != string[i])
            return 0;
        if (arg[i++] == '\0')
            break;
    }
    arg = 0;
    return 1;
}

static void prmodes(void)
{
    int m;

    if (mode.sg_erase < ' ')
        fprintf(stderr, "erase = '^%c'; ", '@' + mode.sg_erase);
    else
        fprintf(stderr, "erase = '%c'; ", mode.sg_erase);
    if (mode.sg_kill < ' ')
        fprintf(stderr, "kill = '^%c'\n", '@' + mode.sg_kill);
    else
        fprintf(stderr, "kill = '%c'\n", mode.sg_kill);
    m = mode.sg_flags;
    if (m & RAW)
        fprintf(stderr, "raw ");
    if (m & CRMOD)
        fprintf(stderr, "-nl ");
    if (m & ECHO)
        fprintf(stderr, "echo ");
    if ((m & XTABS) == XTABS)
        fprintf(stderr, "-tabs ");
    if (m & CBREAK)
        fprintf(stderr, "cbreak ");
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
    int i;

    if (gtty(1, &mode) < 0) {
        fprintf(stderr, "stty: not a terminal\n");
        return 1;
    }
    if (argc == 1) {
        prmodes();
        return 0;
    }
    while (--argc > 0) {
        arg = *++argv;
        if (eq("ek")) {
            mode.sg_erase = '#';
            mode.sg_kill  = '@';
        }
        // The bound is the fix: v7 read argv[argc] and dereferenced it when the
        // keyword was the last argument on the line.
        if (eq("erase")) {
            if (argc < 2) {
                fprintf(stderr, "stty: erase needs a character\n");
                return 1;
            }
            if (**++argv == '^')
                mode.sg_erase = (*argv)[1] & 037;
            else
                mode.sg_erase = **argv;
            argc--;
        }
        if (eq("kill")) {
            if (argc < 2) {
                fprintf(stderr, "stty: kill needs a character\n");
                return 1;
            }
            if (**++argv == '^')
                mode.sg_kill = (*argv)[1] & 037;
            else
                mode.sg_kill = **argv;
            argc--;
        }
        for (i = 0; modes[i].string; i++)
            if (eq(modes[i].string)) {
                mode.sg_flags &= ~modes[i].reset;
                mode.sg_flags |= modes[i].set;
            }
        if (arg)
            fprintf(stderr, "unknown mode: %s\n", arg);
    }
    stty(1, &mode);
    return 0;
}
