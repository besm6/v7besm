
#include <stdio.h>
#include "besm6/b.out.h"

// Write one symbol table entry to a stream, the inverse of fgetsym(): a
// 1-byte name length, a 1-byte type, a half-word value, then the name bytes
// (no trailing NUL).
void fputsym(const struct nlist *s, FILE *file)
{
    int i;

    putc(s->n_len, file);
    putc(s->n_type, file);
    fputh(s->n_value, file);
    for (i=0; i<s->n_len; i++)
        putc(s->n_name[i], file);
}
