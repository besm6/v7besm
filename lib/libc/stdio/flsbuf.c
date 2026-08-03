// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Drain a write buffer, and the three routines that share the draining: fflush(),
// fclose() and the _cleanup() that exit() runs.  v7 keeps them in one file for the
// same reason.
//
// _flsbuf() is what the putc() macro falls out to when the count runs out, and it
// is where all four buffering modes are decided.  It was restructured from v7's
// chain of gotos because there is a mode v7 did not have:
//
//   _IOSTRG   a stream with no descriptor behind it, of which there are two kinds.
//             sprintf's sink is a FIXED caller buffer, and when it is full the byte
//             is DROPPED and the count left at zero so every later putc lands here
//             too -- _doprnt() goes on counting characters it could not store, which
//             is exactly the value C11 wants snprintf to return.  A memory stream
//             carries _IOMEM as well and GROWS instead; open_memstream() holds it at
//             _cnt == 0 so that every one of its bytes arrives here, and the arm it
//             is handed to lives in stdio/memstream.c.
//
//   _IOUNBUF  one write() per byte.  stderr, always; anything malloc() could not
//             find a buffer for; and whatever setvbuf(_IONBF) says.
//
//   _IOLBUF   line buffered, which v7 had no notion of.  The stream is held at
//             _cnt == 0 so that EVERY putc misses and arrives here; this appends
//             the byte and writes the line out on '\n' or a full buffer.  Only the
//             slow path pays, and it is the mode stdout takes on a terminal --
//             where v7 went fully unbuffered and spent a syscall per character.
//
//   else      fully buffered: write what has accumulated, then start the buffer
//             over with c.  This is v7's path and the common one, since a
//             redirected stdout is not a terminal.
//
// _IOUNBUF is v7's _IONBF: same bit, new name, because C11 wanted _IONBF for a
// setvbuf mode (include/stdio.h).
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char _sobuf[];
extern FILE *_lastbuf;

//
// exit()'s flush hook, defined in gen/cuexit.c and null until something below arms
// it.  That indirection is what keeps a program that never writes through a FILE
// from linking any of this; the reasoning is written out in cuexit.c.
//
extern void (*_cleanup_hook)(void);

//
// _flsbuf()'s memory-stream arm, defined here because this is where it is TESTED and
// null until open_memstream() arms it -- the same bargain cuexit.c makes for
// _cleanup, and for the same reason: naming stdio/memstream.c from this file would
// put its code and its _NFILE-entry side table in every program that ever calls
// printf().  A stream carrying _IOMEM has been through open_memstream(), which arms
// this before it returns, so there is no null case to spend a test on.
//
int (*_memputc)(int, FILE *);

void _cleanup(void); // defined at the foot of this file

//
// Hand `n' buffered bytes to the kernel and rewind the buffer.  Returns 0, or EOF
// with _IOERR set -- a short write is an error here, as it is in v7: stdio has no
// way to tell the caller which putc() failed.
//
static int drain(FILE *iop, char *base, int n)
{
    iop->_ptr = base;
    if (n > 0 && write(fileno(iop), base, n) != n) {
        iop->_flag |= _IOERR;
        return EOF;
    }
    return 0;
}

