// term_key() without a terminal.
//
// The decoder needs bytes on file descriptor 0 and nothing else -- no tty, no stty --
// so it is the one half of terminal.c that has a b6sim case.  pipe(2) and dup2(2) are
// both real under b6sim (cmd/sim/syscall.cpp), and a pipe with the write end closed
// behaves exactly like a terminal that has stopped typing.
//
// THE LAST TWO ROWS ARE THE POINT OF THIS PROGRAM ON THIS MACHINE.  Upstream read
// 0233 as an eight-bit CSI introducer, so "\2335~" meant Page Up.  0233 is 0x9B, the
// SECOND BYTE of Cyrillic Л (U+041B = D0 9B), so that reading also meant a user
// typing Л lost the keystroke after it.  The arm is gone (terminal.c), 0233 is now an
// ordinary byte, and the `Cyrillic Л' row asserts that the two bytes of one letter
// come back as two bytes.
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "terminal.h"

#define MAXWANT 3

// The dimension is written out rather than inferred from the initialiser: b6lower
// answers `get_size: Array size not specified' to `struct key_test tests[] = {...}'.
#define NTESTS 26

struct key_test {
    char *name;
    char *input;
    int length;
    int nwant;
    int want[MAXWANT];
};

static void fail(char *name, int got, int expected)
{
    (void)fprintf(stderr, "test_terminal: %s returned %d, expected %d\n", name, got, expected);
    exit(1);
}

int main(void)
{
    struct key_test tests[NTESTS] = {
        { "CSI Home", "\033[H", 3, 1, { KEY_HOME } },
        { "SS3 Home", "\033OH", 3, 1, { KEY_HOME } },
        { "Home 1", "\033[1~", 4, 1, { KEY_HOME } },
        { "Home 7", "\033[7~", 4, 1, { KEY_HOME } },
        { "CSI End", "\033[F", 3, 1, { KEY_END } },
        { "SS3 End", "\033OF", 3, 1, { KEY_END } },
        { "End 4", "\033[4~", 4, 1, { KEY_END } },
        { "End 8", "\033[8~", 4, 1, { KEY_END } },
        { "Insert", "\033[2~", 4, 1, { KEY_INSERT } },
        { "Delete", "\033[3~", 4, 1, { KEY_DELETE } },
        { "Page Up", "\033[5~", 4, 1, { KEY_PGUP } },
        { "Page Down", "\033[6~", 4, 1, { KEY_PGDN } },
        { "Shift Page Up", "\033[5;2~", 6, 1, { KEY_PGUP } },
        { "Control Page Down", "\033[6;5~", 6, 1, { KEY_PGDN } },
        { "Legacy Page Up", "\033[I", 3, 1, { KEY_PGUP } },
        { "Legacy Page Down", "\033[G", 3, 1, { KEY_PGDN } },
        { "Arrow up", "\033[A", 3, 1, { KEY_UP } },
        { "Arrow down", "\033[B", 3, 1, { KEY_DOWN } },
        { "Arrow right", "\033[C", 3, 1, { KEY_RIGHT } },
        { "Arrow left", "\033[D", 3, 1, { KEY_LEFT } },
        { "Plain byte", "a", 1, 1, { 'a' } },
        { "Control byte", "\013", 1, 1, { 013 } },
        { "Lone escape", "\033x", 2, 1, { 27 } },
        { "End of input", "", 0, 1, { -1 } },
        // D0 9B is Л.  Two bytes in, two bytes out, and 0233 is not a CSI.
        { "Cyrillic Л", "\320\233", 2, 2, { 0320, 0233 } },
        // Truncated: the parameter-skip loop must give up rather than spin, and
        // an unrecognised sequence reads as a bare ESC.
        { "Truncated CSI", "\033[5;", 4, 1, { 27 } }
    };
    int fds[2];
    int count;
    int i;
    int j;
    int got;

    count = sizeof tests / sizeof tests[0];
    for (i = 0; i < count; ++i) {
        if (pipe(fds) < 0)
            fail("pipe", -1, 0);
        if (tests[i].length > 0 &&
            write(fds[1], tests[i].input, tests[i].length) != tests[i].length)
            fail("write", -1, tests[i].length);
        (void)close(fds[1]);
        if (dup2(fds[0], 0) < 0)
            fail("dup2", -1, 0);
        (void)close(fds[0]);
        for (j = 0; j < tests[i].nwant; ++j) {
            got = term_key();
            if (got != tests[i].want[j])
                fail(tests[i].name, got, tests[i].want[j]);
        }
    }
    (void)printf("terminal key tests passed\n");
    return 0;
}
