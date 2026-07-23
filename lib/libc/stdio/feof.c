//
// feof -- the function behind the macro (C11 §7.21.10.2).
//
// §7.1.4 lets a header hide a library function behind a macro, but the function
// has to exist as well, for the caller that suppresses the macro with #undef or
// takes the routine's address.  v7 shipped the macro alone.
//
#include <stdio.h>

#undef feof

int feof(FILE *iop)
{
    return (iop->_flag & _IOEOF) != 0;
}
