// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>

void clearerr(FILE *iop)
{
    iop->_flag &= ~(_IOERR | _IOEOF);
}
