
#include <stdio.h>

#include "besm6/b.out.h"
#include "besm6/ranlib.h"

// Write one ranlib (archive symbol-index) entry to a stream, the inverse of
// fgetran(): a 1-byte name length, a half-word archive offset, then the name
// bytes (no trailing NUL).
void fputran(const struct ranlib *s, FILE *file)
{
    int i;

    putc(s->ran_len, file);
    fputh(s->ran_off, file);
    for (i = 0; i < s->ran_len; i++)
        putc(s->ran_name[i], file);
}
