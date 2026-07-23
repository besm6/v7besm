//
// getc -- the function behind the macro (C11 §7.21.7.5).  See feof.c for why it
// has to exist; v7 shipped only fgetc(), which is the same routine under the other
// name.  §7.21.1p3 singles getc out as a macro that may evaluate its argument more
// than once -- this one, being a function, does not.
//
#include <stdio.h>

#undef getc

int getc(FILE *iop)
{
    return fgetc(iop);
}
