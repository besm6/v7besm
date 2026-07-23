/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * fopen() on a descriptor that is already open.  The mode has to be repeated
 * because there is no way to ask the kernel what it was.
 */
#include <stdio.h>

long lseek(int fd, long off, int whence);

FILE *_findiop(void);

FILE *fdopen(int fd, const char *mode)
{
    FILE *iop;

    if ((iop = _findiop()) == NULL)
        return NULL;

    iop->_cnt  = 0;
    iop->_file = fd;
    switch (*mode) {
    case 'r':
        iop->_flag |= _IOREAD;
        break;

    case 'a':
        lseek(fd, 0L, SEEK_END);
        /* No break */
    case 'w':
        iop->_flag |= _IOWRT;
        break;

    default:
        return NULL;
    }

    if (mode[1] == '+') {
        iop->_flag &= ~(_IOREAD | _IOWRT);
        iop->_flag |= _IORW;
    }

    return iop;
}
