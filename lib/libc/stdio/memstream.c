//
// open_memstream -- a stream whose sink is a growing heap buffer (POSIX.1-2008).
// Neither C11 nor v7.  cmd/cpp is the first caller: it captures an isolated macro
// argument prescan through one (cmd/cpp/macro.c) and reads the buffer back after
// fclose.
//
// WHY IT IS AN _IOSTRG STREAM.  In this library that bit means "no descriptor behind
// this FILE", which is exactly true here, and it is the bit the two routines that
// would otherwise go wrong already read: fflush() skips such a stream, and fclose()
// neither close()s a descriptor nor free()s the base -- which is what has to happen,
// the buffer being the caller's to free.  Both answers come out right with no new
// code in flsbuf.o, the object every program that prints anything links.  _IOMEM is
// the second bit, saying WHICH kind of descriptorless stream this is: sprintf's
// fills a fixed caller buffer and drops the byte on overflow (vsnprintf.c), this one
// grows.  The one thing _IOSTRG does NOT give away free is fclose()'s clearing of
// _base -- see the arm there, and lib/test/stdiot.c's slot-reuse case.
//
// THE STREAM IS HELD AT _cnt == 0, the same trick line buffering uses (stdio.h), so
// every putc() misses into _flsbuf() and this file sees every single byte.  Nothing
// here has to reason about what putc() may have stored behind its back, and the
// buffer pointer and the length can be republished on EVERY byte rather than at
// flush time.  That is what lets fflush() stay the no-op _IOSTRG already makes it:
// POSIX promises the two values after fflush() or fclose(), and keeping them always
// current is a strictly stronger promise bought for a store apiece.
//
// THE HOOK IS A POINTER, armed here and tested in flsbuf.c, for the reason cuexit.c
// gives about _cleanup: b6ld pulls an archive member for an undefined symbol, so
// naming memputc() from flsbuf.c would put this file's code and its table in every
// program that calls printf().  It is 245 words against one word of bss and two
// instructions inside a branch a file stream never takes.
//
// THE CALLER'S TWO POINTERS LIVE IN A SIDE TABLE, because FILE has no spare member
// and widening it would cost _NFILE words of bss in every program.  The table is
// indexed by the _iob slot and _file carries the index, which is filbuf.c's
// smallbuf[fileno(iop)] pattern -- a small static array keyed by an integer the FILE
// already holds.  Three consequences worth stating:
//
//   THE INDEX IS BIASED BY _NFILE, so _file is 20..39.  NOFILE == _NFILE == 20
//   (sys/param.h), so that can never be a live descriptor and a stray write() or
//   lseek() on a memory stream fails with EBADF instead of reaching fd 0, 1 or 2.
//   It also stays under 128 and so round-trips through the `char' member whatever
//   its signedness -- plain char is unsigned here (doc/Besm6_Data_Representation.md),
//   which is why vsnprintf's _file = -1 would not do.
//
//   AN ENTRY NEEDS NO FREEING.  A slot is occupied exactly while its FILE carries
//   _IOMEM, and fclose() clears that bit.  Keying the table by the _iob slot rather
//   than by a list of its own is what buys that.
//
//   THE SLOT IS FOUND WITH AN INDEXED LOOP rather than by _findiop(), because the
//   index is the thing wanted: FILE * minus FILE * would be a division by
//   sizeof(FILE) == 6 words, and on the per-byte path at that.
//
// NOT SUPPORTED, and each for a reason: fseek and rewind (there is no descriptor to
// seek, and rewind is void and could not report that), ungetc (the stream is
// write-only, and its pushback arm steps _ptr back and sets _cnt, which would send
// the next putc down the fast path), and setvbuf/setbuf (they would replace the
// buffer the caller has been handed).  ftell IS supported, in ftell.c.  See
// lib/libc/man/fopen.3s.
//
#include <stdio.h>
#include <stdlib.h>

extern int (*_memputc)(int, FILE *);

// The caller's two output parameters, one pair per _iob slot.
static char **membufp[_NFILE];
static size_t *memsizep[_NFILE];

