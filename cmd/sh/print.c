/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// Printing and io conversion.
//
// The shell writes through write(2) and never touches stdio, which is why nothing here
// drags libc's malloc into the link beside the shell's own arena.
//
#include <limits.h>
#include <unistd.h>

#include "defs.h"

//
// numbuf was CHAR[6] in v7 -- five digits and a NUL, which was all a 16-bit int needed.
// An INT here is a 48-bit word holding a 41-bit signed value, so thirteen digits, a
// sign and a NUL.
//
CHAR numbuf[NUMBUF];

void newline(void)
{
    prc(NL);
}

void blank(void)
{
    prc(SP);
}

void prp(void)
{
    if ((flags & prompt) == 0 && cmdadr) {
        prs(cmdadr);
        prs(colon);
    }
}

void prs(STRING as)
{
    STRING s = as;

    if (s != 0)
        write(output, s, length(s) - 1);
}

//
// THE PARAMETER MUST STAY A CHAR.  &c on a standalone char yields a fat pointer at byte
// #5 of its word -- the least significant byte, which is where a standalone char lives
// (doc/Besm6_Data_Representation.md section 8).  Widen it to `int' and &c becomes an
// int *, whose conversion to char * points at byte #0, the most significant byte, which
// is always zero: every prc() would write a NUL and the shell would go silent while
// compiling perfectly.
//
void prc(CHAR c)
{
    if (c)
        write(output, &c, 1);
}

void prt(L_INT t)
{
    INT hr, min, sec;

    t += 30;
    t /= 60;
    sec = t % 60;
    t /= 60;
    min = t % 60;
    if ((hr = t / 60) != 0) {
        prn(hr);
        prc('h');
    }
    prn(min);
    prc('m');
    prn(sec);
    prc('s');
}

void prn(INT n)
{
    itos(n);
    prs(numbuf);
}

//
// Convert n to decimal in numbuf.
//
// v7's version divided by a fixed ladder of powers of ten from 10000 down, so it could
// not print more than five digits, and it copied a signed int into a POS, so a negative
// one came out as a very large positive.  Both are reachable here -- prn() is handed
// pids, exit values and line numbers -- and the second is reachable in v7 too: `shift'
// on an empty argument list assigns $# from argn-1 == -1.
//
void itos(INT n)
{
    CHAR *abuf = numbuf;
    CHAR digits[NUMBUF];
    INT i = 0;
    unsigned long a;

    if (n < 0) {
        *abuf++ = MINUS;
        // Negate in the unsigned domain: the most negative INT has no positive.
        a = -(unsigned long)n;
    } else {
        a = (unsigned long)n;
    }

    do {
        digits[i++] = a % 10 + '0';
        a /= 10;
    } while (a != 0);

    while (i > 0)
        *abuf++ = digits[--i];
    *abuf = 0;
}

//
// Convert a decimal string, stopping at the first non-digit as v7 does.
//
// v7 detected overflow by testing whether the signed accumulator had gone negative.
// Signed overflow is undefined in C11 and a compiler is entitled to conclude that the
// test can never fire and delete it, so the accumulation is unsigned here and the limit
// is checked before it is exceeded rather than after.
//
INT stoi(STRING icp)
{
    STRING cp       = icp;
    unsigned long r = 0;
    BOOL over       = 0;
    CHAR c;

    while ((c = *cp) != 0 && digit(c)) {
        if (r > (unsigned long)(INT_MAX / 10))
            over = 1;
        r = r * 10 + (c - '0');
        if (r > (unsigned long)INT_MAX)
            over = 1;
        cp++;
    }
    if (over || cp == icp)
        failed(icp, badnum);
    return (INT)r;
}
