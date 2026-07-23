// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Read one WORD -- six bytes, not the PDP-11's two.  sizeof(int) == NBPW == 6, so
// v7's `i | (getc(iop)<<8)' would have carried a quarter of the value.
//
// The bytes go out and come back MOST SIGNIFICANT FIRST, which is how six chars
// pack into a word on this machine (doc/Besm6_Data_Representation.md): a word
// written by putw() therefore reads back byte for byte as the word's own six
// characters.  Assembly is through an `unsigned' so the shifts are logical and a
// full 48-bit value survives the round trip.
//
// -1 is a legitimate word, so a caller that cares must ask feof()/ferror().
//
#include <stdio.h>

int getw(FILE *iop)
{
    unsigned w;
    int i, c;

    w = 0;
    for (i = 0; i < (int)sizeof(int); i++) {
        if ((c = getc(iop)) < 0)
            return -1;
        w = (w << 8) | (unsigned)c;
    }
    return w;
}
