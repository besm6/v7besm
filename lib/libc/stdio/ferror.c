//
// ferror -- the function behind the macro (C11 §7.21.10.3).  See feof.c.
//
#include <stdio.h>

#undef ferror

int ferror(FILE *iop)
{
    return (iop->_flag & _IOERR) != 0;
}
