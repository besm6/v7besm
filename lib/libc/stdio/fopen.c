/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdio.h>

FILE *_findiop(void);
FILE *_endopen(const char *file, const char *mode, FILE *iop);

FILE *fopen(const char *file, const char *mode)
{
    return _endopen(file, mode, _findiop());
}
