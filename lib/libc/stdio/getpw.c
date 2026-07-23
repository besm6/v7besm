/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * getpw(uid, buf) -- the whole /etc/passwd line for a user id; 0 found, 1 not.
 *
 * v7 called this one obsolete in its own manual and kept it for the programs that
 * already used it: it hands back the raw line rather than a struct passwd, and the
 * caller does its own splitting.  getpwuid() is what new code wants.
 *
 * It is not built on getpwent() and does not share its statics -- it has a stream of
 * its own, never closed, exactly as v7 left it -- which is why it is a file of its own
 * rather than a fifth entry point next door.
 *
 * The uid field is parsed by v7's odd little loop: three colons are skipped, then
 * digits are accumulated from `n', which is 0 at that point because the `while (--n)'
 * above ran it down.  It reads as an accident and is not one.
 *
 * ONE FIX: v7 guarded those two field walks against a '\n', which cannot be there --
 * the line was copied in a character at a time and stopped AT the newline, so what
 * terminates buf is a '\0'.  A line with fewer than three colons therefore walked off
 * the end of the caller's buffer.  Both loops stop on the terminator here.
 *
 * No header declares it; a caller declares it itself, as one does for getpass().
 */
#include <stdio.h>

int getpw(int uid, char buf[])
{
    static FILE *pwf;
    int n, c;
    char *bp;

    if (pwf == 0)
        pwf = fopen("/etc/passwd", "r");
    if (pwf == NULL)
        return 1;
    rewind(pwf);

    for (;;) {
        bp = buf;
        while ((c = getc(pwf)) != '\n') {
            if (c == EOF)
                return 1;
            *bp++ = c;
        }
        *bp++ = '\0';
        bp    = buf;
        n     = 3;
        while (--n)
            while ((c = *bp++) != ':')
                if (c == '\0')
                    return 1;
        while ((c = *bp++) != ':') {
            if (c == '\0')
                return 1;
            if (c < '0' || c > '9')
                continue;
            n = n * 10 + c - '0';
        }
        if (n == uid)
            return 0;
    }
}
