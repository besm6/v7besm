/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * ttyslot() -- which line of /etc/ttys this process's terminal is, counting from 1.
 *
 * That number is also the slot in /etc/utmp, which is what getlogin() wants it for.
 * Zero means "no terminal", and is also what every failure answers with: v7 made no
 * distinction and neither caller could have used one.
 *
 * Descriptors 0, 1 and 2 are tried in turn, because a program with its output
 * redirected still has a terminal somewhere among them.  Each line of /etc/ttys begins
 * with two flag characters before the device name, which is what the `line + 2' below
 * skips past.
 *
 * ONE FIX to v7's getttys(): it tested for the end of its buffer after storing the
 * character, so a line of exactly 32 characters wrote one past the array.  The test
 * comes first here.  A line longer than the buffer is still truncated rather than
 * rejected, as v7 left it -- /etc/ttys entries are a dozen characters at most.
 *
 * It reads a character at a time, one read() apiece, and that is v7's doing: this runs
 * once at login and stdio would cost more to set up than it saved.
 *
 * No header declares it; a caller declares it itself.
 */
#include <string.h>

char *ttyname(int f);
int open(const char *path, int mode);
int read(int fd, char *buf, int n);
int close(int fd);

#define TTYLINE 32

static const char ttys[] = "/etc/ttys";

/* One line of /etc/ttys, past its two flag characters; NULL at end of file. */
static char *getttys(int f)
{
    static char line[TTYLINE];
    char *lp;

    lp = line;
    for (;;) {
        if (lp >= &line[TTYLINE - 1]) {
            *lp = '\0';
            return line + 2;
        }
        if (read(f, lp, 1) != 1)
            return NULL;
        if (*lp == '\n') {
            *lp = '\0';
            return line + 2;
        }
        lp++;
    }
}

int ttyslot(void)
{
    char *tp, *p;
    int s, tf;

    if ((tp = ttyname(0)) == NULL && (tp = ttyname(1)) == NULL && (tp = ttyname(2)) == NULL)
        return 0;
    if ((p = strrchr(tp, '/')) == NULL)
        p = tp;
    else
        p++;
    if ((tf = open(ttys, 0)) < 0)
        return 0;

    s = 0;
    while ((tp = getttys(tf)) != NULL) {
        s++;
        if (strcmp(p, tp) == 0) {
            close(tf);
            return s;
        }
    }
    close(tf);
    return 0;
}
