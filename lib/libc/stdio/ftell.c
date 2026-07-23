/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Return the file offset.  Coordinates with buffering: the kernel's idea of the
 * position is ahead of the caller's by whatever is still sitting in a read buffer,
 * and behind it by whatever is still sitting in a write buffer.
 */
#include <stdio.h>

long lseek(int fd, long off, int whence);

long ftell(FILE *iop)
{
    long tres;
    int adjust;

    if (iop->_cnt < 0)
        iop->_cnt = 0;
    if (iop->_flag & _IOREAD)
        adjust = -iop->_cnt;
    else if (iop->_flag & (_IOWRT | _IORW)) {
        adjust = 0;
        if (iop->_flag & _IOWRT && iop->_base && (iop->_flag & _IOUNBUF) == 0)
            adjust = iop->_ptr - iop->_base;
    } else
        return -1;
    tres = lseek(fileno(iop), 0L, SEEK_CUR);
    if (tres < 0)
        return tres;
    tres += adjust;
    return tres;
}
