//
// The formatting engine behind the whole printf family.
//
// NOT v7's.  v7's _doprnt was x86 assembly in the Nordier port (stdio/doprnt.s,
// with stdio/fltpr.s and ffltpr.s for the floating conversions), so there was
// nothing to port.  This is the c-compiler's libc/besm6/doprnt.c -- itself derived
// from the FreeBSD kernel printf -- retargeted here.  What changed:
//
//   THE SINK IS A `FILE *'.  The original chose between putbyte() and a caller
//   buffer through a g_to_buf flag; here everything goes through putc(), and
//   sprintf/snprintf hand in a v7 _IOSTRG stream over the caller's buffer.  That
//   buys snprintf's return value for nothing: _flsbuf() drops the byte when such a
//   stream fills up (stdio/flsbuf.c) while the count below goes on rising, which
//   is exactly "the number of characters that would have been written".
//
//   THE CASE FOLD IS GONE.  The original folded every conversion letter to upper
//   case because its output device buffered KOI7 and printed upper case only --
//   '%d' and '%D' both arrived as 'D', and %x/%X, %e/%E, %g/%G were
//   indistinguishable.  This terminal is ASCII (kernel/dev/sc.c), so the letters
//   are case-sensitive again: %x is lower-case hex and %X upper, %e/%g print `e'
//   and %E/%G print `E'.  A null %s prints "(null)", not "(NULL)".
//
//   %n was added, and the length modifiers widened to the C11 set.  `long' is
//   `int' and `double' is `float', one word each, so l/h/hh/ll/L/j/z/t are all
//   accepted and all ignored -- every argument is one word and va_arg steps by one
//   word whatever type it is named with.
//
// The one thing that must NOT be tidied is the null test in %s: the argument is
// read as a raw word and only then reinterpreted as a char *, because reading it
// back through char** would re-decorate a null word into a (nonzero) fat pointer
// and hide the null.
//
// g_iop and g_len are file statics, as the original's sink state was.  Nothing
// re-enters this engine today; when signal delivery lands (lib phase 6), a handler
// that calls printf will be the first thing that could.
//
#include <math.h>
#include <stdio.h>

enum {
    MAXNBUF  = 32, // digits buffer: 48-bit octal (16) + sign + prefix + slack
    MAX_DIG  = 12, // max meaningful significant digits for a 48-bit double
    DEF_PREC = 6,  // default precision for %f/%e and significant digits for %g
};

static FILE *g_iop; // where the characters go
static int g_len;   // characters produced so far -- the return value

static void emit(int c)
{
    putc(c, g_iop);
    ++g_len;
}

static void emit_pad(int c, int count)
{
    while (count > 0) {
        emit(c);
        --count;
    }
}

// Map a value 0..15 to its hex digit, in the case the conversion letter asked for.
static int mkhex(int ch, int upper)
{
    ch = ch & 15;
    if (ch > 9)
        return ch + (upper ? 'A' : 'a') - 10;
    return ch + '0';
}

//
// Convert `ul' to base `base' digits, written into nbuf in reverse order.
// nbuf[0] is a 0 sentinel; digits occupy nbuf[1..].  At least `prec' digits are
// produced (zero padded).  Returns a pointer to the most-significant digit (so
// the caller walks downward to nbuf+1 to print MSD..LSD) and stores the digit
// count in *lenp.
//
static char *ksprintn(char *nbuf, unsigned ul, int base, int prec, int upper, int *lenp)
{
    char *p = nbuf;

    *p = 0;
    do {
        *++p = (char)mkhex((int)(ul % (unsigned)base), upper);
        ul   = ul / (unsigned)base;
    } while (--prec > 0 || ul);
    if (lenp)
        *lenp = (int)(p - nbuf);
    return p;
}

//
// Floating-point conversion (defined at the bottom of the file).  Works on the
// caller buffer b[0..bsize-1], walking it with char* cursors.  Stores the
// digits, sets *startidx to the index of the first character, and returns the
// character count.  b[0] is reserved for a rounding carry.  fmtch is always one
// of 'f'/'e'/'g' -- lower case -- and expch is the letter an exponent takes.
//
static int cvt(double number, int prec, int sharpflag, int *negp, int fmtch, int expch, char *b,
               int bsize, int *startidx);

