//
// termcapt -- lib/libtermcap: tgetent, tgetnum, tgetflag, tgetstr, tgoto and tputs.
//
// WHAT IS WORTH PROVING HERE is not that termcap parses -- it did that on a PDP-11 in
// 1980 -- but that it parses on a machine where a `char *' is a fat pointer.  Four
// comparisons between two of them had to go (lib/libtermcap/termcap.c says which and
// why: the byte offset sits above the word address and DECREMENTS as the pointer
// advances, so `<' orders them wrongly and in silence).  Every one of those four is on a
// buffer bound or an end-of-scan test, so a wrong answer does not fault -- it truncates
// an entry, mis-splices a `tc=', or scans off the front of the buffer.  Nothing but
// reading real entries out of a real database catches that, which is what this does.
//
// THIS PROGRAM OWNS ITS ENVIRONMENT.  tgetent() consults $TERMCAP before /etc/termcap,
// so a developer with that variable set in the shell would otherwise change what the
// test reads.  `environ' is crt0's and is a plain extern (<unistd.h>), so the test
// assigns its own one-entry vector instead -- v7's answer to this, before putenv
// existed -- and both branches become reachable from one program:
//
//      part 1  $TERMCAP holds AN ENTRY.  A hand-written terminal, `tt', that exercises
//              tdecode's whole escape table, the `@' cancellation the shipped database
//              has no reachable example of, and every % form of tgoto.
//      part 2  $TERMCAP holds A FILE NAME -- argv[1] -- and the real entries are read
//              out of it.  It must be absolute: tgetent treats a $TERMCAP not beginning
//              with `/' as an entry, which is part 1's case.
//
// ARGV[1] IS THE DATABASE, and that is what lets one .expected adjudicate both harnesses.
// Under b6sim it is the source tree's etc/termcap (lib/test/termcapt.args, where
// run-test.sh substitutes @srcdir@); under the booted kernel it is /etc/termcap, THE SAME
// FILE, staged onto the image by etc/CMakeLists.txt.  The host has no /etc/termcap to fall
// back on, so naming it is not a convenience.
//
// NOTHING HOST-DEPENDENT IS PRINTED: no path, no address, no capability whose value
// depends on the terminal anyone happens to be sitting at.  Every string goes through
// pr(), which renders a control character as ^X and anything above 0177 in octal, so the
// output is plain ASCII whatever the entry holds.
//
#include <stdio.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

static int errors;

// tgetent's buffer is 1024 characters by contract (<term.h>), and tgetstr's area is sized
// by the caller.  171 words and 34 of them here.
static char buf[1024];
static char area[200];
static char *ap;

// The environment this program hands itself; see the header.
static char *envvec[2];
static char tcvar[1024];

//
// A capability string, rendered so that the expectation is plain ASCII.
//
static void pr(char *s)
{
    if (s == 0) {
        printf("(none)");
        return;
    }
    for (; *s; s++) {
        int c = *s & 0377;
        if (c == 0177)
            printf("\\177");
        else if (c < 040)
            printf("^%c", c + 0100);
        else if (c > 0176)
            printf("\\%03o", c);
        else
            printf("%c", c);
    }
}

static void ck(char *what, int got, int want)
{
    printf("%-24s %6d", what, got);
    if (got != want) {
        printf("  WRONG, wanted %d", want);
        errors++;
    }
    printf("\n");
}

// A string capability: fetched, rendered, and checked against what it must decode to.
// Comparing the bytes matters as much as printing them -- pr() would render two different
// strings identically if one of them were truncated to nothing but printable characters.
static void cs(char *id, char *want)
{
    char *got;

    ap  = area;
    got = tgetstr(id, &ap);
    printf("%-24s ", id);
    pr(got);
    if (want == 0 ? got != 0 : (got == 0 || strcmp(got, want) != 0)) {
        printf("  WRONG, wanted ");
        pr(want);
        errors++;
    }
    printf("\n");
}

