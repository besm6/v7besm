// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// v7 keeps fread and fwrite in one file (stdio/rdwr.c); they are split here
// because they share nothing, and one function per file is what lets b6ranlib's
// index pull only the one a program calls.
//
// THE TRANSFER GOES A WORD AT A TIME WHERE IT CAN, which is the one thing this
// file adds to v7.  Six chars pack into a word here, so a byte-wise move costs a
// fat-pointer load out of the stream buffer and a b$stb read-modify-write into
// the caller's, per byte; one `unsigned' assignment moves all six.  It is legal
// only where BOTH cursors sit on a word boundary -- the caller's buffer and the
// stream's -- and alignment is testable in C exactly as lib/libc/gen/qsort.c
// tests it, and for the same reason.  Cursors that are mutually misaligned can
// never use it and stay on getc, which is correct, just no faster than v7.
//
// THE TRANSFER IS FLATTENED to size*count bytes rather than v7's loop of `count'
// items of `size' bytes, and that is not a tidying: the commonest call in the
// tree is fread(buf, 1, n, f) -- cmd/strip's segment copy is one -- where a
// per-item move would be handed ONE byte at a time and could never reach a whole
// word.  The two are equivalent because the bytes are contiguous either way: both
// consume until EOF, and `bytes moved / size' is v7's count of COMPLETE items,
// with a partial one consumed but not counted.  The divide happens only on the
// short read, never on the normal path.
//
// THE BYTE COUNTS ARE `int', NOT size_t.  A size_t is unsigned, and this compiler
// lowers EVERY relational out of line -- b$uge for the unsigned, b$ge for the
// signed -- so the type decides which helper the loop calls, not whether it calls
// one.  An `int' is 41 bits and a transfer is bounded by a 15-bit address space,
// so nothing is lost.  The inner loop is down to a single relational per word by
// clamping once, ahead of it, to whichever of `wanted' and `buffered' is less.
//
#include <stdio.h>

#define NBPW        6                           // bytes per word: sizeof(int)
#define WALIGNED(p) ((char *)(int *)(p) == (p)) // the round trip is the identity only at byte #0

//
// Move up to n bytes from the stream to *pp, advancing it, and answer how many
// arrived.  Fewer than n means EOF or an error.
//
static int rdbytes(char **pp, int n, FILE *iop)
{
    char *p = *pp;
    int got = 0;
    int c;

    for (;;) {
        // A whole word wanted, a whole word buffered, both cursors aligned: no
        // refill and no EOF can fall inside the inner loop, so it needs neither
        // test.  _cnt is read before _ptr is touched, which is what keeps a
        // stream that has no buffer yet out of here.
        if (n - got >= NBPW && iop->_cnt >= NBPW && WALIGNED(p) && WALIGNED(iop->_ptr)) {
            unsigned *d = (unsigned *)p;
            unsigned *s = (unsigned *)iop->_ptr;
            int left    = n - got;

            if (iop->_cnt < left)
                left = iop->_cnt; // bytes wanted, or bytes buffered, whichever is less
            while (left >= NBPW) {
                *d++ = *s++;
                left -= NBPW;
                got += NBPW;
                iop->_cnt -= NBPW;
            }
            p         = (char *)d;
            iop->_ptr = (char *)s;
            continue; // the buffer may want a refill before the next word
        }
        if (got >= n || (c = getc(iop)) < 0)
            break;
        *p++ = c;
        got++;
    }
    *pp = p;
    return got;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *iop)
{
    char *p;
    int n, got;

    if (size == 0 || count == 0)
        return 0;
    p   = ptr;
    n   = size * count;
    got = rdbytes(&p, n, iop);
    if (got == n)
        return count;
    return got / (int)size; // short read: complete items only, as in v7
}
