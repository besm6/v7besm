// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// The v7 linear congruential generator, x' = 1103515245*x + 12345.
//
// v7 held the state in a signed `long' and relied on it wrapping at 2^32.  Here a
// `long' is one word -- 41 bits signed, 48 unsigned -- so the state is declared
// UNSIGNED and wraps at 2^48, where the arithmetic is defined rather than merely
// customary.  The generator is unharmed by the wider modulus: 1103515245 is odd and
// congruent to 1 mod 4 and the increment is odd, which is what makes the period the
// full modulus, whatever power of two that is.
//
// The SEQUENCE IS STILL V7's, value for value, which is worth knowing before anyone
// tries to "fix" the modulus.  The low bits of a truncating product depend only on the
// low bits of its operands, so x mod 2^32 evolves here exactly as it did on a 32-bit
// machine, whatever the state's high bits are doing; and rand() returns bits 16..30,
// which are inside that window.  A wider state cannot change what is handed back --
// only srand()ing above 2^32 could, and no v7 program does.
//
#include <stdlib.h>

static unsigned long randx = 1;

void srand(unsigned x)
{
    randx = x;
}

int rand(void)
{
    randx = randx * 1103515245 + 12345;
    return (int)((randx >> 16) & 077777);
}
