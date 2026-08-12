/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// makekey -- generate an encryption key.  /usr/lib/makekey.
//
// Ten bytes in, thirteen out, the transformation deliberately expensive: it is crypt(3)
// and nothing else.  No stdio, so it costs a fraction of a program that has one (§6).
//
// Two corrections to v7's twenty-one lines.  A read(2) may be short, and v7's two
// unchecked ones wrote thirteen bytes derived from whatever the stack held; the loop below
// refuses instead.  And v7 passed an unterminated char[8], which crypt(3) reads one past
// before its own guard stops it.
//
#include <unistd.h>

// Read exactly n bytes, or fewer at end of input.
static int readall(char *buf, int n)
{
    int got, i;

    for (i = 0; i < n; i += got) {
        got = read(0, buf + i, n - i);
        if (got <= 0)
            break;
    }
    return i;
}

int main(void)
{
    char key[9], salt[3];

    if (readall(key, 8) != 8 || readall(salt, 2) != 2) {
        write(2, "makekey: short input\n", 21);
        return 1;
    }
    key[8]  = '\0';
    salt[2] = '\0';
    write(1, crypt(key, salt), 13);
    return 0;
}
