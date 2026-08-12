// The oracle for cmd/crypt and cmd/makekey: v7's rotor with a REAL int32_t seed and a REAL
// uint16_t random, over the build host's crypt(3).  Nothing of the BESM-6 port is in this
// path, which is what makes the fixtures a second implementation's answer rather than a
// transcript of our own -- ../README.md, "The oracle", is the argument.
//
// NOTHING BUILDS THIS.  It is host C, run by hand when a fixture has to be regenerated:
//
//	cc -o mkfix mkfix.c
//	./mkfix hobbit      <enc.in  >enc.expected      # and the mirror pair for dec
//	./mkfix crypto12    <key8.in >key8.expected     # keylong.expected is the same bytes
//	./mkfix -k          <../../makekey/test/key.in  >../../makekey/test/key.expected
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROTORSZ 256
#define MASK    0377

static char t1[ROTORSZ], t2[ROTORSZ], t3[ROTORSZ];

static void setup(const char *pwarg)
{
    int ic, i, k, temp;
    uint16_t random;
    char buf[13];
    char pw[9], salt[3];
    int32_t seed;

    memset(pw, 0, sizeof(pw));
    strncpy(pw, pwarg, 8);
    salt[0] = pw[0];
    salt[1] = pw[1];
    salt[2] = 0;
    memcpy(buf, crypt(pw, salt), 13);

    seed = 123;
    for (i = 0; i < 13; i++)
        seed = seed * buf[i] + i;
    for (i = 0; i < ROTORSZ; i++) {
        t1[i] = i;
        t3[i] = 0;
    }
    for (i = 0; i < ROTORSZ; i++) {
        seed   = 5 * seed + buf[i % 13];
        random = (uint16_t)(seed % 65521);
        k      = ROTORSZ - 1 - i;
        ic     = (random & MASK) % (k + 1);
        random >>= 8;
        temp   = t1[k];
        t1[k]  = t1[ic];
        t1[ic] = temp;
        if (t3[k] != 0)
            continue;
        ic = (random & MASK) % k;
        while (t3[ic] != 0)
            ic = (ic + 1) % k;
        t3[k]  = ic;
        t3[ic] = k;
    }
    for (i = 0; i < ROTORSZ; i++)
        t2[t1[i] & MASK] = i;
}

int main(int argc, char **argv)
{
    int i, n1 = 0, n2 = 0;
    unsigned char c;

    if (argc == 2 && strcmp(argv[1], "-k") == 0) {
        // makekey: ten bytes on stdin, thirteen out.
        char key[9], salt[3];
        if (fread(key, 1, 8, stdin) != 8 || fread(salt, 1, 2, stdin) != 2)
            return 1;
        key[8]  = 0;
        salt[2] = 0;
        fwrite(crypt(key, salt), 1, 13, stdout);
        return 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: mkfix key <in >out, or mkfix -k <tenbytes\n");
        return 2;
    }
    setup(argv[1]);
    while ((i = getchar()) != EOF) {
        c = i;
        c = t2[(t3[(t1[(c + n1) & MASK] + n2) & MASK] - n2) & MASK] - n1;
        putchar(c);
        n1++;
        if (n1 == ROTORSZ) {
            n1 = 0;
            n2++;
            if (n2 == ROTORSZ)
                n2 = 0;
        }
    }
    return 0;
}
