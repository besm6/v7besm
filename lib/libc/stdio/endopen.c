// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// The half of fopen()/freopen() that does the opening: attach `file' to the slot
// `iop', already chosen by the caller.
//
// The syscalls are declared here rather than in a header: v7 has no <unistd.h> and
// neither does include/, so a routine that calls one says so at its head.  off_t is
// one word, so lseek() takes three arguments and not the PDP-11's four.
//
#include <errno.h>
#include <stdio.h>

int open(const char *path, int mode);
int creat(const char *path, int mode);
int close(int fd);
long lseek(int fd, long off, int whence);

//
// "w+" and "a+" have to be created and then reopened for reading as well: creat()
// leaves the descriptor write-only.
//
static int create(const char *file, int rw)
{
    int f;

    f = creat(file, 0666);
    if (rw && f >= 0) {
        close(f);
        f = open(file, 2);
    }
    return f;
}

FILE *_endopen(const char *file, const char *mode, FILE *iop)
{
    int rw, f;

    if (iop == NULL)
        return NULL;

    rw = mode[1] == '+';

    switch (*mode) {
    case 'w':
        f = create(file, rw);
        break;

    case 'a':
        if ((f = open(file, rw ? 2 : 1)) < 0) {
            if (errno == ENOENT)
                f = create(file, rw);
        }
        lseek(f, 0L, SEEK_END);
        break;

    case 'r':
        f = open(file, rw ? 2 : 0);
        break;

    default:
        return NULL;
    }

    if (f < 0)
        return NULL;

    iop->_cnt  = 0;
    iop->_file = f;

    if (rw)
        iop->_flag |= _IORW;
    else if (*mode == 'r')
        iop->_flag |= _IOREAD;
    else
        iop->_flag |= _IOWRT;

    return iop;
}
