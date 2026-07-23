// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>

FILE *_endopen(const char *file, const char *mode, FILE *iop);

FILE *freopen(const char *file, const char *mode, FILE *iop)
{
    fclose(iop);
    return _endopen(file, mode, iop);
}
