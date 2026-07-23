// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// v7 returned whatever the last putc() gave back, and nothing at all for an empty
// string; C11 §7.21.7.4 wants a non-negative value on success and EOF on failure,
// which is what ferror() answers.
//
#include <stdio.h>

int fputs(const char *s, FILE *iop)
{
    int c;

    while ((c = *s++) != 0)
        putc(c, iop);
    return ferror(iop) ? EOF : 0;
}
