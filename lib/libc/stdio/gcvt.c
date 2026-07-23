// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// gcvt -- floating output conversion to a minimal-length string.
//
// v7's, unchanged but for the prototypes: it is all decimal digit shuffling over
// what ecvt() produced, and nothing in it depends on the width of a word.  ecvt is
// declared here because no header declares it -- it is a v7 extension, not C11.
//
#include <stdio.h>

char *ecvt(double arg, int ndigits, int *decpt, int *sign);

char *gcvt(double number, int ndigit, char *buf)
{
    int sign, decpt;
    char *p1, *p2;
    int i;

    p1 = ecvt(number, ndigit, &decpt, &sign);
    p2 = buf;
    if (sign)
        *p2++ = '-';
    for (i = ndigit - 1; i > 0 && p1[i] == '0'; i--)
        ndigit--;
    if ((decpt >= 0 && decpt - ndigit > 4) || (decpt < 0 && decpt < -3)) {
        // use E-style
        decpt--;
        *p2++ = *p1++;
        *p2++ = '.';
        for (i = 1; i < ndigit; i++)
            *p2++ = *p1++;
        *p2++ = 'e';
        if (decpt < 0) {
            decpt = -decpt;
            *p2++ = '-';
        } else
            *p2++ = '+';
        *p2++ = decpt / 10 + '0';
        *p2++ = decpt % 10 + '0';
    } else {
        if (decpt <= 0) {
            if (*p1 != '0')
                *p2++ = '.';
            while (decpt < 0) {
                decpt++;
                *p2++ = '0';
            }
        }
        for (i = 1; i <= ndigit; i++) {
            *p2++ = *p1++;
            if (i == decpt)
                *p2++ = '.';
        }
        if (ndigit < decpt) {
            while (ndigit++ < decpt)
                *p2++ = '0';
            *p2++ = '.';
        }
    }
    if (p2[-1] == '.')
        p2--;
    *p2 = '\0';
    return buf;
}
