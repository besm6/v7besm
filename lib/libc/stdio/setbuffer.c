// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// setbuffer, setlinebuf -- the BSD half of the buffering interface.  Neither is C11
// and neither was in v7; both say what setvbuf() says, and callers in cmd/ reach for
// them (cmd/cpp/cpp.c).
//
// setbuffer is setbuf() with a size.  A null buffer -- or a size that cannot hold
// one -- means unbuffered, where setvbuf() itself would fail the call.
//
// `buf != NULL' and not `buf': a null char * is not a zero word, since the cast that
// makes it fat sets the marker bit over the zero address.  The compiler compares the
// address part for ==, which is what makes the first spelling right (setbuf.c).
//
// setlinebuf only turns the bit on, where setvbuf(iop, NULL, _IOLBF, BUFSIZ) would
// malloc: _flsbuf() picks a buffer lazily when _base is NULL, and hands stdout the
// static _sobuf rather than the heap, so a stream that has a buffer keeps it and one
// that has none pays nothing until it writes.  A write stream is held at _cnt == 0
// so that every putc misses into _flsbuf, which is the whole mechanism (stdio.h);
// a read stream's _cnt is bytes still unread and is left alone.
//
#include <stdio.h>

void setbuffer(FILE *iop, char *buf, int size)
{
    (void)setvbuf(iop, buf, (buf != NULL && size > 0) ? _IOFBF : _IONBF, (size_t)size);
}

int setlinebuf(FILE *iop)
{
    iop->_flag = (iop->_flag & ~_IOUNBUF) | _IOLBUF;
    if (iop->_flag & _IOWRT)
        iop->_cnt = 0;
    return 0;
}
