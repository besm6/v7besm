
#include <stdio.h>
#include "besm6/b.out.h"

int fgetint(register FILE *f, register int *i)
{
    *i = fgeth(f);
    fgeth(f);
    return 1;
}