//
// The initial capacity, in bytes.  NOT BUFSIZ, which is 3072 bytes -- 512 words, a
// fifty-sixth of the address space -- for a stream cmd/cpp opens once per macro
// argument to collect a few dozen characters.  Eight words, which is a whole number
// of them, so malloc's rounding wastes nothing; the doubling below reaches BUFSIZ in
// six more steps.
//
#define MEMSTART 48

//
// Double the buffer, keeping `len' bytes and room for the NUL.
//
// A FAILURE LOSES THE BUFFER, and that is realloc's doing rather than a choice made
// here: this realloc free()s the block before it allocates (gen/malloc.c, and
// man/malloc.3 documents the compaction that depends on it), so there is nothing
// left to fall back on and the pointer already published to the caller is dead.  So
// it is unpublished -- a null buffer and a zero length, which free() accepts as a
// no-op and which a caller sees for what it is through ferror() or *sizep.  _base is
// left null, and memputc() refuses every later byte on that: a zero _bufsiz would
// otherwise send the next call back here to realloc(NULL, 0), which is malloc(0) --
// a one-word block to write a stream into.
//
static int grow(FILE *iop, int i, int len)
{
    char *base;
    int cap;

    cap  = iop->_bufsiz * 2;
    base = realloc(iop->_base, (size_t)cap);
    if (base == NULL) {
        iop->_base   = NULL;
        iop->_ptr    = NULL;
        iop->_bufsiz = 0;
        *membufp[i]  = NULL;
        *memsizep[i] = 0;
        iop->_flag |= _IOERR;
        return EOF;
    }
    iop->_base   = base;
    iop->_ptr    = base + len;
    iop->_bufsiz = cap;
    return 0;
}

//
// _flsbuf()'s memory-stream arm: append one byte, grow when full, republish.  It
// answers as _flsbuf() itself does -- the byte, or EOF with _IOERR set, which is
// what stops fwrite()'s loop.  _cnt was already put back to zero by the caller.
//
// The NUL is written after every byte and counted in neither *sizep nor the capacity
// test: POSIX promises the buffer is a string as well as a byte count, so the
// capacity always keeps room for one -- hence `len + 1 >= _bufsiz' and not `len'.
//
static int memputc(int c, FILE *iop)
{
    int i, len;

    if (iop->_base == NULL) // a growth failure has already killed this stream
        return EOF;

    i   = iop->_file - _NFILE;
    len = iop->_ptr - iop->_base;
    if (len + 1 >= iop->_bufsiz && grow(iop, i, len) == EOF)
        return EOF;

    *iop->_ptr++ = c;
    *iop->_ptr   = '\0';
    *membufp[i]  = iop->_base;
    *memsizep[i] = len + 1;
    return c & 0377;
}

//
// The buffer is allocated up front rather than on the first byte, so that a stream
// nothing is ever written to still leaves *bufp a real malloc'd empty string and
// *sizep zero, as POSIX requires -- and so that the one failure a caller can act on
// is reported by the NULL return it already has to test for.
//
FILE *open_memstream(char **bufp, size_t *sizep)
{
    FILE *iop;
    char *base;
    int i;

    // _findiop()'s predicate, written with the index the table is keyed by.
    for (i = 0;; i++) {
        if (i >= _NFILE)
            return NULL;
        iop = &_iob[i];
        if ((iop->_flag & (_IOREAD | _IOWRT | _IORW)) == 0)
            break;
    }

    base = malloc((size_t)MEMSTART);
    if (base == NULL)
        return NULL;
    *base = '\0';

    iop->_base   = base;
    iop->_ptr    = base;
    iop->_bufsiz = MEMSTART;
    iop->_cnt    = 0;                         // held there: every putc must miss into _flsbuf
    iop->_flag   = _IOWRT | _IOSTRG | _IOMEM; // an assignment: the slot was free
    iop->_file   = _NFILE + i;                // never a live descriptor: NOFILE == _NFILE

    membufp[i]  = bufp;
    memsizep[i] = sizep;
    *bufp       = base;
    *sizep      = 0;

    _memputc = memputc;
    return iop;
}
