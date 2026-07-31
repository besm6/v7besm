/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// wc -- count lines, words and characters.
//
//      wc [ -lwc ] [ file ... ]
//
// The first of task C5a's six (../README.md), and the cheapest program in the phase: no
// buffer, no arena, no pointer walk, and therefore none of §2 -- the whole of the six turned
// out to carry not one char * comparison between them, which ../README.md records.
//
// WHAT A WORD IS, AND WHY THE TEST HAD TO CHANGE.  v7 wrote
//
//      if(' '<c&&c<0177)
//
// which makes a word out of the printable ASCII range and a DELIMITER out of everything
// above it.  On this machine text is UTF-8 (§11), so every byte of a Cyrillic letter is
// 0200 or over and v7's test would count `привет' as no words at all -- and, worse, would
// count `привет мир' as none while counting `hello world' as two.  The test is now
//
//      c > ' ' && c != 0177
//
// so a byte above 0177 is a word constituent.  The DEL exclusion is v7's and is kept, as is
// the shape of what follows it: only space, tab and newline reset the token, so a stray
// control character neither ends a word nor starts one.  That is upstream's reading and this
// port does not change it.
//
// A CHARACTER IS A BYTE, which is the other half of the same point and is a limitation
// rather than a choice: `charct' counts what getc() returns, so a two-byte letter counts as
// two.  wc.1 says so.  Counting UTF-8 sequences would be a different program from the one
// the manual page describes, and the count everything else on this machine wants -- a file's
// size, a line's length in the terminal driver -- is the byte one.
//
// THE COUNTERS ARE int.  v7 declared six longs; a long is an int is one 41-bit word here
// (§3), so `%7ld' is written `%7d' and the types say what they are.  41 bits reaches
// 1.1e12, which is a hundred thousand times the disk.
//
// AN UNREADABLE FILE IS AN ERROR NOW.  v7 printed the diagnostic, skipped the file and
// exited 0, so no script could tell `wc *.c' that read everything from one that read half.
// This port remembers the failure and exits 1; wc.1 marks it.  The file is still skipped
// rather than aborting the run, which is v7's choice and the useful one.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>

// Print the counts the flag string asks for, in the flag string's order.
static void wcp(const char *wd, int charct, int wordct, int linect)
{
    while (*wd) {
        switch (*wd++) {
        case 'l':
            printf("%7d", linect);
            break;

        case 'w':
            printf("%7d ", wordct);
            break;

        case 'c':
            printf("%7d", charct);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int i, token;
    FILE *fp;
    int linect, wordct, charct;
    int tlinect = 0, twordct = 0, tcharct = 0;
    const char *wd;
    int errflg = 0;
    int c;

    wd = "lwc";
    if (argc > 1 && *argv[1] == '-') {
        // A bare `-' leaves an empty flag string, and wcp() then prints nothing at all.
        // That is v7's, and wc.1 says so rather than this port inventing a meaning for it.
        wd = ++argv[1];
        argc--;
        argv++;
    }

    i  = 1;
    fp = stdin;
    do {
        if (argc > 1 && (fp = fopen(argv[i], "r")) == NULL) {
            fprintf(stderr, "wc: can't open %s\n", argv[i]);
            errflg = 1;
            continue;
        }
        linect = 0;
        wordct = 0;
        charct = 0;
        token  = 0;
        for (;;) {
            c = getc(fp);
            if (c == EOF)
                break;
            charct++;
            if (c > ' ' && c != 0177) {
                if (!token) {
                    wordct++;
                    token++;
                }
                continue;
            }
            if (c == '\n')
                linect++;
            else if (c != ' ' && c != '\t')
                continue;
            token = 0;
        }
        // print lines, words, chars
        wcp(wd, charct, wordct, linect);
        if (argc > 1) {
            printf(" %s\n", argv[i]);
        } else
            printf("\n");
        fclose(fp);
        tlinect += linect;
        twordct += wordct;
        tcharct += charct;
    } while (++i < argc);
    if (argc > 2) {
        wcp(wd, tcharct, twordct, tlinect);
        printf(" total\n");
    }
    exit(errflg);
}
