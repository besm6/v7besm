// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Find a free slot in _iob[], or NULL when all _NFILE are in use.
//
// A slot is free when none of the three "open" bits is set; fclose() clears them,
// which is what makes a closed stream reusable.
//
#include <stdio.h>

extern FILE *_lastbuf;

FILE *_findiop(void)
{
    FILE *iop;

    for (iop = _iob; iop->_flag & (_IOREAD | _IOWRT | _IORW); iop++)
        if (iop >= _lastbuf)
            return NULL;

    return iop;
}
