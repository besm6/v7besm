/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The scanning engine behind scanf/fscanf/sscanf, v7's, with three changes forced
 * by the machine and one by C11.
 *
 *   THE ARGUMENT LIST IS A va_list.  v7 walked the caller's parameter block with an
 *   `int **' handed in by scanf(); <stdarg.h> is the only way to do that here.
 *
 *   AN ARGUMENT IS CARRIED AS A RAW WORD, not as a typed pointer.  A `char *' is a
 *   fat pointer and an `int *' is not, so the two cannot share a C type without one
 *   of them being re-decorated on the way through -- and a NULL that acquires the
 *   fat marker stops testing as null.  The word is read with va_arg(ap, int) and
 *   reinterpreted at the point of use, exactly as the printf engine does for %s;
 *   `store' says whether there is an argument at all, since %*d has none.
 *
 *   THE SIZE MODIFIERS COLLAPSE.  short == int == long == one word and float ==
 *   double == one word, so v7's switch on (scale<<4)|size has two arms and not six.
 *   `h', `l' and an upper-case conversion letter are still parsed, and still mean
 *   nothing.
 *
 *   THE CHARACTER-CLASS TABLE IS INDEXED SAFELY.  v7 wrote _sctab[getc(iop)] and
 *   then asked whether the value was EOF, reading _sctab[-1]; and _getccl walked a
 *   `char' subscript into a 128-entry table, which is out of bounds for anything
 *   above 0177.  Both are bounded here.
 *
 * _sctab is file-static because nothing outside reads it -- and %[ writes on it, so
 * a scan with a character class is not re-entrant.  That is v7's bargain too.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define SPC 01
#define STP 02

#define SHORT   0
#define REGULAR 1
#define LONG    2
#define INT     0
#define FLOAT   1

#define NUMBUF 64 /* longest run of digits _innum will keep */

static char _sctab[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, SPC, SPC, 0, 0, 0, 0, 0,   0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   0,   0, 0, 0, 0, SPC,
};

/*
 * A character class, `%[abc]' or `%[^abc]'.  The set is recorded in the STP bit of
 * _sctab and the cursor is returned just past the `]'.
 */
static const char *_getccl(const char *s)
{
    int c, t;

    t = 0;
    if (*s == '^') {
        t = 1;
        s++;
    }
    for (c = 0; c < 128; c++)
        if (t)
            _sctab[c] &= ~STP;
        else
            _sctab[c] |= STP;
    while ((c = *s++ & 0177) != ']') {
        if (c == 0)
            return s - 1; /* unterminated set: stop at the NUL */
        if (t)
            _sctab[c] |= STP;
        else
            _sctab[c] &= ~STP;
    }
    return s;
}

/* %c, %s and %[: copy characters through.  Returns 1 if anything was stored. */
static int _instr(int raw, int store, int type, int len, FILE *iop, int *eofptr)
{
    char *ptr;
    int ch, ignstp, ndone;

    *eofptr = 0;
    ptr     = store ? *(char **)&raw : NULL;
    ndone   = 0;
    if (type == 'c' && len == 30000)
        len = 1;

    /* %s skips leading white space; %c and %[ do not. */
    ignstp = type == 's' ? SPC : 0;
    for (;;) {
        ch = getc(iop);
        if (ch == EOF || (_sctab[ch] & ignstp) == 0)
            break;
    }

    ignstp = SPC;
    if (type == 'c')
        ignstp = 0;
    else if (type == '[')
        ignstp = STP;
    while (ch != EOF && (_sctab[ch] & ignstp) == 0) {
        if (store)
            *ptr++ = ch;
        ndone++;
        if (--len <= 0)
            break;
        ch = getc(iop);
    }
    if (ch != EOF) {
        if (len > 0)
            ungetc(ch, iop);
    } else
        *eofptr = 1;
    if (store && ndone) {
        if (type != 'c')
            *ptr = '\0';
        return 1;
    }
    return 0;
}

/* One conversion.  Returns 1 if a value was produced (whether or not stored). */
static int _innum(int raw, int store, int type, int len, int size, FILE *iop, int *eofptr)
{
    char numbuf[NUMBUF];
    char *np;
    int c, base, c1;
    int expseen, scale, negflg, ndigit;
    long lcval;

    if (type == 'c' || type == 's' || type == '[')
        return _instr(raw, store, type, len, iop, eofptr);

    lcval  = 0;
    ndigit = 0;
    scale  = INT;
    if (type == 'e' || type == 'f' || type == 'g')
        scale = FLOAT;
    base = 10;
    if (type == 'o')
        base = 8;
    else if (type == 'x')
        base = 16;
    np      = numbuf;
    expseen = 0;
    negflg  = 0;
    while ((c = getc(iop)) == ' ' || c == '\t' || c == '\n')
        ;
    if (c == '-') {
        negflg++;
        *np++ = c;
        c     = getc(iop);
        len--;
    } else if (c == '+') {
        len--;
        c = getc(iop);
    }
    for (; --len >= 0; c = getc(iop)) {
        if (np >= &numbuf[NUMBUF - 2])
            break;
        if (isdigit(c) || (base == 16 && (('a' <= c && c <= 'f') || ('A' <= c && c <= 'F')))) {
            ndigit++;
            if (base == 8)
                lcval <<= 3;
            else if (base == 10)
                lcval = ((lcval << 2) + lcval) << 1;
            else
                lcval <<= 4;
            c1 = c;
            if ('0' <= c && c <= '9')
                c -= '0';
            else if ('a' <= c && c <= 'f')
                c -= 'a' - 10;
            else
                c -= 'A' - 10;
            lcval += c;
            c = c1;
        } else if (c == '.') {
            if (base != 10 || scale == INT)
                break;
            ndigit++;
        } else if ((c == 'e' || c == 'E') && expseen == 0) {
            if (base != 10 || scale == INT || ndigit == 0)
                break;
            expseen++;
            *np++ = c;
            c     = getc(iop);
            if (c != '+' && c != '-' && ('0' > c || c > '9'))
                break;
        } else
            break;
        *np++ = c;
    }
    if (negflg)
        lcval = -lcval;
    if (c != EOF) {
        ungetc(c, iop);
        *eofptr = 0;
    } else
        *eofptr = 1;
    if (!store || np == numbuf)
        return 0;
    *np = 0;

    /*
     * One word per scalar, so `size' -- SHORT, REGULAR or LONG -- has nothing to
     * choose between.  All that is left is integer against floating.
     */
    (void)size;
    if (scale == FLOAT)
        **(double **)&raw = atof(numbuf);
    else
        **(int **)&raw = lcval;
    return 1;
}