int _doprnt(const char *fmt, va_list ap, FILE *iop)
{
    char nbuf[MAXNBUF];
    int i, c, base, ladjust, sharpflag, neg, dot, upper;
    int n, width, dwidth, sign, blank, extrazeros, padding, dlen;
    unsigned ul;
    char *s, *msd;

    g_iop = iop;
    g_len = 0;

    i = 0;
    for (;;) {
        while ((c = fmt[i]) != '%') {
            if (!c)
                goto done;
            emit(c);
            ++i;
        }
        ++i;
        padding    = ' ';
        width      = 0;
        extrazeros = 0;
        ladjust    = 0;
        sharpflag  = 0;
        neg        = 0;
        sign       = 0;
        blank      = 0;
        dot        = 0;
        upper      = 0;
        dwidth     = -1;

    reswitch:
        c = fmt[i];
        ++i;
        if (c == '.') {
            dot    = 1;
            dwidth = 0;
            goto reswitch;
        }
        if (c == '#') {
            sharpflag = 1;
            goto reswitch;
        }
        if (c == '+') {
            sign = -1;
            goto reswitch;
        }
        if (c == ' ') {
            blank = 1;
            goto reswitch;
        }
        if (c == '-') {
            ladjust = 1;
            goto reswitch;
        }
        if (c == 'l' || c == 'h' || c == 'L' || c == 'j' || c == 'z' || c == 't') {
            // length modifiers: every argument is one word -- accept and ignore
            goto reswitch;
        }
        if (c == '*') {
            if (!dot) {
                width = va_arg(ap, int);
                if (width < 0) {
                    ladjust = 1;
                    width   = -width;
                }
            } else {
                dwidth = va_arg(ap, int);
            }
            goto reswitch;
        }
        if (c == '0' && !dot) {
            padding = '0';
            goto reswitch;
        }
        if (c >= '0' && c <= '9') {
            n = 0;
            for (;;) {
                n = n * 10 + c - '0';
                c = fmt[i];
                if (c < '0' || c > '9')
                    break;
                ++i;
            }
            if (dot)
                dwidth = n;
            else
                width = n;
            goto reswitch;
        }

        if (c == '%') {
            emit('%');
            continue;
        }

        if (c == 'c') {
            if (!ladjust)
                emit_pad(' ', width - 1);
            emit(va_arg(ap, int));
            if (ladjust)
                emit_pad(' ', width - 1);
            continue;
        }

        if (c == 'n') {
            n = va_arg(ap, int);
            if (n != 0)
                **(int **)&n = g_len;
            continue;
        }

        if (c == 's') {
            // Detect a null argument from the raw word, then reconstruct the
            // char* from it.  Reading it back through char** would re-decorate a
            // null word into a (nonzero) fat pointer and hide the null.
            n = va_arg(ap, int);
            if (n == 0) {
                s = "(null)";
            } else {
                s = *(char **)&n;
            }
            if (!dot) {
                n = 0;
                while (s[n])
                    ++n;
            } else {
                n = 0;
                while (n < dwidth && s[n])
                    ++n;
            }
            width -= n;
            if (!ladjust)
                emit_pad(' ', width);
            for (c = 0; c < n; ++c)
                emit(s[c]);
            if (ladjust)
                emit_pad(' ', width);
            continue;
        }

        // ---- floating point ----
        if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
            double d;
            int sidx, slen, expch;

            if (c >= 'A' && c <= 'Z') {
                upper = 1;
                c += 'a' - 'A';
            }
            expch = upper ? 'E' : 'e';
            d     = va_arg(ap, double);
            if (dwidth > MAX_DIG) {
                if (c != 'g' || sharpflag)
                    extrazeros = dwidth - MAX_DIG;
                dwidth = MAX_DIG;
            } else if (dwidth == -1) {
                dwidth = DEF_PREC;
            }
            if (d < 0) {
                neg = 1;
                d   = -d;
            }
            nbuf[0] = 0;
            slen    = cvt(d, dwidth, sharpflag, &neg, c, expch, nbuf, MAXNBUF, &sidx);
            dlen    = slen + (neg ? 1 : 0);
            if (!ladjust && padding == ' ' && (width - dlen) > 0)
                emit_pad(' ', width - dlen);
            if (neg)
                emit('-');
            if (!ladjust && padding == '0' && (width - dlen) > 0)
                emit_pad('0', width - dlen);
            for (n = 0; n < slen; ++n) {
                if (extrazeros && nbuf[sidx + n] == expch) {
                    emit_pad('0', extrazeros);
                    extrazeros = 0;
                }
                emit(nbuf[sidx + n]);
            }
            if (extrazeros)
                emit_pad('0', extrazeros);
            if (ladjust && (width - dlen) > 0)
                emit_pad(' ', width - dlen);
            continue;
        }

        // ---- integer conversions ----
        if (c == 'd' || c == 'i') {
            ul = va_arg(ap, unsigned);
            if (!sign)
                sign = 1;
            base = 10;
            goto number;
        }
        if (c == 'u') {
            ul   = va_arg(ap, unsigned);
            base = 10;
            goto nosign;
        }
        if (c == 'o') {
            ul   = va_arg(ap, unsigned);
            base = 8;
            goto nosign;
        }
        if (c == 'x' || c == 'X') {
            ul    = va_arg(ap, unsigned);
            upper = (c == 'X');
            base  = 16;
            goto nosign;
        }
        if (c == 'p') {
            ul        = va_arg(ap, unsigned);
            base      = 16;
            sharpflag = 1;
            goto nosign;
        }

        // unknown conversion: echo it verbatim
        emit('%');
        emit(c);
        continue;

    nosign:
        sign  = 0;
        blank = 0;
    number:
        //
        // §7.21.6.1p6: for d,i,o,u,x,X a precision makes the `0' flag ignored.  Only
        // for those -- %010.2f keeps its zero fill -- which is why the test is here
        // and not, as in the engine this came from, in the `.' case of the flag
        // parser, where it silently disarmed the floating conversions too.
        //
        if (dot)
            padding = ' ';
        if (sign) {
            if ((int)ul < 0) {
                neg = '-';
                ul  = (unsigned)(-(int)ul);
            } else if (sign < 0) {
                neg = '+';
            } else if (blank) {
                neg = ' ';
            }
        }
        if (dwidth >= MAXNBUF) {
            extrazeros = dwidth - MAXNBUF + 1;
            dwidth     = MAXNBUF - 1;
        }
        msd = ksprintn(nbuf, ul, base, dwidth, upper, &dlen);
        if (sharpflag && ul != 0) {
            if (base == 8)
                dlen += 1;
            else if (base == 16)
                dlen += 2;
        }
        if (neg)
            ++dlen;

        if (!ladjust && padding == ' ' && (width - dlen) > 0)
            emit_pad(' ', width - dlen);
        if (neg)
            emit(neg);
        if (sharpflag && ul != 0) {
            if (base == 8) {
                emit('0');
            } else if (base == 16) {
                emit('0');
                emit(upper ? 'X' : 'x');
            }
        }
        if (extrazeros)
            emit_pad('0', extrazeros);
        if (!ladjust && padding == '0' && (width - dlen) > 0)
            emit_pad('0', width - dlen);
        while (msd > nbuf)
            emit(*msd--);
        if (ladjust && (width - dlen) > 0)
            emit_pad(' ', width - dlen);
        continue;
    }

