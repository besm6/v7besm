// Copyright (c) 1990, 1993 The Regents of the University of California.
// See /COPYRIGHT or www.tuhs.org for details.

//
// strtol — convert the initial portion of a string to long (C11 §7.22.1.4).
//
// v7 had no strtol; this is 4.4BSD's, whose scanner and cutoff/cutlim overflow
// test carry over unchanged.  What could not carry over is everything that
// depends on the width and the REPRESENTATION of this machine's integers
// (doc/Besm6_Data_Representation.md):
//
//   THE LIMITS ARE ASYMMETRIC AND NARROW.  `long' is `int' -- one word, 41 bits
//   signed -- so LONG_MAX is 2^40-1 and LONG_MIN is -2^40, while an `unsigned
//   long' is the full 48 bits.  The accumulator is unsigned, as BSD's is, and
//   has room for either limit with plenty to spare.
//
//   NO SIGNED VALUE IS EVER REINTERPRETED AS UNSIGNED.  An integer here is a
//   41-bit two's complement field with bits 48..42 ZERO, so the raw word of a
//   negative value is not its 48-bit two's complement, and BSD's
//   `-(unsigned long)LONG_MIN' and `acc = neg ? LONG_MIN : LONG_MAX' (a negative
//   stored into an unsigned long and returned as a long) would depend on how the
//   front end widens a sign.  Both are rewritten so that every cast is
//   value-preserving: the negative cutoff is (unsigned long)LONG_MAX + 1, which
//   is 2^40 computed entirely in unsigned arithmetic, and the result is built as
//   a signed long at the end rather than round-tripped through acc.  LONG_MIN
//   itself is returned as the constant, never as -(long)acc: acc is 2^40 there,
//   which no `long' can hold.
//
//   THE CTYPE MACROS ARE NOT APPLIED TO A RAW BYTE.  `char' is unsigned and the
//   table has 129 entries (gen/ctype_.c), so a byte of 128..255 runs off its end;
//   the white-space skip guards with isascii(), and the digit decode is written
//   as explicit range tests, which are in range for any byte and are what atoi()
//   uses next door.
//
// Two places where BSD is not what C11 asks for, both fixed here:
//
//   "0x" WITH NO HEX DIGIT AFTER IT.  BSD consumes the prefix, finds nothing, and
//   reports the whole input unconsumed.  §7.22.1.4p4 wants the longest initial
//   subject sequence, which is "0": the value is 0 and *endptr points at the `x'.
//   `stop' below is where *endptr goes when no digit is consumed at all, and the
//   prefix scan moves it to that `x'.  It is a pointer to a character rather than
//   a null-or-not flag on purpose: a null char * is a word of zero and not a fat
//   pointer, and this way nothing has to test one.
//
//   AN OUT-OF-RANGE BASE is undefined in C11 and BSD scans on with it.  POSIX
//   permits EINVAL, which is more use to a caller than a nonsense answer.
//
// errno is otherwise untouched -- never cleared -- as C11 and BSD both leave it.
//
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

long strtol(const char *nptr, char **endptr, int base)
{
    const char *s    = nptr;
    const char *stop = nptr;
    unsigned long acc, cutoff;
    int c, cutlim;
    int neg = 0, any;

    if (base != 0 && (base < 2 || base > 36)) {
        errno = EINVAL;
        if (endptr != 0)
            *endptr = (char *)nptr;
        return 0;
    }

    // Skip white space and pick up a leading sign, if any.  With base 0, allow
    // 0x for hex and a leading 0 for octal; with base 16, allow 0x as well.
    do {
        c = *s++;
    } while (isascii(c) && isspace(c));
    if (c == '-') {
        neg = 1;
        c   = *s++;
    } else if (c == '+') {
        c = *s++;
    }
    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        stop = s; // the `x': where a bare "0x" must leave *endptr
        c    = s[1];
        s += 2;
        base = 16;
    }
    if (base == 0)
        base = c == '0' ? 8 : 10;

    // Compute the cutoff value between legal numbers and illegal numbers.  That
    // is the largest legal value, divided by the base.  An input number that is
    // greater than this value, if followed by a legal input character, is too
    // big.  One that is equal to this value may be valid or not; the limit
    // between valid and invalid numbers is then based on the last digit.  For
    // instance, the range for longs here is [-1099511627776..1099511627775], so
    // in base 10 cutoff is 109951162777 and cutlim is either 5 (neg == 0) or 6
    // (neg == 1): a value above 109951162777, or equal with a next digit above
    // 5 (or 6), is too big and gets a range error.
    //
    // Set any if any `digits' consumed; make it negative to indicate overflow.
    cutoff = neg ? (unsigned long)LONG_MAX + 1 : (unsigned long)LONG_MAX;
    cutlim = (int)(cutoff % (unsigned long)base);
    cutoff = cutoff / (unsigned long)base;
    for (acc = 0, any = 0;; c = *s++) {
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'a' && c <= 'z')
            c -= 'a' - 10;
        else if (c >= 'A' && c <= 'Z')
            c -= 'A' - 10;
        else
            break;
        if (c >= base)
            break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc *= (unsigned long)base;
            acc += (unsigned long)c;
        }
    }
    if (endptr != 0)
        *endptr = (char *)(any ? s - 1 : stop);
    if (any < 0) {
        errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    if (neg)
        return acc == (unsigned long)LONG_MAX + 1 ? LONG_MIN : -(long)acc;
    return (long)acc;
}
