
#include <stdio.h>
#include <stdlib.h>
#include "besm6/b.out.h"

// Read one symbol table entry from a stream into *sym: a 1-byte name length,
// a 1-byte type, a half-word value, then that many name bytes. Allocates
// sym->n_name and NUL-terminates it.
// Returns the on-disk entry size in bytes (n_len + 5) on success, 1 for an
// empty entry / EOF (zero-length name), or 0 on out of memory.
int fgetsym(register FILE *text, register struct nlist *sym)
{
    register int c;

    if ((sym->n_len = getc(text)) <= 0)
        return 1;

    if (! (sym->n_name = malloc(sym->n_len+1)))
        return 0;

    sym->n_type = getc(text);
    sym->n_value = fgeth(text);

    for (c=0; c<sym->n_len; c++)
        sym->n_name [c] = getc(text);

    sym->n_name [sym->n_len] = '\0';

    // On disk: 1 len byte + 1 type byte + 3-byte value + n_len name bytes.
    return sym->n_len + 5;
}