int _flsbuf(int c, FILE *iop)
{
    char *base;
    char c1;

    //
    // Arm exit()'s flush.  Here and nowhere else: every buffered write in the
    // library reaches this routine on the first character -- putc misses while
    // _cnt is zero, and printf, puts, fputs and fwrite all go through putc -- so
    // by the time any stream holds data that would need flushing, the hook is set.
    // Reading through a FILE arms nothing, and needs nothing: there is no unwritten
    // data behind a read buffer, and the kernel closes the descriptors anyway.
    //
    _cleanup_hook = _cleanup;

    if (iop->_flag & _IORW) {
        iop->_flag |= _IOWRT;
        iop->_flag &= ~_IOEOF;
    }

    if (iop->_flag & _IOSTRG) {
        iop->_cnt = 0;
        //
        // Which kind of descriptorless stream: sprintf's drops the byte, a memory
        // stream grows to take it.  The test rides inside a branch a fully buffered
        // stream never reaches, so the common path pays nothing for it.
        //
        if (iop->_flag & _IOMEM)
            return (*_memputc)(c, iop);
        return EOF;
    }

    //
    // First write on this stream: choose the buffer, and with it the mode.  stdout
    // gets the static _sobuf so that the commonest stream in the system costs no
    // malloc, and becomes line buffered when it is talking to a terminal.
    //
    if (iop->_base == NULL && (iop->_flag & _IOUNBUF) == 0) {
        if (iop == stdout) {
            iop->_base   = _sobuf;
            iop->_bufsiz = BUFSIZ;
            if (isatty(fileno(iop)))
                iop->_flag |= _IOLBUF;
        } else if ((iop->_base = malloc(BUFSIZ)) == NULL) {
            iop->_flag |= _IOUNBUF;
        } else {
            iop->_bufsiz = BUFSIZ;
            iop->_flag |= _IOMYBUF;
        }
        iop->_ptr = iop->_base;
        iop->_cnt = 0;
    }

    if (iop->_flag & _IOUNBUF) {
        c1        = c;
        iop->_cnt = 0;
        if (write(fileno(iop), &c1, 1) != 1) {
            iop->_flag |= _IOERR;
            return EOF;
        }
        return c & 0377;
    }

    base = iop->_base;

    if (iop->_flag & _IOLBUF) {
        *iop->_ptr++ = c;
        iop->_cnt    = 0;
        if (c == '\n' || iop->_ptr >= base + iop->_bufsiz)
            if (drain(iop, base, iop->_ptr - base) == EOF)
                return EOF;
        return c & 0377;
    }

    if (drain(iop, base, iop->_ptr - base) == EOF) {
        iop->_cnt = 0;
        return EOF;
    }
    iop->_cnt    = iop->_bufsiz - 1;
    *iop->_ptr++ = c;
    return c & 0377;
}

int fflush(FILE *iop)
{
    char *base;
    int r;

    // §7.21.5.2: a null stream flushes every one of them.  Not a v7 idea.
    if (iop == NULL) {
        r = 0;
        for (iop = _iob; iop < _lastbuf; iop++)
            if (fflush(iop) == EOF)
                r = EOF;
        return r;
    }

    if ((iop->_flag & (_IOUNBUF | _IOSTRG | _IOWRT)) == _IOWRT && (base = iop->_base) != NULL) {
        if (drain(iop, base, iop->_ptr - base) == EOF)
            return EOF;
        //
        // A line-buffered stream stays at zero -- that is the whole trick, and
        // restoring a real count here would send the next putc down the fast path
        // and lose the newline flush.
        //
        if ((iop->_flag & _IOLBUF) == 0)
            iop->_cnt = iop->_bufsiz;
    }
    return 0;
}

int fclose(FILE *iop)
{
    int r;

    r = EOF;
    if (iop->_flag & _IOMEM) {
        //
        // A memory stream owns no descriptor, and its buffer is the CALLER's -- who
        // is about to read it and free it.  So there is nothing to close and nothing
        // to free, and everything writable was published as it was written.
        //
        // But the pointers MUST be cleared, and that is not tidiness.  This slot is
        // about to become reusable; _endopen() sets only _cnt, _file and _flag, and
        // _flsbuf() picks a buffer only when _base is null.  A later fopen() landing
        // on this slot would therefore keep the buffer just handed away, drain the
        // memory stream's stale bytes into its own file, and go on writing into
        // storage the caller has since freed.  No _IOSTRG stream could reach here
        // before open_memstream(): sprintf's lives on the stack (vsnprintf.c) and is
        // never an _iob slot.  lib/test/stdiot.c has the regression case.
        //
        iop->_base   = NULL;
        iop->_ptr    = NULL;
        iop->_bufsiz = 0;
        r            = 0;
    } else if (iop->_flag & (_IOREAD | _IOWRT | _IORW) && (iop->_flag & _IOSTRG) == 0) {
        r = fflush(iop);
        if (close(fileno(iop)) < 0)
            r = EOF;
        if (iop->_flag & _IOMYBUF)
            free(iop->_base);
        if (iop->_flag & (_IOMYBUF | _IOUNBUF))
            iop->_base = NULL;
    }
    iop->_flag &= ~(_IOREAD | _IOWRT | _IOUNBUF | _IOLBUF | _IOMYBUF | _IOERR | _IOEOF | _IOSTRG |
                    _IORW | _IOMEM);
    iop->_cnt = 0;
    return r;
}

//
// Flush every stream on the way out.  exit() calls this and _exit() does not,
// which is the whole difference between them (gen/cuexit.c).
//
void _cleanup(void)
{
    FILE *iop;

    for (iop = _iob; iop < _lastbuf; iop++)
        fclose(iop);
}