done:
    return g_len;
}

//
// Round the decimal digits start..end up by one unit in the last place,
// propagating carries.  `expo' (when non-null) carries the e-format exponent so
// a carry out of the leading digit bumps it; otherwise an f-format carry extends
// left into the reserved slot and moves *startp back one.  Mirrors the FreeBSD
// cvtround.
//
static void cvtround(double fract, int *expo, char **startp, char *end, int ch, int *negp)
{
    double tmp;
    char *start, *p;
    int up;

    start = *startp;
    p     = end;

    if (fract) {
        modf(fract * 10, &tmp);
        up = (int)tmp;
    } else {
        up = ch - '0';
    }
    if (up > 4) {
        for (;; --p) {
            if (*p == '.')
                --p;
            ++*p;
            if (*p <= '9')
                break;
            *p = '0';
            if (p == start) {
                if (expo) { // e/E: increment exponent
                    *p = '1';
                    ++*expo;
                } else { // f: prepend a digit into the reserved slot
                    --p;
                    *p = '1';
                    --start;
                }
                break;
            }
        }
    } else if (*negp) {
        // "%.3f" of -0.0004 must not print a negative zero
        for (;; --p) {
            if (*p == '.')
                --p;
            if (*p != '0')
                break;
            if (p == start)
                *negp = 0;
        }
    }
    *startp = start;
}

// Append the exponent suffix ("e+NN") at write cursor p; return the new cursor.
static char *exponent(char *p, int expin, int expch)
{
    char eb[8];
    int expo, k;

    expo = expin;

    *p++ = (char)expch;
    if (expo < 0) {
        expo = -expo;
        *p++ = '-';
    } else {
        *p++ = '+';
    }
    k = 8;
    if (expo > 9) {
        do {
            --k;
            eb[k] = (char)(expo % 10 + '0');
            expo  = expo / 10;
        } while (expo > 9);
        --k;
        eb[k] = (char)(expo + '0');
        for (; k < 8; ++k)
            *p++ = eb[k];
    } else {
        *p++ = '0';
        *p++ = (char)(expo + '0');
    }
    return p;
}