// tgoto over a cm string given literally, so the % form under test is visible in the
// source next to its answer rather than buried in a database entry.
static void cg(char *cm, int col, int line, char *want)
{
    char *got = tgoto(cm, col, line);

    printf("tgoto(");
    pr(cm);
    printf(",%d,%d) = ", col, line);
    pr(got);
    if (strcmp(got, want) != 0) {
        printf("  WRONG, wanted ");
        pr(want);
        errors++;
    }
    printf("\n");
}

// tputs' sink: collect the characters instead of writing them, so the padding decision --
// the delay is parsed and emitted as nothing (lib/libtermcap/tputs.c) -- is visible as the
// absence of the digits rather than as timing.
static char outbuf[64];
static int outn;

static int collect(int c)
{
    if (outn < (int)sizeof(outbuf) - 1)
        outbuf[outn++] = c;
    return c;
}

static void cp(char *id, int affcnt, char *want)
{
    char *s;

    ap   = area;
    s    = tgetstr(id, &ap);
    outn = 0;
    tputs(s, affcnt, collect);
    outbuf[outn] = 0;
    printf("tputs(%s,%d) = ", id, affcnt);
    pr(outbuf);
    if (strcmp(outbuf, want) != 0) {
        printf("  WRONG, wanted ");
        pr(want);
        errors++;
    }
    printf("\n");
}

//
// Part 1: $TERMCAP holds the entry itself.
//
// The entry is one line with the newlines already crunched out, which is the form
// tgetent's inline branch requires.  Every escape in tdecode's table appears once:
// \E -> 033, \^ -> ^, \\ -> \, \: -> :, \n \r \t \b \f, ^X -> control-X, and \101 -> 'A'
// for the octal arm.  `NC@' and `zz@' are the cancellations.  The last field must not be
// a `tc=' -- tnchktc() reads it -- and is not.
//
static char entry[] =
    "TERMCAP=tt|tt-test|termcap port test terminal:"
    "am:bs:NC@:co#40:li#12:ug#0:pa#0100:"
    "cl=^L:ce=\\E[K:ts=\\E\\:\\n\\r\\t\\b\\f\\^\\\\\\101:zz@:"
    "cm=%i%d;%dr:pd=50\\E[K:ps=3*\\EM:pf=1.5\\ED:";

static void part1(void)
{
    envvec[0] = entry;
    envvec[1] = 0;
    environ   = envvec;

    printf("--- part 1: $TERMCAP is an entry\n");
    // Every name of the first field must match, aliases and the one with spaces included.
    //
    // NO NEGATIVE CASE BELONGS HERE, and the reason is the harnesses: a $TERMCAP entry that
    // does NOT match makes tgetent fall through and open E_TERMCAP, so the answer is 0 under
    // the kernel (where /etc/termcap is on the image) and -1 under b6sim (where the host has
    // no such file).  Part 2 does the negative cases, against the real database, where both
    // worlds agree.
    ck("tgetent tt", tgetent(buf, "tt"), 1);
    ck("tgetent tt-test", tgetent(buf, "tt-test"), 1);
    ck("tgetent termcap port test terminal", tgetent(buf, "termcap port test terminal"), 1);

    ck("tgetent tt", tgetent(buf, "tt"), 1);
    ck("tgetnum co", tgetnum("co"), 40); // decimal
    ck("tgetnum li", tgetnum("li"), 12);
    ck("tgetnum ug", tgetnum("ug"), 0);
    ck("tgetnum pa", tgetnum("pa"), 64); // 0100, the octal arm
    ck("tgetnum xx", tgetnum("xx"), -1); // absent
    ck("tgetnum NC", tgetnum("NC"), -1); // cancelled with @
    ck("tgetflag am", tgetflag("am"), 1);
    ck("tgetflag bs", tgetflag("bs"), 1);
    ck("tgetflag xx", tgetflag("xx"), 0); // absent
    ck("tgetflag NC", tgetflag("NC"), 0); // cancelled with @

    // tdecode, escape by escape.  ts is \E : \n \r \t \b \f ^ \ A.
    cs("cl", "\014");
    cs("ce", "\033[K");
    cs("ts", "\033:\n\r\t\b\f^\\A");
    cs("xx", 0); // absent
    cs("zz", 0); // cancelled with @

    // tgoto.  The database's own cm first, then every % form given literally.
    cg("%i%d;%dr", 4, 9, "10;5r");
    cg("\033[%i%d;%dH", 0, 0, "\033[1;1H"); // one-origin, both fields
    cg("%d;%d", 7, 123, "123;7");           // %d widens as needed
    cg("%2;%2", 7, 8, "08;07");             // %2 always two digits
    cg("%3;%3", 7, 8, "008;007");           // %3 always three
    cg("%r%d;%d", 7, 8, "7;8");             // %r starts with the column
    cg("%.%.", 5, 6, "\006\005");           // %. is the raw value
    cg("%+ %+ ", 5, 6, "&%");               // %+x adds x first: 6+32, 5+32
    cg("%%%d", 0, 42, "%42");               // %% is a literal per cent
    cg("%B%d;%d", 0, 42, "66;0");           // BCD: 4<<4|2 = 0102 = 66
    cg("%D%d;%d", 0, 42, "22;0");           // Delta Data: 42 - 2*(42%16) = 22
    cg("%>2c%d;%d", 0, 1, "1;0");           // %>xy: 1 > '2' is false, no add
    cg("%>\001\002%d;%d", 0, 5, "7;0");     // 5 > 1, so add 2
    cg("%q", 1, 2, "OOPS");                 // an unimplemented form
    cg(0, 1, 2, "OOPS");                    // and a null cm

    // tputs: the delay is parsed and emitted as nothing at all.
    cp("pd", 1, "\033[K"); // 50 milliseconds
    cp("ps", 3, "\033M");  // 3 milliseconds, scaled by affcnt
    cp("pf", 1, "\033D");  // 1.5 milliseconds
    cp("ce", 1, "\033[K"); // no delay to strip
}

