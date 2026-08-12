/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// crypt -- encode and decode.
//
//	crypt [ password ]
//
// The machine is an involution, so the same command undoes itself and there is no decrypt
// flag.  ./rotor.c is the machine; ./README.md is the account of the port.
//
// v7 ran the rotor inline over getchar()/putchar().  This reads a block, hands it to
// crblock() and writes it back, so no stream byte is ever looked at here -- which is how
// ../README.md §11 is answered: there is nowhere left to put a mask.
//
// NOT SETUID: it reads its own input and writes its own output.
//
#include <stdio.h>
#include <unistd.h>

#include "rotor.h"

static char perm[PERMSZ];
static char cbuf[BUFSIZ]; // static: 512 words against a 4,096-word stack (§6)

int main(int argc, char **argv)
{
    char *key;
    long pos;
    int n;

    // getpass(3) cannot fail -- it falls back to stdin when there is no /dev/tty -- and it
    // reads eight characters, every one the rotor uses.
    key = (argc != 2) ? getpass("Enter key:") : argv[1];

    crinit(key, perm); // destroys the key where it stands, in argv

    pos = 0;
    while ((n = fread(cbuf, 1, sizeof(cbuf), stdin)) > 0) {
        crblock(perm, cbuf, n, pos);
        fwrite(cbuf, 1, n, stdout);
        pos += n;
    }
    fflush(stdout);
    return 0;
}
