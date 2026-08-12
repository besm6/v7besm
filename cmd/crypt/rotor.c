/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// The one-rotor machine: v7's crypt.c setup() and ed.c crinit()/crblock() merged into the
// one copy both programs link.  Two changes from v7, both argued in ./README.md: the key
// comes from crypt(3) instead of a forked /usr/lib/makekey, and the seed arithmetic is
// bounded to a PDP-11 `long'.
//
#include "rotor.h"

#include <string.h>
#include <unistd.h>

#define MASK 0377

// A PDP-11 `long': the low 32 bits taken as signed.  Not fidelity for its own sake --
// b$mul keeps the HIGH bits on overflow, so unbounded this derives nothing reproducible.
#define WRAP32(s) ((((s) & 0xffffffff) ^ 0x80000000) - 0x80000000)

int crinit(char *key, char *perm)
{
    char *t1 = perm;
    char *t2 = perm + ROTORSZ;
    char *t3 = perm + 2 * ROTORSZ;
    int i, ic, k, temp, random, nonempty;
    char pw[9], salt[3], buf[13];
    long seed;

    // The ten bytes v7 wrote down the pipe: eight of key, and the first two again as salt.
    nonempty = (*key != '\0');
    strncpy(pw, key, 8);
    pw[8]   = '\0';
    salt[0] = pw[0];
    salt[1] = pw[1];
    salt[2] = '\0';

    // crypt.1: the key is destroyed immediately upon entry, ps(1) being able to read argv.
    while (*key)
        *key++ = '\0';

    memcpy(buf, crypt(pw, salt), 13);
    memset(pw, 0, sizeof(pw));

    // Every buf[i] is printable, so a plain char being unsigned here changes nothing.
    seed = 123;
    for (i = 0; i < 13; i++)
        seed = WRAP32(seed * buf[i] + i);

    for (i = 0; i < ROTORSZ; i++) {
        t1[i] = i;
        t3[i] = 0;
    }
    for (i = 0; i < ROTORSZ; i++) {
        seed = WRAP32(5 * seed + buf[i % 13]);

        // A PDP-11 `unsigned' is 16 bits, so only these bits ever reached the rotor.
        random = (seed % 65521) & 0177777;

        k  = ROTORSZ - 1 - i;
        ic = (random & MASK) % (k + 1);
        random >>= 8;
        temp   = t1[k];
        t1[k]  = t1[ic];
        t1[ic] = temp;
        if (t3[k] != 0)
            continue;

        // t3 is a pairing, hence its own inverse: this is what makes the machine an
        // involution.  k never reaches 0 here -- README.md, "Why % k is safe".
        ic = (random & MASK) % k;
        while (t3[ic] != 0)
            ic = (ic + 1) % k;
        t3[k]  = ic;
        t3[ic] = k;
    }
    for (i = 0; i < ROTORSZ; i++)
        t2[t1[i] & MASK] = i;

    memset(buf, 0, sizeof(buf));
    return nonempty;
}

void crblock(char *perm, char *buf, int nchar, long startn)
{
    char *t1 = perm;
    char *t2 = perm + ROTORSZ;
    char *t3 = perm + 2 * ROTORSZ;
    char *p1;
    int n1, n2;

    n1 = startn & MASK;
    n2 = (startn >> 8) & MASK;
    p1 = buf;
    while (nchar--) {
        // §11: every mask below is on an INDEX into a 256-entry table, never on a byte.
        *p1 = t2[(t3[(t1[(*p1 + n1) & MASK] + n2) & MASK] - n2) & MASK] - n1;
        n1++;
        if (n1 == ROTORSZ) {
            n1 = 0;
            n2++;
            if (n2 == ROTORSZ)
                n2 = 0;
        }
        p1++;
    }
}
