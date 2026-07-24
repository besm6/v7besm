/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// Input, output and file copying.
//
#include <fcntl.h>
#include <unistd.h>

#include "defs.h"

void initf(UFD fd)
{
    SHFILE f = standin;

    f->fdes = fd;
    f->fsiz = ((flags & (oneflg | ttyflg)) == 0 ? SHBUFSIZ : 1);
    f->fnxt = f->fend = f->fbuf;
    f->feval          = 0;
    f->flin           = 1;
    f->feof           = FALSE;
}

INT estabf(STRING s)
{
    SHFILE f;

    (f = standin)->fdes = -1;
    f->fend             = length(s) + (f->fnxt = s);
    f->flin             = 1;
    return f->feof      = (s == 0);
}

void push(SHFILE af)
{
    SHFILE f;

    (f = af)->fstak = standin;
    f->feof         = 0;
    f->feval        = 0;
    standin         = f;
}

INT pop(void)
{
    SHFILE f;

    if ((f = standin)->fstak) {
        if (f->fdes >= 0)
            close(f->fdes);
        standin = f->fstak;
        return TRUE;
    }
    return FALSE;
}

void chkpipe(INT *pv)
{
    if (pipe(pv) < 0 || pv[INPIPE] < 0 || pv[OTPIPE] < 0)
        error(piperr);
}

INT chkopen(STRING idf)
{
    INT rc;

    if ((rc = open(idf, O_RDONLY)) < 0)
        failed(idf, badopen);
    return rc;
}

//
// Move file descriptor f1 onto f2 and close f1.
//
// SHRENAME, NOT RENAME: ISO C owns `rename', with two const char * parameters, and
// <stdio.h> declares it.  v7 had neither.
//
// v7 also spelled the move as dup(f1|DUPFLG, f2) -- the PDP-11 kernel's two-argument
// dup with a flag bit in the high byte of the first argument, which is what dup2 was
// before it had a name.  libc has dup2 here (lib/libc/sys/dup.S) and the kernel's dup
// takes two arguments always, so say it directly.
//
void shrename(INT f1, INT f2)
{
    if (f1 != f2) {
        dup2(f1, f2);
        close(f1);
        if (f2 == 0)
            ioset |= 1;
    }
}

INT create(STRING s)
{
    INT rc;

    if ((rc = creat(s, 0666)) < 0)
        failed(s, badcreate);
    return rc;
}

INT tmpfil(void)
{
    itos(serial++);
    movstr(numbuf, tmpnam);
    return create(tmpout);
}

//
// Read a here-document out of the input and into a temp file.
//
void copy(IOPTR ioparg)
{
    CHAR c, *ends;
    CHAR *cline, *clinep;
    INT fd;
    IOPTR iop;

    if ((iop = ioparg) != 0) {
        copy(iop->iolst);
        ends = mactrim(iop->ioname);
        if (nosubst)
            iop->iofile &= ~IODOC;
        fd          = tmpfil();
        iop->ioname = cpystak(tmpout);
        iop->iolst  = iotemp;
        iotemp      = iop;
        cline       = locstak();

        for (;;) {
            clinep = cline;
            chkpr(NL);
            while (c = (nosubst ? readc() : nextc(*ends)), !eolchar(c)) {
                chkstak(clinep);
                *clinep++ = c;
            }
            chkstak(clinep);
            *clinep = 0;
            if (eof || eq(cline, ends))
                break;
            chkstak(clinep);
            *clinep++ = NL;
            write(fd, cline, clinep - cline);
        }
        close(fd);
    }
}
