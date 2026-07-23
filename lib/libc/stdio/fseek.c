/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Seek for the standard library.  Coordinates with buffering.
 *
 * The whole point of the first arm is to seek WITHIN the buffer when the target is
 * already there, without troubling the kernel.  off_t is one word here, so the
 * `long' the prototype names is the same word as the `int' the arithmetic uses and
 * v7's (int) casts on the buffer arithmetic are gone.
 *
 * p is initialised to -1 so that a stream that is neither readable nor writable
 * reports failure rather than whatever the register happened to hold; v7 left it
 * uninitialised.
 */
#include <stdio.h>

long lseek(int fd, long off, int whence);

int fseek(FILE *iop, long offset, int ptrname)
{
    int c;
    long p;

    p = -1;
    iop->_flag &= ~_IOEOF;
    if (iop->_flag & _IOREAD) {
        if (ptrname < 2 && iop->_base && !(iop->_flag & _IOUNBUF)) {
            c = iop->_cnt;
            p = offset;
            if (ptrname == 0)
                p += c - lseek(fileno(iop), 0L, SEEK_CUR);
            else
                offset -= c;
            if (!(iop->_flag & _IORW) && c > 0 && p <= c && p >= iop->_base - iop->_ptr) {
                iop->_ptr += p;
                iop->_cnt -= p;
                return 0;
            }
        }
        if (iop->_flag & _IORW) {
            iop->_ptr = iop->_base;
            iop->_flag &= ~_IOREAD;
        }
        p         = lseek(fileno(iop), offset, ptrname);
        iop->_cnt = 0;
    } else if (iop->_flag & (_IOWRT | _IORW)) {
        fflush(iop);
        if (iop->_flag & _IORW) {
            iop->_cnt = 0;
            iop->_flag &= ~_IOWRT;
            iop->_ptr = iop->_base;
        }
        p = lseek(fileno(iop), offset, ptrname);
    }
    return p == -1 ? -1 : 0;
}
