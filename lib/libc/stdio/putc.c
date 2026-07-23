//
// putc -- the function behind the macro (C11 §7.21.7.8).  See getc.c.
//
#include <stdio.h>

#undef putc

int putc(int c, FILE *iop)
{
    return fputc(c, iop);
}
