// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// Swap adjacent bytes: n/2 pairs from `from' to `to'.
//
// v7 wrote this over `short *' -- "swap bytes in 16-bit [half-]words for going
// between the 11 and the interdata".  There is no 16-bit unit on this machine:
// `short' is an alias for `int', one 48-bit word (doc/Besm6_Data_Representation.md),
// so the v7 loop would swap the halves of a WORD and mean nothing to any caller.
//
// What every caller actually wants is the byte stream reordered pairwise -- that is
// what dump/restor and tp use it for -- so this is written over `char *', which on
// this machine is the fat pointer that walks a byte at a time across word boundaries.
// An odd n leaves the last byte alone, as it did on the PDP-11 (n/2 truncates).
//
// Both bytes are read before either is written, so from == to is legal; overlap by
// one byte is not, exactly as before.
//
void swab(const char *from, char *to, int n)
{
    char a, b;

    n /= 2;
    while (--n >= 0) {
        a     = from[0];
        b     = from[1];
        to[0] = b;
        to[1] = a;
        from += 2;
        to += 2;
    }
}
