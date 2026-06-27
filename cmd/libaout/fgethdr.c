
#include <stdio.h>
#include "besm6/b.out.h"

// Read an a.out exec header from a stream into *h. Each of the 9 logical
// fields is stored as one full word (a value half-word followed by a zero
// padding half-word); fgetw() reads both halves and the padding is discarded.
// Always returns 1.
int fgethdr(register FILE *text, register struct exec *h)
{
    h->a_magic = fgetw(text);
    h->a_const = fgetw(text);
    h->a_text  = fgetw(text);
    h->a_data  = fgetw(text);
    h->a_bss   = fgetw(text);
    h->a_abss  = fgetw(text);
    h->a_syms  = fgetw(text);
    h->a_entry = fgetw(text);
    h->a_flag  = fgetw(text);
    return 1;
}
