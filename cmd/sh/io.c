/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// Input, output and file copying.
//
#include <fcntl.h>
#include <unistd.h>

#include "defs.h"

//
// Point the current input at descriptor fd, with an empty buffer.
//
// The buffer is a bufferful at a time normally, but ONE CHARACTER at a time when the
// input is a terminal or the shell is running a single command: reading ahead of what
// the user typed would swallow input a command about to be started is entitled to.
//
void initf(UFD fd)
{
    SHFILE f = standin;

    f->fdes = fd;
    f->fsiz = ((flags & (oneflg | ttyflg)) == 0 ? SHBUFSIZ : 1);
    f->fnxt = f->fend = f->fbuf;
    f->feval          = 0;
    f->flin           = 1;
    f->feof           = FALSE;
    f->fencd          = FALSE; // raw until somebody says otherwise -- see mode.h
}

//
// Point the current input at a string rather than a file.  This is how `eval', a trap
// command and command substitution read text that is already in memory: the descriptor
// is set to -1, and readc() takes characters straight out of s.  Returns true if there
// was nothing there.
//
INT estabf(STRING s)
{
    SHFILE f;

    (f = standin)->fdes = -1;
    f->fend             = length(s) + (f->fnxt = s);
    f->flin             = 1;
    return f->feof      = (s == 0);
}

//
// Start reading from af, remembering the input it interrupts so that pop() can go back.
// The inputs form a stack: a script that runs `.', which reads a file that uses `eval',
// is three deep.
//
void push(SHFILE af)
{
    SHFILE f;

    (f = af)->fstak = standin;
    f->feof         = 0;
    f->feval        = 0;
    f->fencd        = 0; // the block is usually a C-stack local: see mode.h
    standin         = f;
}

//
// Finish with the current input and go back to the one it interrupted, closing the file
// on the way.  False if there is nothing to go back to.
//
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

//
// Make a pipe -- a pair of descriptors, read end in pv[0] and write end in pv[1] -- or
// give up with "cannot make pipe".
//
void chkpipe(INT *pv)
{
    if (pipe(pv) < 0 || pv[INPIPE] < 0 || pv[OTPIPE] < 0)
        error(piperr);
}

//
// Open a file for reading, or give up with "cannot open".  The checking is the point:
// the caller never has to test the result.
//
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

//
// Create a file for writing, or give up with "cannot create".  Mode 0666 lets the user's
// umask decide the permissions.
//
INT create(STRING s)
{
    INT rc;

    if ((rc = creat(s, 0666)) < 0)
        failed(s, badcreate);
    return rc;
}

//
// Create the next here-document temp file and return its descriptor.  The name is
// "/tmp/sh" plus this shell's pid plus a serial number, so two shells and two
// here-documents in one shell cannot collide.
//
INT tmpfil(void)
{
    itos(serial++);
    movstr(numbuf, tmpnam);
    return create(tmpout);
}

//
// Read a here-document out of the input and into a temp file.
//
// A here-document is the `<<WORD' form: the lines that follow, up to one holding just
// WORD, become the command's standard input.  They are read now, at the end of the
// command line where the text starts, and stashed in /tmp -- the command has not been
// started yet and there is nowhere else to put them.
//
// The recursion at the top handles several here-documents on one line, in the order they
// were written.  Whether $ substitution happens in the text depends on whether WORD was
// quoted, which trim() reported in `nosubst'.
//
void copy(IOPTR ioparg)
{
    INT c;
    CHAR *ends;
    CHAR *cline, *clinep;
    BOOL literal;
    INT fd;
    IOPTR iop;

    if ((iop = ioparg) != 0) {
        copy(iop->iolst);
        ends    = mactrim(iop->ioname);
        literal = nosubst;
        if (literal)
            iop->iofile &= ~IODOC;
        fd          = tmpfil();
        iop->ioname = cpystak(tmpout);
        iop->iolst  = iotemp;
        iotemp      = iop;
        cline       = locstak();

        for (;;) {
            clinep = cline;
            chkpr(NL);
            while (c = (literal ? readc() : nextc(*ends)), !eolchar(c)) {
                //
                // THE TEMP FILE IS WRITTEN IN THE STORED FORM when it is going to be read
                // back by subst() -- which is exactly when the terminator was NOT quoted,
                // since a quoted one clears IODOC above and hands the file to the command
                // as it stands.  nextc() marks a backslash escape, and that mark has to
                // survive the round trip through the file or `\$x' in a <<EOF body starts
                // being substituted.  v7 could let it: its mark fitted in the byte, and it
                // took it off again with a `& 0177' in subst().
                //
                if (literal) {
                    chkstak(clinep);
                    *clinep++ = c;
                } else {
                    clinep = putq(clinep, c);
                }
            }
            chkstak(clinep);
            *clinep = 0;
            //
            // The terminator is compared against the RAW line, so a line the encoder
            // touched cannot match one -- which is right: `\EOF' is not `EOF'.  The
            // comparison was already this strict in v7, for the same reason.
            //
            if (eof || eq(cline, ends))
                break;
            chkstak(clinep);
            *clinep++ = NL;
            write(fd, cline, clinep - cline);
        }
        close(fd);
    }
}
