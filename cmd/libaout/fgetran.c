
#include <stdio.h>
#include <stdlib.h>

#include "besm6/b.out.h"
#include "besm6/ranlib.h"

// Read one ranlib (archive symbol-index) entry from a stream into *sym:
// a 1-byte name length, a half-word archive offset, then that many name bytes.
// Allocates sym->ran_name and NUL-terminates it.
// Returns 1 on success, 0 on EOF (zero-length entry), -1 on out of memory.
int fgetran(FILE *text, struct ranlib *sym)
{
    int c;

    /* read struct ranlib from file */
    /* 1 byte - length of name */
    /* 3 bytes - half-word seek in archive */
    /* 'len' bytes - symbol name */
    /* if len == 0 then eof */
    /* return 1 if ok, 0 if eof, -1 if out of memory */

    if ((sym->ran_len = getc(text)) <= 0)
        return 0;

    if (!(sym->ran_name = malloc(sym->ran_len + 1)))
        return -1;

    sym->ran_off = fgeth(text);
    for (c = 0; c < sym->ran_len; c++)
        sym->ran_name[c] = getc(text);

    sym->ran_name[sym->ran_len] = '\0';

    return 1;
}
