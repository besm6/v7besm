/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

int puts(const char *s)
{
    int c;

    while ((c = *s++) != 0)
        putchar(c);
    return putchar('\n');
}
