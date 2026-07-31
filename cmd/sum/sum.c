/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sum -- checksum a file and say how big it is.
//
//      sum [ file ... ]
//
// One of task C5a's six (../README.md).  Two things are worth knowing about it, and the
// first is a NEGATIVE result: the checksum does not change on this machine, and it is not
// obvious that it should not.
//
// THE 16-BIT CHECKSUM SURVIVES A 41-BIT unsigned, exactly.  The loop is
//
//      if (sum & 01) sum = (sum >> 1) + 0x8000; else sum >>= 1;
//      sum += c;
//      sum &= 0xFFFF;
//
// -- a rotate right within sixteen bits, then an add, then a mask.  The mask is INSIDE the
// loop, so `sum <= 0xFFFF' holds at the top of every iteration; `sum >> 1' is then at most
// 0x7FFF and there is no high garbage to shift down, and `sum + c' is at most 0x100FE, which
// needs seventeen bits and cannot overflow a word here any more than it overflowed two bytes
// there.  So the answer is bit for bit the PDP-11's, and would NOT have been if v7 had
// leaned on unsigned wraparound instead of masking.  It is a width dependence that is not
// there, which is worth a comment precisely because the next reader will look for one.
//
// THE BLOCK COUNT IS THE PART THAT CHANGED, and it is §4.  v7 divided by BUFSIZ, which was
// 512 on a PDP-11 and was also its BSIZE, so the number named a filesystem block.  BUFSIZ is
// 3072 here and so is BSIZE, so the division still names one -- but a count REPORTED TO A
// USER is not in that unit on this machine: df, du, quot and `ls -s' all print KBPB == 3
// reported blocks per filesystem block, so that what they say means something without
// knowing BSIZE.  sum now says the same, its number is a multiple of three, and sum.1 has
// the BLOCKS ARE 1024 BYTES section the other four carry.  §4's rule about where the
// multiply goes is kept: `nblocks' holds filesystem blocks right up to the printf.
//
// (The number is therefore SIX TIMES v7's for the same file, not a third of it: v7 counted
// 512-byte blocks and this counts 1024-byte ones.  A file of one byte reads as 3 either way,
// being one filesystem block however small it is.)
//
// A SINGLE FILE ARGUMENT DOES NOT GET ITS NAME PRINTED -- the test is `argc > 2' -- which is
// v7's and is what every later sum(1) does too.  Left alone; sum.1 now says it.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

// §4: the reported block is 1024 bytes and a filesystem block must be a whole number of
// them, or the count below would be a lie by rounding.
_Static_assert(BSIZE % KBYTE == 0, "a block must be a whole number of reported blocks");

int main(int argc, char **argv)
{
    unsigned sum;
    int i, c;
    FILE *f;
    int nbytes;
    int errflg = 0;

    i = 1;
    do {
        if (i < argc) {
            if ((f = fopen(argv[i], "r")) == NULL) {
                fprintf(stderr, "sum: Can't open %s\n", argv[i]);
                errflg += 10;
                continue;
            }
        } else
            f = stdin;
        sum    = 0;
        nbytes = 0;
        while ((c = getc(f)) != EOF) {
            nbytes++;
            if (sum & 01)
                sum = (sum >> 1) + 0x8000;
            else
                sum >>= 1;
            sum += c;
            sum &= 0xFFFF;
        }
        if (ferror(f)) {
            errflg++;
            fprintf(stderr, "sum: read error on %s\n", argc > 1 ? argv[i] : "-");
        }
        // The count stays in filesystem blocks until here, and KBPB turns it into the
        // 1024-byte blocks every other program on this image reports (§4).
        printf("%05u%6d", sum, (nbytes + BSIZE - 1) / BSIZE * KBPB);
        if (argc > 2)
            printf(" %s", argv[i]);
        printf("\n");
        fclose(f);
    } while (++i < argc);
    exit(errflg);
}
