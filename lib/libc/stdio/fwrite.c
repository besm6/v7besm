// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

// See fread.c for why v7's rdwr.c is two files here, for the word-at-a-time
// transfer this direction mirrors, for why the count is flattened to size*count
// bytes, and for why the byte counts are `int'.  The only asymmetry is what _cnt
// counts: bytes available on a read stream, free space on a write stream.  Both
// make `_cnt >= 6' the right question.
//
// v7 asked ferror() once per item; this asks it once per byte actually pushed
// through putc.  For fwrite(buf, 1, n, f) -- the commonest call -- that is the
// same question at the same rate, and the word path skips it entirely, so the
// only place it is asked more often than v7 asked it is a transfer that cannot
// use whole words at all.  The byte that failed is not counted, which is what
// makes `bytes written / size' v7's count of complete items.
#include <stdio.h>

#define NBPW        6                           // bytes per word: sizeof(int)
#define WALIGNED(p) ((char *)(int *)(p) == (p)) // the round trip is the identity only at byte #0

//
// Move up to n bytes from *pp to the stream, advancing it, and answer how many
// were written.  Fewer than n means the stream went bad.
//
static int wrbytes(const char **pp, int n, FILE *iop)
{
    const char *p = *pp;
    int put       = 0;

    for (;;) {
        // See rdbytes(): a whole word to write, room for a whole word, and both
        // cursors on a word boundary.  A line-buffered, unbuffered or memory
        // stream is held at _cnt == 0 by design and so never arrives here, and
        // neither does a stream whose buffer has not been chosen yet.
        if (n - put >= NBPW && iop->_cnt >= NBPW && WALIGNED(p) && WALIGNED(iop->_ptr)) {
            const unsigned *s = (const unsigned *)p;
            unsigned *d       = (unsigned *)iop->_ptr;
            int left          = n - put;

            if (iop->_cnt < left)
                left = iop->_cnt; // bytes to write, or room in the buffer, whichever is less
            while (left >= NBPW) {
                *d++ = *s++;
                left -= NBPW;
                put += NBPW;
                iop->_cnt -= NBPW;
            }
            p         = (const char *)s;
            iop->_ptr = (char *)d;
            continue; // the buffer may want a flush before the next word
        }
        if (put >= n)
            break;
        putc(*p++, iop);
        if (ferror(iop))
            break;
        put++;
    }
    *pp = p;
    return put;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *iop)
{
    const char *p;
    int n, put;

    if (size == 0 || count == 0)
        return 0;
    p   = ptr;
    n   = size * count;
    put = wrbytes(&p, n, iop);
    if (put == n)
        return count;
    return put / (int)size; // short write: complete items only, as in v7
}
