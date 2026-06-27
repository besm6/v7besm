
#include <stdio.h>
#include "besm6/b.out.h"

// Write an a.out exec header to a stream, the inverse of fgethdr(). Each of
// the 9 logical fields is emitted as one full word via fputw(): the value in
// the low half-word and a zero padding half-word, so every field starts on a
// 6-byte word boundary.
void fputhdr(const struct exec *filhdr, register FILE *coutb)
{
    fputw((uword_t) filhdr->a_magic, coutb);
    fputw((uword_t) filhdr->a_const, coutb);
    fputw((uword_t) filhdr->a_text, coutb);
    fputw((uword_t) filhdr->a_data, coutb);
    fputw((uword_t) filhdr->a_bss, coutb);
    fputw((uword_t) filhdr->a_abss, coutb);
    fputw((uword_t) filhdr->a_syms, coutb);
    fputw((uword_t) filhdr->a_entry, coutb);
    fputw((uword_t) filhdr->a_flag, coutb);
}
