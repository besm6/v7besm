
#include <stdio.h>
#include "besm6/b.out.h"

// Write an a.out exec header to a stream, the inverse of fgethdr(). Each of
// the 9 logical fields is emitted as a value half-word followed by a zero
// padding half-word, so every field starts on a 6-byte word boundary.
void fputhdr(const struct exec *filhdr, register FILE *coutb)
{
    fputh((long) filhdr->a_magic, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_const, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_text, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_data, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_bss, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_abss, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_syms, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_entry, coutb);
    fputh(0L, coutb);

    fputh((long) filhdr->a_flag, coutb);
    fputh(0L, coutb);
}
