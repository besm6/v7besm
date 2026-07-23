// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Hand a stream a buffer of its own, or none at all.  The buffer must be BUFSIZ
// bytes -- that is what setbuf() promises and what _bufsiz is set to here; a caller
// with a buffer of some other size wants setvbuf().
//
// `buf == NULL' and not `!buf': a null char * is not a zero word, since the cast
// that makes it fat sets the marker bit over the zero address.  The compiler
// compares the address part for ==, which is what makes the first spelling right
// and the second unreliable (lib/README.md).
//
#include <stdio.h>
#include <stdlib.h>

void setbuf(FILE *iop, char *buf)
{
    if (iop->_base != NULL && iop->_flag & _IOMYBUF)
        free(iop->_base);
    iop->_flag &= ~(_IOMYBUF | _IOUNBUF | _IOLBUF);
    if ((iop->_base = buf) == NULL) {
        iop->_flag |= _IOUNBUF;
        iop->_bufsiz = 0;
    } else {
        iop->_ptr    = iop->_base;
        iop->_bufsiz = BUFSIZ;
    }
    iop->_cnt = 0;
}