//
// Format the magnitude `number' into b[] for %f/%e/%g.  Walks the buffer with
// char* cursors.  b[0] is reserved for a rounding carry; formatting runs from
// b+1.  *startidx receives the index of the first character (0 if a carry
// extended left); the character count is returned.
//
static int cvt(double number, int precin, int sharpflag, int *negp, int fmtch, int expch, char *b,
               int bsize, int *startidx)
{
    double fract, integer, tmp;
    char *p, *t, *start, *endp;
    int expcnt, gformat, dotrim, prec;

    prec    = precin;
    expcnt  = 0;
    gformat = 0;
    dotrim  = 0;
    fract   = modf(number, &integer);

    endp  = b + bsize - 1;
    start = b + 1; // reserved rounding slot at b[0]
    t     = b + 1;

    // integer part, least-significant first, into the top of the buffer
    p = endp - 1;
    while (integer) {
        tmp = modf(integer / 10, &integer);
        *p  = (char)((int)((tmp + 0.01) * 10) + '0');
        --p;
        ++expcnt;
    }

    if (fmtch == 'f') {
        if (expcnt) {
            ++p;
            while (p < endp)
                *t++ = *p++;
        } else {
            *t++ = '0';
        }
        if (prec || sharpflag)
            *t++ = '.';
        if (fract) {
            if (prec) {
                do {
                    fract = modf(fract * 10, &tmp);
                    *t++  = (char)((int)tmp + '0');
                } while (--prec && fract);
            }
            if (fract)
                cvtround(fract, 0, &start, t - 1, '0', negp);
        }
        for (; prec > 0; --prec)
            *t++ = '0';
        *startidx = (int)(start - b);
        return (int)(t - start);
    }

    if (fmtch == 'e') {
    eformat:
        if (expcnt) {
            ++p;
            *t++ = *p;
            if (prec || sharpflag)
                *t++ = '.';
            for (;;) {
                if (!prec)
                    break;
                ++p;
                if (p >= endp)
                    break;
                *t++ = *p;
                --prec;
            }
            if (!prec) {
                ++p;
                if (p < endp) {
                    fract = 0;
                    cvtround(0, &expcnt, &start, t - 1, *p, negp);
                }
            }
            --expcnt;
        } else if (fract) {
            for (expcnt = -1;; --expcnt) {
                fract = modf(fract * 10, &tmp);
                if (tmp)
                    break;
            }
            *t++ = (char)((int)tmp + '0');
            if (prec || sharpflag)
                *t++ = '.';
        } else {
            *t++ = '0';
            if (prec || sharpflag)
                *t++ = '.';
        }
        if (fract) {
            if (prec) {
                do {
                    fract = modf(fract * 10, &tmp);
                    *t++  = (char)((int)tmp + '0');
                } while (--prec && fract);
            }
            if (fract)
                cvtround(fract, &expcnt, &start, t - 1, '0', negp);
        }
        for (; prec > 0; --prec)
            *t++ = '0';
        if (gformat && !sharpflag) {
            while (t > start) {
                --t;
                if (*t != '0')
                    break;
            }
            if (*t == '.')
                --t;
            ++t;
        }
        t         = exponent(t, expcnt, expch);
        *startidx = (int)(start - b);
        return (int)(t - start);
    }

    // fmtch == 'g'
    if (!prec)
        ++prec;
    if (expcnt > prec || (!expcnt && fract && fract < 0.0001)) {
        --prec;
        gformat = 1;
        goto eformat;
    }
    if (expcnt) {
        for (;;) {
            ++p;
            if (p >= endp)
                break;
            *t++ = *p;
            --prec;
        }
    } else {
        *t++ = '0';
    }
    if (prec || sharpflag) {
        dotrim = 1;
        *t++   = '.';
    }
    while (prec && fract) {
        fract = modf(fract * 10, &tmp);
        *t++  = (char)((int)tmp + '0');
        --prec;
    }
    if (fract)
        cvtround(fract, 0, &start, t - 1, '0', negp);
    if (sharpflag) {
        for (; prec > 0; --prec)
            *t++ = '0';
    } else if (dotrim) {
        while (t > start) {
            --t;
            if (*t != '0')
                break;
        }
        if (*t != '.')
            ++t;
    }
    *startidx = (int)(start - b);
    return (int)(t - start);
}
