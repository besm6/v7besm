//
// Make a unique file and return a descriptor open on it, read-write.
//
// The safe half of mktemp(3): it picks the name the same way, but it creates the
// file itself instead of handing back a name for the caller to race on.  Neither
// v7 nor C11 has it; POSIX does, and cmd/ar wants it three times over -- one
// named, re-readable scratch file per insertion point, which tmpfile() cannot
// give (see cmd/ar/README.md).
//
// TWO THINGS THIS MACHINE MAKES DIFFERENT, and both are why the routine could not
// simply be written on top of mktemp() and open():
//
//   - mktemp() CONSUMES the run of `X': after one call there is nothing left to
//     vary, so a retry loop around it would offer the same name for ever.  Its
//     loop is therefore written out below rather than called.
//   - There is no O_CREAT and no O_EXCL in this kernel (<fcntl.h>).  creat() is
//     the only way to make a file and it hands back a WRITE-ONLY descriptor, so
//     the file has to be reopened -- the same dance lib/libc/stdio/endopen.c does
//     for "w".  Which means this mkstemp() INHERITS mktemp()'s race rather than
//     closing it: what the caller gains is the file existing, read-write, before
//     anything is written to it, not exclusive creation.
//
// BOTH ROUTINES LIVE IN ONE FILE because mkstemp() IS mkstemps() with a zero
// suffix, and a caller that wants either always pulls both.  mkstemps() is the
// BSD extension of the pair and came with cmd/cc (task C9e): the driver names its
// temporaries `/tmp/ccXXXXXX.i', `.ast', `.tac', `.s', because every stage of the
// pipeline is a file whose SUFFIX says what is in it -- and the plain mkstemp()
// cannot fill a run of `X' that is not at the end of the template.
//
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

//
// As mkstemp(), but the last `suffixlen' bytes of the template are a fixed suffix
// and the run of `X' ends just before them.  A negative or over-long suffixlen is
// EINVAL, as BSD's is.
//
int mkstemps(char *as, int suffixlen)
{
    char *s, *last;
    unsigned pid;
    int i, fd;

    // Find the end of the template, then step back over the fixed suffix: `last'
    // is one past the rightmost `X' the walk below may write.
    last = as;
    while (*last)
        last++;
    if (suffixlen < 0 || last - as < suffixlen) {
        errno = EINVAL;
        return -1;
    }
    last -= suffixlen;

    // Fill the trailing run of `X' with the digits of the process id, rightmost
    // digit first, and leave s on the leftmost of them.
    pid = getpid();
    s   = last;
    while (s > as && s[-1] == 'X') {
        --s;
        *s = (pid % 10) + '0';
        pid /= 10;
    }

    // The all-digits name is the first candidate; then a single letter in that
    // leftmost position, `a' through `z', as mktemp() walks it.  s == last means
    // the template carried no `X' at all -- there is nowhere to put a letter, so
    // the one name is the only candidate.
    for (i = 'a';; i++) {
        if (access(as, 0) < 0) {
            fd = creat(as, 0600);
            if (fd < 0)
                return -1;
            close(fd);
            return open(as, O_RDWR);
        }
        if (s == last || i > 'z') {
            errno = EEXIST;
            return -1;
        }
        *s = i;
    }
}

int mkstemp(char *as)
{
    return mkstemps(as, 0);
}
