// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>

long lseek(int fd, long off, int whence);

void rewind(FILE *iop)
{
    fflush(iop);
    lseek(fileno(iop), 0L, SEEK_SET);
    iop->_cnt = 0;
    iop->_ptr = iop->_base;
    iop->_flag &= ~(_IOERR | _IOEOF);
    if (iop->_flag & _IORW)
        iop->_flag &= ~(_IOREAD | _IOWRT);
}
