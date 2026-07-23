/*
 * tmpnam -- a file name no file has (C11 §7.21.4.4).  Not a v7 routine.
 *
 * It does NOT go through mktemp(): that one fills the trailing X's from the
 * process id and then walks a SINGLE letter, so it can distinguish 26 names and
 * TMP_MAX promises 26^3.  The three letters are counted here instead, and the pid
 * still separates one process's names from another's.
 *
 * "/tmp/t" + at most five pid digits + three letters + NUL is 15 characters, which
 * is inside L_tmpnam.  s == NULL asks for the static buffer, which the next call
 * overwrites -- that is what §7.21.4.4 says happens.
 */
#include <stdio.h>

int access(const char *path, int mode);
int getpid(void);

static char stat_buf[L_tmpnam];

char *tmpnam(char *s)
{
    static int seq;
    char *p;
    unsigned pid;
    int n, i;

    if (s == NULL)
        s = stat_buf;

    for (n = 0; n < TMP_MAX; n++) {
        p    = s;
        *p++ = '/';
        *p++ = 't';
        *p++ = 'm';
        *p++ = 'p';
        *p++ = '/';
        *p++ = 't';
        /* The pid, most significant digit first. */
        pid = getpid();
        for (i = 10000; i > 1; i /= 10)
            if (pid >= (unsigned)i)
                break;
        for (; i > 0; i /= 10) {
            *p++ = (pid / i) % 10 + '0';
        }
        /* Three letters of the sequence: 26^3 names, which is TMP_MAX. */
        i = seq;
        seq++;
        if (seq >= TMP_MAX)
            seq = 0;
        *p++ = 'a' + (i / 676) % 26;
        *p++ = 'a' + (i / 26) % 26;
        *p++ = 'a' + i % 26;
        *p   = '\0';
        if (access(s, 0) == -1)
            return s;
    }
    return NULL;
}
