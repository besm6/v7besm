// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// A subroutine version of the macro putchar.  v7's returned nothing; C11 §7.21.7.9
// wants the character back, or EOF.
//
#include <stdio.h>

#undef putchar

int putchar(int c)
{
    return putc(c, stdout);
}
