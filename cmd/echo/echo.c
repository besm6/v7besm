/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// echo -- write the arguments, separated by blanks, on the standard output.
//
// The v7 program, unchanged in what it does.  It is the smallest command on the system and
// the only one that touches neither the filesystem nor a struct, so the port is exactly the
// mechanical C11 pass every v7 source needs and nothing else: b6parse has no implicit `int'
// and no K&R parameter lists, so main() gets a prototype and an explicit return type, and
// `register int i, nflg;' loses the register -- the back end allocates its own.
//
// exit(0) became `return 0': crt0 calls exit(main(argc, argv)), so stdio is flushed either
// way, and this file then needs no <stdlib.h>.
//
// argv[i] is a fat char * (a bit-48 marker, a byte offset and a 15-bit word address --
// doc/Besm6_Data_Representation.md section 7); fputs() walks one correctly, and nothing here
// does arithmetic on the pointer VALUE, which is what the hazards in cmd/sh/README.md are
// all about.  Hence four lines of change and no more.
//
#include <stdio.h>

int main(int argc, char *argv[])
{
    int i, nflg;

    // -n: suppress the trailing newline.  Only the first argument is examined, and only its
    // first two characters, so `-none' is the flag and `echo -n -n' echoes the second one.
    // That is v7's behaviour, and what shell scripts of the period expect.
    nflg = 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
        nflg++;
        argc--;
        argv++;
    }

    for (i = 1; i < argc; i++) {
        fputs(argv[i], stdout);
        if (i < argc - 1)
            putchar(' ');
    }
    if (nflg == 0)
        putchar('\n');
    return 0;
}
