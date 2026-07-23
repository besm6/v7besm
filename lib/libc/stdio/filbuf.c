/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Refill a read buffer: what the getc() macro falls out to when the count runs out.
 *
 * A string stream (sscanf) never refills -- its whole input was there from the
 * start -- so _IOSTRG returns EOF, which is what stops _doscan().
 *
 * _IOUNBUF is v7's _IONBF; the bit kept its value and gave up its name to C11's
 * setvbuf mode (include/stdio.h).  An unbuffered stream still needs somewhere to
 * put the one byte it reads, hence smallbuf: one char per descriptor, so two
 * unbuffered streams cannot tread on each other.
 */
#include <stdio.h>
#include <stdlib.h>

int read(int fd, char *buf, int n);

int _filbuf(FILE *iop)
{
    static char smallbuf[_NFILE];

    if (iop->_flag & _IORW)
        iop->_flag |= _IOREAD;

    if ((iop->_flag & _IOREAD) == 0 || iop->_flag & _IOSTRG)
        return EOF;

tryagain:
    if (iop->_base == NULL) {
        if (iop->_flag & _IOUNBUF) {
            iop->_base   = &smallbuf[fileno(iop)];
            iop->_bufsiz = 1;
            goto tryagain;
        }
        if ((iop->_base = malloc(BUFSIZ)) == NULL) {
            iop->_flag |= _IOUNBUF;
            goto tryagain;
        }
        iop->_bufsiz = BUFSIZ;
        iop->_flag |= _IOMYBUF;
    }
    iop->_ptr = iop->_base;
    iop->_cnt = read(fileno(iop), iop->_ptr, iop->_flag & _IOUNBUF ? 1 : iop->_bufsiz);
    if (--iop->_cnt < 0) {
        if (iop->_cnt == -1) {
            iop->_flag |= _IOEOF;
            if (iop->_flag & _IORW)
                iop->_flag &= ~_IOREAD;
        } else
            iop->_flag |= _IOERR;
        iop->_cnt = 0;
        return EOF;
    }
    return *iop->_ptr++ & 0377;
}
