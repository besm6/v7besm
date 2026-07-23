// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Push one character back.  Exactly one is guaranteed, and only into a buffer that
// has somewhere to put it -- there is no separate pushback slot in the FILE.
//
// The _base == NULL guard is not v7's: v7 would step _ptr off a null base for a
// stream nothing had read from yet, which on this machine writes to word 0.
//
#include <stdio.h>

int ungetc(int c, FILE *iop)
{
    if (c == EOF || iop->_base == NULL)
        return EOF;
    if ((iop->_flag & _IOREAD) == 0 || iop->_ptr <= iop->_base) {
        if (iop->_ptr == iop->_base && iop->_cnt == 0)
            iop->_ptr++;
        else
            return EOF;
    }
    iop->_cnt++;
    *--iop->_ptr = c;
    iop->_flag &= ~_IOEOF;
    return c & 0377;
}
