/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// rev -- reverse the characters of every line.
//
//      rev [ file ... ]
//
// One of task C5a's six (../README.md), and the one of them that is not a faithful port.
// See README.md beside it for the argument; the short form is here.
//
// THE DIVERGENCE: THIS rev REVERSES UTF-8 SEQUENCES, NOT BYTES.  v7's reverses bytes, which
// is the same thing on an ASCII machine and is not the same thing here.  Since task C11 this
// system's text is UTF-8 end to end -- the console driver, the clists, the filesystem and
// /bin/sh all carry eight bits (§11) -- so a byte-reversing rev takes `привет', a line the
// machine can now type, store, glob and pass as an argument, and returns twelve bytes of
// mojibake.  Every other program on this image treats a Cyrillic line as text; this one
// would have been the exception, and the exception would have looked like a bug.
//
// The rule is UTF-8's own and needs no table: a byte 10xxxxxx is a CONTINUATION and belongs
// to the byte before it.  So the reversal walks back from the end of the line, collects each
// character's bytes as a group, and emits the group FORWARD.  Malformed input loses nothing
// -- a stray continuation run with no lead byte comes out as its own group -- which is the
// property that matters for a filter that has to be safe on arbitrary bytes.
//
// ../README.md §10 allows a divergence and asks for it to be written down twice: this header
// is one, the rewritten rev.1.umm is the other, and README.md is the account.
//
// TWO CONSEQUENCES FOR THE LONG-LINE SPLIT, which v7 has and which this keeps.  A line
// longer than the buffer is cut into buffer-sized pieces and each is reversed on its own,
// with a newline after it.  That was harmless with bytes; with characters it would cut a
// two-byte letter in half at every boundary, so the cut BACKS OFF to a character boundary
// and the bytes it declined to take start the next piece.  Without that the UTF-8 awareness
// would be a lie for exactly the lines long enough to need it.  N is 1024 rather than v7's
// 256 while the code is being touched -- the buffer is file scope, so it costs bss and not
// the four-page stack (§6).
//
// AND ONE UPSTREAM BUG.  v7 discards a final line that has no newline: `case EOF: goto eof'
// leaves whatever is already in the buffer unprinted, so `echo -n abc | rev' produced
// nothing at all.  It is printed now, with the newline rev adds to every other line.
//
// WHAT IS LEFT ALONE, both v7's: a file that cannot be opened aborts the whole run rather
// than the rest of the arguments continuing, and the no-argument path fcloses stdin.  Both
// are visible, neither is wrong, and rev.1.umm says so.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>

#define N 1024

static char line[N];
static FILE *input;

// How many bytes the UTF-8 sequence led by b claims.  A byte that leads nothing claims one,
// so malformed input is chunked rather than hung on.
static int seqlen(int b)
{
    if ((b & 0200) == 0)
        return 1;
    if ((b & 0340) == 0300)
        return 2;
    if ((b & 0360) == 0340)
        return 3;
    if ((b & 0370) == 0360)
        return 4;
    return 1;
}

// Where the last character of line[0..len-1] begins.  char is unsigned here, so a
// continuation byte really is 0200..0277 and not a negative number (§3).
static int lastchar(int len)
{
    int k;

    for (k = len - 1; k > 0 && (line[k] & 0300) == 0200; k--)
        ;
    return k;
}

// line[0..len-1] backwards a CHARACTER at a time, then a newline.
static void revline(int len)
{
    int i, j, k;

    for (i = len - 1; i >= 0; i = j - 1) {
        for (j = i; j > 0 && (line[j] & 0300) == 0200; j--)
            ;
        for (k = j; k <= i; k++)
            putc(line[k], stdout);
    }
    putc('\n', stdout);
}

int main(int argc, char **argv)
{
    int i, c, k, len;

    input = stdin;
    do {
        if (argc > 1) {
            if ((input = fopen(argv[1], "r")) == NULL) {
                fprintf(stderr, "rev: cannot open %s\n", argv[1]);
                exit(1);
            }
        }
        len = 0;
        for (;;) {
            c = getc(input);
            if (c == EOF)
                break;
            line[len++] = c;
            if (c == '\n') {
                revline(len - 1);
                len = 0;
                continue;
            }
            if (len < N)
                continue;

            // The buffer is full and the line has not ended.  Cut it -- but not through the
            // middle of a character: if the last sequence is incomplete, leave its bytes for
            // the next piece.
            k = lastchar(len);
            if (k > 0 && k + seqlen(line[k]) > len) {
                revline(k);
                for (i = 0; k + i < len; i++)
                    line[i] = line[k + i];
                len = i;
            } else {
                revline(len);
                len = 0;
            }
        }
        if (len > 0)
            revline(len); // the final line, which v7 threw away
        fclose(input);
        argc--;
        argv++;
    } while (argc > 1);
    return 0;
}