int _doscan(FILE *iop, const char *fmt, va_list ap)
{
    int ch, ch1;
    int nmatch, len, fileended, size, store, raw;

    nmatch    = 0;
    fileended = 0;
    raw       = 0;
    for (;;)
        switch (ch = *fmt++) {
        case '\0':
            return nmatch;

        case '%':
            if ((ch = *fmt++) == '%')
                goto def;
            store = 1;
            if (ch == '*') {
                store = 0;
                ch    = *fmt++;
            } else
                raw = va_arg(ap, int);
            len  = 0;
            size = REGULAR;
            while (isdigit(ch)) {
                len = len * 10 + ch - '0';
                ch  = *fmt++;
            }
            if (len == 0)
                len = 30000;
            if (ch == 'l') {
                ch   = *fmt++;
                size = LONG;
            } else if (ch == 'h') {
                size = SHORT;
                ch   = *fmt++;
            } else if (ch == '[')
                fmt = _getccl(fmt);
            if (isupper(ch)) {
                ch   = tolower(ch);
                size = LONG;
            }
            if (ch == '\0')
                return -1;
            if (_innum(raw, store, ch, len, size, iop, &fileended) && store)
                nmatch++;
            if (fileended)
                return nmatch ? nmatch : -1;
            break;

        case ' ':
        case '\n':
        case '\t':
            while ((ch1 = getc(iop)) == ' ' || ch1 == '\t' || ch1 == '\n')
                ;
            if (ch1 != EOF)
                ungetc(ch1, iop);
            break;

        default:
        def:
            ch1 = getc(iop);
            if (ch1 != ch) {
                if (ch1 == EOF)
                    return nmatch ? nmatch : -1;
                ungetc(ch1, iop);
                return nmatch;
            }
        }
}
