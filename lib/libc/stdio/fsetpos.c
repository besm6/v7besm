/*
 * fsetpos -- restore a position recorded by fgetpos (C11 §7.21.9.3).  Not a v7
 * routine, and the mirror of fgetpos.c: fpos_t is a plain byte offset here.
 */
#include <stdio.h>

int fsetpos(FILE *iop, const fpos_t *pos)
{
    return fseek(iop, *pos, SEEK_SET);
}