//
// Part 2: $TERMCAP names the file, and the entries are the shipped database's.
//
// WHAT THIS COVERS THAT PART 1 CANNOT: the read loop -- 1024-character buffered reads
// over a 4,993-byte file, entries continued across lines with an escaped newline, comment
// lines skipped by tnamatch, and `tc='.  Three of the four deleted pointer comparisons
// are in that loop and in tnchktc, so this is the part that would fail if any of them
// had been left as v7 wrote it.
//
static void part2(char *path)
{
    strcpy(tcvar, "TERMCAP=");
    strcat(tcvar, path);
    envvec[0] = tcvar;
    envvec[1] = 0;
    environ   = envvec;

    printf("--- part 2: $TERMCAP is a file name\n");

    // cons25, the first entry, and the one that is 15 continued lines long.
    ck("tgetent cons25", tgetent(buf, "cons25"), 1);
    ck("strlen(buf)", (int)strlen(buf), 751);
    ck("tgetnum co", tgetnum("co"), 80);
    ck("tgetnum li", tgetnum("li"), 25);
    ck("tgetflag am", tgetflag("am"), 1);
    cs("cl", "\033[H\033[J");
    cs("cm", "\033[%i%d;%dH");
    cs("kD", "\177"); // \177 -- the octal arm, in the real database
    cs("bl", "\007"); // ^G

    // Its alias, and the alias in the middle of a |-list.
    ck("tgetent ansi", tgetent(buf, "ansi"), 1);
    ck("tgetnum li", tgetnum("li"), 25);
    ck("tgetent ansi80x25", tgetent(buf, "ansi80x25"), 1);
    ck("tgetnum li", tgetnum("li"), 25);

    // vt100 -- a name with a space in it among its aliases, and capabilities that carry
    // padding delays, which is what makes it the interesting one for tputs.
    ck("tgetent vt100", tgetent(buf, "vt100"), 1);
    ck("tgetnum li", tgetnum("li"), 24);
    ck("tgetflag xn", tgetflag("xn"), 1);
    ck("tgetflag bw", tgetflag("bw"), 0); // cons25 has it; vt100 does not
    cs("cl", "50\033[H\033[J");
    cs("do", "2\033[B");
    cs("if", "/usr/share/tabset/vt100"); // a file that is not on this system
    cp("cl", 1, "\033[H\033[J");         // the 50 is stripped, not printed
    cp("sf", 24, "\033D");               // `2*' -- delay scaled by the line count
    cg("\033[%i%d;%dH", 39, 11, "\033[12;40H");
    ck("tgetent dec vt100", tgetent(buf, "dec vt100"), 1);
    ck("tgetnum li", tgetnum("li"), 24);

    // xterm: `tc=xterm-basic'.  co, li and cl are NOT in the xterm entry -- they arrive
    // only if tnchktc() found the referenced entry, cut its name field off and spliced
    // the rest on.  This is the check that the splice happened and landed in the right
    // place; the second tgetnum is the one that would break if the recursion clobbered
    // the outer buffer.
    ck("tgetent xterm", tgetent(buf, "xterm"), 1);
    // 844 = xterm's 261 less the 15 of `tc=xterm-basic:', plus xterm-basic's 630 less the
    // 32 of its own name field.  That arithmetic is tnchktc's, and it is the one place a
    // wrong pointer comparison would leave a plausible-looking entry.
    ck("strlen(buf)", (int)strlen(buf), 844);
    ck("tgetnum co", tgetnum("co"), 80);
    ck("tgetnum li", tgetnum("li"), 24);
    ck("tgetnum kn", tgetnum("kn"), 12);
    cs("cl", "\033[H\033[2J");
    cs("k1", "\033OP"); // from xterm itself, before the splice
    cs("as", "\033(0"); // from xterm-basic, after it
    ck("tgetent linux", tgetent(buf, "linux"), 1);
    ck("tgetnum co", tgetnum("co"), 80);

    // xterm-basic on its own -- the same entry, reached directly rather than through tc=.
    ck("tgetent xterm-basic", tgetent(buf, "xterm-basic"), 1);
    ck("strlen(buf)", (int)strlen(buf), 630);
    ck("tgetnum co", tgetnum("co"), 80);

    // xterm-color says `tc=xterm-r6', AND THERE IS NO xterm-r6 IN THIS DATABASE.  A
    // dangling tc= is a failed tgetent, not a partial entry: tnchktc's recursive call
    // comes back 0 and it propagates.  Left in rather than edited out of termcap.small,
    // because a chain that cannot be resolved is the one failure of tnchktc worth having
    // a case for.
    ck("tgetent xterm-color", tgetent(buf, "xterm-color"), 0);

    // A terminal the database does not describe, and one whose name is a prefix of a real
    // entry's -- tnamatch must not match either.
    ck("tgetent notaterminal", tgetent(buf, "notaterminal"), 0);
    ck("tgetent vt10", tgetent(buf, "vt10"), 0);
    ck("tgetent xter", tgetent(buf, "xter"), 0);

    // The comment lines of the file, and the copyright block at the top, begin with #.
    // tnamatch refuses a buffer starting with one, which is why a 34-line preamble does
    // not match the terminal called "#" or anything else.
    ck("tgetent #", tgetent(buf, "#"), 0);
}

//
// TWO BRANCHES ARE DELIBERATELY NOT COVERED, and both for the same reason: their answer
// would differ between the two harnesses, and a shared .expected cannot say two things.
//
//   tgetent's -1, the database that cannot be opened.  A $TERMCAP naming a file that is
//   not there falls back to E_TERMCAP -- and /etc/termcap EXISTS on the disk image and
//   does not exist on a macOS build host, so the same call returns 1 in one world and -1
//   in the other.
//
//   The `Termcap entry too long' arms, which want an entry past 1024 characters.  Nothing
//   in termcap.small comes near it (the longest is vt100's 820, and xterm's 844 after the
//   splice), and a fixture entry big enough would have to be a sixth of this program's
//   whole address space.
//

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: termcapt /path/to/termcap\n");
        return 1;
    }
    part1();
    part2(argv[1]);
    printf("--- %d error%s\n", errors, errors == 1 ? "" : "s");
    return errors != 0;
}
