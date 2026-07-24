// Copyright (c) 1990, 1993 The Regents of the University of California.
// See /COPYRIGHT or www.tuhs.org for details.

//
// strtoul — convert the initial portion of a string to unsigned long
// (C11 §7.22.1.7).
//
// See gen/strtol.c for the logic and for what this machine changed in 4.4BSD's
// scanner -- the ctype guard, the explicit digit ranges, the "0x" and bad-base
// fixes -- all of which are the same here.  Only the limits differ: an `unsigned
// long' is the whole 48-bit word, so the cutoff is ULONG_MAX and there is no
// asymmetric negative end to protect.  A leading `-' is honoured and the result
// negated, which §7.22.1.7p2 requires and which is ordinary modulo-2^48 unsigned
// negation here (b$uneg) -- no signed value is reinterpreted anywhere.
//
// It is a second copy of the scanner rather than a call into a shared static
// core, as it is in BSD: one member per function is what lets b6ranlib's index
// pull only the one a program calls (lib/README.md), and a shared core would
// come along with either.
//
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

unsigned long strtoul(const char *nptr, char **endptr, int base)
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
        stop = s;
        c    = s[1];
        s += 2;
        base = 16;
    }
    if (base == 0)
        base = c == '0' ? 8 : 10;

    cutoff = ULONG_MAX / (unsigned long)base;
    cutlim = (int)(ULONG_MAX % (unsigned long)base);
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
        return ULONG_MAX;
    }
    if (neg)
        acc = -acc;
    return acc;
}
