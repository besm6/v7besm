//
// setvbuf -- choose the buffering mode and the buffer (C11 §7.21.5.6).  Not a v7
// routine: v7 had setbuf() alone, which could only say "this buffer" or "none".
//
// _bufsiz exists for this call.  v7 wrote BUFSIZ into _filbuf and _flsbuf outright
// because setbuf() promised a buffer of exactly that size; here the caller names
// the size, so the stream has to remember it or overrun what it was given.
//
// The three modes are NOT the flag bits of the same family: _IOFBF/_IOLBF/_IONBF
// are 0/1/2 and _IOUNBUF/_IOLBUF are 04/0400 (include/stdio.h).  The clash is why
// v7's _IONBF flag bit is spelled _IOUNBUF now.
//
#include <stdio.h>
#include <stdlib.h>

int setvbuf(FILE *iop, char *buf, int mode, size_t size)
{
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)
        return -1;

    if (iop->_base != NULL && iop->_flag & _IOMYBUF)
        free(iop->_base);
    iop->_flag &= ~(_IOMYBUF | _IOUNBUF | _IOLBUF);
    iop->_base   = NULL;
    iop->_bufsiz = 0;
    iop->_cnt    = 0;

    if (mode == _IONBF) {
        iop->_flag |= _IOUNBUF;
        return 0;
    }

    if (size == 0)
        return -1;
    if (buf == NULL) {
        if ((buf = malloc(size)) == NULL)
            return -1;
        iop->_flag |= _IOMYBUF;
    }
    iop->_base   = buf;
    iop->_ptr    = buf;
    iop->_bufsiz = size;
    if (mode == _IOLBF)
        iop->_flag |= _IOLBUF;
    return 0;
}
