/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sort -- sort and/or merge files.
//
//      sort [ -mubdfinctx ] [ +pos1 [ -pos2 ] ] ... [ -o name ] [ -T dir ] [ name ] ...
//
// Task C5d (../TODO.md), the sixteenth of the text filters and the heavyweight of the phase:
// it manages its own storage, runs its own merge over temp files, and catches four signals to
// clean them up.  §1's C11 pass is mechanical and is not described here.  What the port had
// to decide is below, worst first; ./README.md is the long form.
//
// THE FIVE 256-BYTE TABLES WERE ROTATED BY 128, AND THAT IS A WILD READ.  v7 wrote fold[],
// nofold[], nonprint[] and dict[] with entries 0..127 holding the values for 0200..0377 and
// entries 128..255 holding the values for 0000..0177, and then indexed every one of them
// biased -- `nofold+128', `dict+128', `fold+128', `nonprint+128', `zero+128' -- so that a
// SIGNED char subscript of -128..127 landed in range.  A char is UNSIGNED here (§11), so
// `code[*pa]' evaluated table[128 + c] for c in 0..255: up to 128 bytes past the end of a
// 256-byte array, for every byte of every Cyrillic letter.  The tables are in natural order
// now and the five biases are gone, so the bound holds by construction -- the same shape as
// the CCL widening in ../grep/README.md, except that grep's was a wild STORE and this is a
// wild READ.
//
// AND THEN THE QUESTION THE ROTATION WAS HIDING: WHAT DO -i AND -d MEAN ABOVE 0177?  v7's
// tables answer `ignore' for every byte of 0200..0377, in both.  Carried faithfully that
// makes `sort -d' and `sort -i' delete every byte of every Cyrillic letter before comparing,
// so `привет' and `мир' compare EQUAL and the order among them is whatever the sort happened
// to do -- col's failure mode exactly, plausible output that is silently wrong.  THE FIFTH
// DELIBERATE DIVERGENCE, after touch, rev, col and grep -b: a byte above 0177 is significant.
// nonprint[] calls it printing and dict[] calls it alphanumeric, which is what it is on a
// machine whose text is UTF-8 end to end.  fold[] stays the identity there: folding the case
// of a Cyrillic letter is a two-byte operation and this program has no business doing it.
//
// cmpa()'s `*pb > *pa' DID NOT HAVE TO CHANGE, and that is worth saying.  It was a SIGNED
// byte comparison on the PDP-11, so v7 sorted 0200..0377 BELOW 0000..0177; here a char is
// unsigned and the same line orders bytes by their value, which is what a UTF-8 corpus wants
// -- byte order and code-point order are the same thing in UTF-8.
//
// AN ARENA THAT TAKES EVERYTHING STARVES stdio, SILENTLY.  sort grabs its arena with brk()
// and only then fopen()s its inputs, and a stream whose malloc(BUFSIZ) fails does not fail:
// lib/libc/stdio/filbuf.c quietly sets _IOUNBUF and does one read(2) PER BYTE from then on.
// Correct, unbounded slow, and nothing says a word.  So the arena reserves those buffers first
// -- by ALLOCATING and freeing them rather than by subtracting a number, since malloc serves
// one 512-word buffer per page it takes and a subtraction would have been nearly half short.
// A reserve that is computed can be computed wrong; one that is allocated cannot be, and its
// failure is a checked NULL instead of a silent slowdown.
// Two other things about the arena: v7 backed off in 512-BYTE clicks where the
// kernel grants a 1024-WORD page (kernel/sys1.c sbreak), and v7 kept a further 512 bytes back
// `for recursion' -- which has nothing to protect here, the stack being its own four pages at
// 070000 ABOVE the heap's ceiling (kernel/utab.c estabur) rather than the far end of the same
// segment.  That reserve is deleted rather than converted.
//
// A `struct merg *' IS NOT A `char *'.  v7 sorted its merge inputs with
// `qsort((char **)ibuf, (char **)(ibuf+i))' -- an array of `struct merg *' reinterpreted
// wholesale as `char *', which works on a PDP-11 because l[] is the first member.  A char *
// here is a FAT pointer: bit 48 set and a byte offset in bits 47-45 (doc/Besm6_Data_-
// Representation.md), where a pointer to a struct is a plain word address with bit 48 clear.
// The bit patterns are not interchangeable.  Since the fan-in is N == 7 it is a typed
// insertion sort now, using the comparison shape the surrounding code already used.
//
// copyproto() COPIED TWO char * THROUGH AN int LVALUE, word by word.  An int is bits 41-1; a
// fat pointer uses bit 48 and bits 47-45.  Every key's code[] and ignore[] went through it.
// It copies the members now.
//
// A LINE LIMIT ON ONE PATH AND NOT THE OTHER IS WORSE THAN EITHER.  v7 truncated a merge line
// at 512 bytes by overwriting the last byte in a loop, with no diagnostic, while the sort
// pass had no line limit at all -- so the same file came out right or came out corrupted
// depending on whether it fit in the arena in one pass.  L is 3072 now, one BSIZE, and the
// two paths share it: a longer line is a diagnostic and a non-zero exit on both.
//
// THE FINAL LINE WITH NO NEWLINE IS KEPT, which is the same upstream bug rev and uniq had.
// v7's rline() answered `end of file' the instant it saw EOF whatever it had already read,
// and sort()'s reader supplied the missing newline and then dropped the line anyway.
//
// TWO UNBOUNDED WRITES, which is §6's recurring finding: the temp-file name was sprintf'd
// from an unbounded -T argument into char[30], and the sort pass wrote a line into the arena
// with no bound inside the line.  snprintf and L respectively.
//
// isdigit() ON AN ARBITRARY BYTE RUNS OFF THE TABLE -- <ctype.h>'s macros index
// `(_ctype_ + 1)[c]' and lib/libc/gen/ctype_.c is 129 entries.  A numeric key is ASCII digits
// by definition, so this file has its own digit() rather than an isascii() wart at seven
// call sites.
//
// TEMP FILES GO TO /tmp, and there is no second try.  v7 tried /usr/tmp and fell back to
// /tmp; this image has /tmp and no /usr/tmp (../../root.manifest), so the first try was a
// wasted creat() on every run.  With one entry left, -T becomes authoritative: v7 silently
// used /tmp when the directory the user named was unusable, which is not a service.
//
// WHAT IS LEFT ALONE, DELIBERATELY: sortlines() marks a duplicate under -u by poking a NUL
// into the text arena through the line pointer, read back by sort()'s writer as `if (*cp)';
// field()'s `.' case writes past m[2] into n[2] on purpose, using the distance between them;
// and an unrecognised flag letter is taken as a position rather than diagnosed.  All three
// are v7's and all three still do here what they did there.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// The longest line, in bytes, NEWLINE INCLUDED, on both the sort path and the merge path.
// v7's was 512 and applied to the merge path alone.  One BSIZE is generous and is also one
// stdio buffer, so a line and the buffer that carries it are the same size.
#define L 3072

#define N  7  // input files merged at a time
#define NF 10 // keys

// The bytes of line text budgeted behind each line pointer.  v7 wrote 8 against a two-byte
// pointer -- FOUR TIMES THE POINTER, a fifth of the arena spent on the index.  It is the
// ratio that carries over and not the 8: a pointer is a word here, so the same fifth is 24.
#define AVGTEXT (4 * (int)sizeof(char *))

// The fewest lines an arena is worth having: enough text for one whole line.
#define MINLINES ((L + AVGTEXT - 1) / AVGTEXT)

// The temp-file name is `<dir>/stm<pid>aa'.  setfil() overwrites the last two letters in
// place, so the buffer is one name and not a formatting scratchpad.
#define MAXTEMP 64

// The two letters setfil() writes run `aa' to `zz' and then off the end of the alphabet.
#define MAXTMPFILES (26 * 26)

struct merg {
    char l[L];
    FILE *b;
};

// merge() lays N+1 `struct merg' over the front of the arena -- N inputs, plus the one past
// them that holds the previous line under -u.  The arena must never be split so finely that
// they do not fit.  The size is `sizeof' and not L + sizeof(FILE *): l[] is padded out to a
// whole number of words before b, which is C4f's /etc/mtab trap in another program.
#define MERGRESERVE ((N + 1) * (int)sizeof(struct merg))

_Static_assert(sizeof(struct merg) % NBPW == 0, "a merg slot must be a whole number of words");

// What stdio must still be able to malloc after the arena is taken: N merge inputs and the
// output, all open at once, plus one for slack.  stdin and stdout cost nothing -- they use
// the static _sibuf/_sobuf in bss (lib/libc/stdio/data.c) -- and fclose() gives a buffer
// back, so the peak is what is open at one moment and not what was ever opened.
//
// The word on the end of BUFSIZ is malloc's block header, which is what makes the number
// exact rather than approximate: nine blocks of BUFSIZ + NBPW carve a reservation of
// (N+2)*(BUFSIZ+NBPW) with nothing left over.
#define STDIORESERVE ((N + 2) * (BUFSIZ + NBPW))

static FILE *is, *os;
static char *dirtry[] = { "/tmp", NULL };
static char **dirs;
static char file1[MAXTEMP];
static char *file = file1;
static char *filep;
static int nfiles;
static unsigned nlines;
static unsigned ntext;
static char **lspace;
static char *tspace;
static int mflg;
static int cflg;
static int uflg;
static char *outfil;
static int unsafeout; /*kludge to assure -m -o works*/
static char tabchar;
static int eargc;
static char **eargv;
static int nfields;

// term() is a signal handler as well as the ordinary way out, so the status it exits with is
// read from a handler.
static volatile sig_atomic_t error = 1;

static char zero[256];

// fold: -f, upper and lower case together.  v7's table folded `]' to `\' -- a plain
// transcription slip in a hand-written table, corrected here.
static char fold[256] = {
    0000, 0001, 0002, 0003, 0004, 0005, 0006, 0007, 0010, 0011, 0012, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0024, 0025, 0026, 0027, 0030, 0031, 0032, 0033, 0034, 0035, 0036, 0037,
    0040, 0041, 0042, 0043, 0044, 0045, 0046, 0047, 0050, 0051, 0052, 0053, 0054, 0055, 0056, 0057,
    0060, 0061, 0062, 0063, 0064, 0065, 0066, 0067, 0070, 0071, 0072, 0073, 0074, 0075, 0076, 0077,
    0100, 0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0112, 0113, 0114, 0115, 0116, 0117,
    0120, 0121, 0122, 0123, 0124, 0125, 0126, 0127, 0130, 0131, 0132, 0133, 0134, 0135, 0136, 0137,
    0140, 0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0112, 0113, 0114, 0115, 0116, 0117,
    0120, 0121, 0122, 0123, 0124, 0125, 0126, 0127, 0130, 0131, 0132, 0173, 0174, 0175, 0176, 0177,
    0200, 0201, 0202, 0203, 0204, 0205, 0206, 0207, 0210, 0211, 0212, 0213, 0214, 0215, 0216, 0217,
    0220, 0221, 0222, 0223, 0224, 0225, 0226, 0227, 0230, 0231, 0232, 0233, 0234, 0235, 0236, 0237,
    0240, 0241, 0242, 0243, 0244, 0245, 0246, 0247, 0250, 0251, 0252, 0253, 0254, 0255, 0256, 0257,
    0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267, 0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277,
    0300, 0301, 0302, 0303, 0304, 0305, 0306, 0307, 0310, 0311, 0312, 0313, 0314, 0315, 0316, 0317,
    0320, 0321, 0322, 0323, 0324, 0325, 0326, 0327, 0330, 0331, 0332, 0333, 0334, 0335, 0336, 0337,
    0340, 0341, 0342, 0343, 0344, 0345, 0346, 0347, 0350, 0351, 0352, 0353, 0354, 0355, 0356, 0357,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367, 0370, 0371, 0372, 0373, 0374, 0375, 0376, 0377
};

// nofold: the identity, and the default.  A comparison through it is `*pb - *pa'.
static char nofold[256] = {
    0000, 0001, 0002, 0003, 0004, 0005, 0006, 0007, 0010, 0011, 0012, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0024, 0025, 0026, 0027, 0030, 0031, 0032, 0033, 0034, 0035, 0036, 0037,
    0040, 0041, 0042, 0043, 0044, 0045, 0046, 0047, 0050, 0051, 0052, 0053, 0054, 0055, 0056, 0057,
    0060, 0061, 0062, 0063, 0064, 0065, 0066, 0067, 0070, 0071, 0072, 0073, 0074, 0075, 0076, 0077,
    0100, 0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0112, 0113, 0114, 0115, 0116, 0117,
    0120, 0121, 0122, 0123, 0124, 0125, 0126, 0127, 0130, 0131, 0132, 0133, 0134, 0135, 0136, 0137,
    0140, 0141, 0142, 0143, 0144, 0145, 0146, 0147, 0150, 0151, 0152, 0153, 0154, 0155, 0156, 0157,
    0160, 0161, 0162, 0163, 0164, 0165, 0166, 0167, 0170, 0171, 0172, 0173, 0174, 0175, 0176, 0177,
    0200, 0201, 0202, 0203, 0204, 0205, 0206, 0207, 0210, 0211, 0212, 0213, 0214, 0215, 0216, 0217,
    0220, 0221, 0222, 0223, 0224, 0225, 0226, 0227, 0230, 0231, 0232, 0233, 0234, 0235, 0236, 0237,
    0240, 0241, 0242, 0243, 0244, 0245, 0246, 0247, 0250, 0251, 0252, 0253, 0254, 0255, 0256, 0257,
    0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267, 0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277,
    0300, 0301, 0302, 0303, 0304, 0305, 0306, 0307, 0310, 0311, 0312, 0313, 0314, 0315, 0316, 0317,
    0320, 0321, 0322, 0323, 0324, 0325, 0326, 0327, 0330, 0331, 0332, 0333, 0334, 0335, 0336, 0337,
    0340, 0341, 0342, 0343, 0344, 0345, 0346, 0347, 0350, 0351, 0352, 0353, 0354, 0355, 0356, 0357,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367, 0370, 0371, 0372, 0373, 0374, 0375, 0376, 0377
};

// nonprint: -i, `ignore characters outside the ASCII range 040-0176'.  1 means ignore.  Tab
// and newline are NOT ignored -- cmp() stops a field at the newline it can see, so a table
// that ignored it would walk off the end of the line.  THE DIVERGENCE IS THE SECOND HALF:
// v7 ignored 0200..0377 and this does not, a byte above 0177 being part of a printing letter
// on a machine whose text is UTF-8.
static char nonprint[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// dict: -d, `only letters, digits and blanks are significant'.  1 means ignore.  Same
// divergence in the second half, and for the sharper reason: a Cyrillic letter is a letter.
static char dict[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

_Static_assert(sizeof zero == 256, "a table indexed by a byte has 256 entries (§11)");
_Static_assert(sizeof fold == 256, "a table indexed by a byte has 256 entries (§11)");
_Static_assert(sizeof nofold == 256, "a table indexed by a byte has 256 entries (§11)");
_Static_assert(sizeof nonprint == 256, "a table indexed by a byte has 256 entries (§11)");
_Static_assert(sizeof dict == 256, "a table indexed by a byte has 256 entries (§11)");

struct field {
    char *code;
    char *ignore;
    int nflg;
    int rflg;
    int bflg[2];
    int m[2];
    int n[2];
};

static struct field fields[NF];
static struct field proto = { nofold, zero, 0, 1, { 0, 0 }, { 0, -1 }, { 0, 0 } };

// merge()'s inputs.  v7 declared 256 of these and used at most N: merge(a, b) is never called
// with b - a greater than N, in main() or anywhere else.
static struct merg *ibuf[N];

static int cmp(char *i, char *j);
static int cmpa(char *pa, char *pb);
static int (*compare)(char *, char *) = cmpa;

static void arena(void);
static void sortpass(void);
static void merge(int a, int b);
static void msort(struct merg **base, int n);
static int rline(struct merg *mp);
static void disorder(const char *s, char *t);
static void newfile(void);
static char *setfil(int i);
static void oldfile(void);
static void safeoutfil(void);
static void cant(const char *f);
static void diag(const char *s, const char *t);
static void term(int sig);
static char *skip(char *pp, struct field *fp, int j);
static char *eol(char *p);
static void copyproto(void);
static void field(char *s, int k);
static int number(char **ppa);
static int blank(int c);
static int digit(int c);
static void sortlines(char **a, char **l);

int main(int argc, char **argv)
{
    int a;
    int fd;
    char *arg;
    struct field *p, *q;
    int lo, hi;

    copyproto();
    eargv = argv;
    while (--argc > 0) {
        if (**++argv == '-')
            for (arg = *argv;;) {
                switch (*++arg) {
                case '\0':
                    if (arg[-1] == '-')
                        eargv[eargc++] = "-";
                    break;

                case 'o':
                    if (--argc > 0)
                        outfil = *++argv;
                    continue;

                case 'T':
                    if (--argc > 0)
                        dirtry[0] = *++argv;
                    continue;

                default:
                    field(++*argv, nfields > 0);
                    break;
                }
                break;
            }
        else if (**argv == '+') {
            if (++nfields >= NF) {
                diag("too many keys", "");
                exit(1);
            }
            copyproto();
            field(++*argv, 0);
        } else
            eargv[eargc++] = *argv;
    }
    q = &fields[0];
    for (a = 1; a <= nfields; a++) {
        p = &fields[a];
        if (p->code != proto.code)
            continue;
        if (p->ignore != proto.ignore)
            continue;
        if (p->nflg != proto.nflg)
            continue;
        if (p->rflg != proto.rflg)
            continue;
        if (p->bflg[0] != proto.bflg[0])
            continue;
        if (p->bflg[1] != proto.bflg[1])
            continue;
        p->code    = q->code;
        p->ignore  = q->ignore;
        p->nflg    = q->nflg;
        p->rflg    = q->rflg;
        p->bflg[0] = p->bflg[1] = q->bflg[0];
    }
    if (eargc == 0)
        eargv[eargc++] = "-";
    if (cflg && eargc > 1) {
        diag("can check only 1 file", "");
        exit(1);
    }
    safeoutfil();

    arena();

    fd = -1;
    for (dirs = dirtry; *dirs; dirs++) {
        // §6: the -T argument is the user's and has no length.  snprintf reports what the
        // name WOULD have taken, so an over-long directory is a diagnostic and not a walk
        // past the end of file1[].
        a = snprintf(file1, sizeof file1, "%s/stm%05uaa", *dirs, (unsigned)getpid());
        if (a < 0 || (unsigned)a >= sizeof file1) {
            diag("temp directory name too long: ", *dirs);
            exit(1);
        }
        filep = file1 + a - 2;
        if ((fd = creat(file, 0600)) >= 0)
            break;
    }
    if (fd < 0) {
        diag("can't locate temp", "");
        exit(1);
    }
    close(fd);
    signal(SIGHUP, term);
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, term);
    signal(SIGPIPE, term);
    signal(SIGTERM, term);
    nfiles = eargc;
    if (!mflg && !cflg) {
        sortpass();
        fclose(stdin);
    }
    for (lo = (mflg | cflg) ? 0 : eargc; lo + N < nfiles || (unsafeout && lo < eargc); lo = hi) {
        hi = lo + N;
        if (hi >= nfiles)
            hi = nfiles;
        newfile();
        merge(lo, hi);
    }
    if (lo != nfiles) {
        oldfile();
        merge(lo, nfiles);
    }
    error = 0;
    term(0);
    return 0; // not reached: term() exits
}

//
// Claim the arena.
//
// v7 asked for `end + 16*2048' bytes and backed off in 512-byte clicks until brk() took it,
// then gave one more click back `for recursion'.  Three things are different here and only
// the first is arithmetic.
//
//   * The kernel grants the break a PAGE at a time (pground() in kernel/sys1.c sbreak), so
//     512-byte steps would have asked twelve times for each page they gave up.  Growing a
//     page at a time until it is refused reaches the same ceiling in at most 28 calls.
//
//   * There is nothing to keep back for the stack.  On the PDP-11 the stack grew down into
//     the far end of the data segment; here it is its own four pages at 070000 and the heap's
//     ceiling is exactly where it begins (estabur()'s `nt + nd > USTKPAGE * PGSZ', in
//     kernel/utab.c).  The two cannot meet.
//
//   * There IS something to keep back for stdio, which v7 had no reason to think about
//     because its buffers came out of the same brk() it had just exhausted and it accepted
//     the consequence.  Here the consequence is silent: filbuf()/flsbuf() set _IOUNBUF when
//     malloc(BUFSIZ) fails and do one read(2) or write(2) per BYTE afterwards.
//
// The reservation is taken by allocating it and freeing it rather than by subtracting it,
// and that is the point rather than a detail.  malloc grows the break a page at a time and
// never gives a page back, so one malloc of the whole reservation forces the break up to
// cover it and leaves it on the free list, where the nine fopen()s below are the only things
// that will ask for it.  A reserve that is computed can be computed wrong -- malloc's own
// rounding would have made a subtraction of (N+2)*BUFSIZ nearly half short.  One that is
// allocated cannot be, and its failure is a checked NULL instead of a silent slowdown.
//
static void arena(void)
{
    char *base, *top, *p;
    unsigned avail;

    p = malloc(STDIORESERVE);
    if (p == NULL) {
        diag("out of memory", "");
        exit(1);
    }
    free(p);

    base = sbrk(0);
    if (base == NULL) {
        diag("out of memory", "");
        exit(1);
    }
    while (sbrk(wtob(PGSZ)) != NULL)
        continue;
    top = sbrk(0);

    lspace = (char **)base;
    avail  = top - base;

    // Every line costs one pointer plus its share of the text.  v7 wrote the 10 bytes of
    // that as `5*(sizeof(char *)/sizeof(char))', which comes out 30 here -- the right answer
    // for the wrong reason, since `sizeof(char *) == 6' had eaten the whole expression and
    // it would have said 30 whatever the text budget was.
    //
    // The floor is one line's worth of TEXT, not one line: a pass may only start a line
    // while L bytes of text are left, so ntext below L would take no lines at all and
    // sortpass() would spin writing empty runs.
    if (avail < (unsigned)MERGRESERVE + (unsigned)MINLINES * (sizeof(char *) + AVGTEXT)) {
        diag("out of memory", "");
        exit(1);
    }
    nlines = (avail - MERGRESERVE) / (sizeof(char *) + AVGTEXT);
    ntext  = nlines * AVGTEXT;
    tspace = (char *)(lspace + nlines);
}

//
// The sort pass: fill the arena, sort it, write it out, repeat until the input is done.
//
static void sortpass(void)
{
    char *cp;
    char **lp;
    int c;
    int done;
    int i;
    int slots, room, len;
    char *f;

    done = 0;
    i    = 0;
    c    = EOF;
    do {
        cp    = tspace;
        lp    = lspace;
        slots = nlines;
        room  = ntext;
        // A line is at most L bytes, so a pass starts another one only while a whole one
        // would still fit -- and then the bound inside the line is a COUNT and not a pointer
        // comparison, which matters here: the arena test is once a line, but the length test
        // is once a byte, and a fat-pointer relational is two out-of-line calls.  v7 tested
        // the arena at the top of the line and then wrote the line with no bound at all,
        // which is §6's recurring finding and here would have run off the break.
        while (slots > 0 && room >= L) {
            *lp++ = cp;
            slots--;
            len = 0;
            while (c != '\n') {
                if (c != EOF) {
                    if (len >= L - 1) {
                        diag("line too long", "");
                        term(0);
                    }
                    *cp++ = c;
                    len++;
                    c = getc(is);
                    continue;
                } else if (is)
                    fclose(is);
                if (i < eargc) {
                    if ((f = setfil(i++)) == 0)
                        is = stdin;
                    else if ((is = fopen(f, "r")) == NULL)
                        cant(f);
                    c = getc(is);
                } else
                    break;
            }
            *cp++ = '\n';
            len++;
            room -= len;
            if (c == EOF) {
                done++;
                // The last line had no newline of its own: keep it, with the one just
                // supplied.  v7 dropped it here -- the same bug rev and uniq had.  An entry
                // holding nothing BUT that newline is the ordinary end of a file and goes.
                if (len == 1)
                    lp--;
                break;
            }
            c = getc(is);
        }
        sortlines(lspace, lp);
        if (done == 0 || nfiles != eargc)
            newfile();
        else
            oldfile();
        while (lp > lspace) {
            cp = *--lp;
            if (*cp)
                do
                    putc(*cp, os);
                while (*cp++ != '\n');
        }
        fclose(os);
    } while (done == 0);
}

static void merge(int a, int b)
{
    struct merg *p;
    char *cp, *dp;
    int i;
    struct merg **ip, *jp;
    char *f;
    int j;
    int k, l;
    int muflg;
    int cval;

    p = (struct merg *)lspace;
    j = 0;
    for (i = a; i < b; i++) {
        f = setfil(i);
        if (f == 0)
            p->b = stdin;
        else if ((p->b = fopen(f, "r")) == NULL)
            cant(f);
        ibuf[j] = p;
        if (!rline(p))
            j++;
        p++;
    }

    do {
        i = j;
        msort(ibuf, i);
        l = 0;
        while (i--) {
            cp = ibuf[i]->l;
            if (*cp == '\0') {
                l = 1;
                if (rline(ibuf[i])) {
                    k = i;
                    while (++k < j)
                        ibuf[k - 1] = ibuf[k];
                    j--;
                }
            }
        }
    } while (l);

    muflg = (mflg && uflg) || cflg;
    i     = j;
    while (i > 0) {
        cp = ibuf[i - 1]->l;
        // `i < 2' is not v7's, and it guards a read of ibuf[-1] that is unreachable rather
        // than a bug that fires: every path that brings i down to 1 has already set muflg,
        // and entering with j == 1 needs -m or -c, which set it too.  It is written down
        // because the reasoning is what makes the line safe, and the reasoning is not in it.
        if (!cflg && (uflg == 0 || muflg || i < 2 || (*compare)(ibuf[i - 1]->l, ibuf[i - 2]->l)))
            do
                putc(*cp, os);
            while (*cp++ != '\n');
        if (muflg) {
            cp = ibuf[i - 1]->l;
            dp = p->l;
            do {
            } while ((*dp++ = *cp++) != '\n');
        }
        for (;;) {
            if (rline(ibuf[i - 1])) {
                i--;
                if (i == 0)
                    break;
                if (i == 1)
                    muflg = uflg;
            }
            ip = &ibuf[i];
            while (--ip > ibuf && (*compare)(ip[0]->l, ip[-1]->l) < 0) {
                jp        = *ip;
                *ip       = *(ip - 1);
                *(ip - 1) = jp;
            }
            if (!muflg)
                break;
            cval = (*compare)(ibuf[i - 1]->l, p->l);
            if (cflg) {
                if (cval > 0)
                    disorder("disorder:", ibuf[i - 1]->l);
                else if (uflg && cval == 0)
                    disorder("nonunique:", ibuf[i - 1]->l);
            } else if (cval == 0)
                continue;
            break;
        }
    }
    p = (struct merg *)lspace;
    for (i = a; i < b; i++) {
        fclose(p->b);
        p++;
        if (i >= eargc)
            unlink(setfil(i));
    }
    fclose(os);
}

//
// Order the merge inputs.  v7 handed this to its own qsort() with the array cast from
// `struct merg **' to `char **', which is a reinterpretation and not a conversion: a char *
// here carries bit 48 and a byte offset that a pointer to a struct does not have.  The
// fan-in is N, so an insertion sort in the array's own type costs nothing and says what it
// means.  The predicate is the one merge()'s own bubble already used.
//
// THE SECOND THING THAT CAST WAS DOING IS THE PART A REPLACEMENT LOSES.  v7's qsort also
// MARKED duplicates under -u -- `**k++ = 0' -- and it reached mp->l[0] only through the same
// identity; merge()'s loop reads the mark back to decide which stream to advance.  An
// insertion sort that only sorted would leave `sort -u' over a merge quietly keeping every
// duplicate, and no case that did not merge would notice.  The scan runs DOWNWARD so that a
// line already marked is never the one compared against.
//
// compare() IS INVERTED: a positive answer means the FIRST argument sorts earlier, so the
// array comes out descending and merge() takes ibuf[i-1] first.  Anything that "fixed" the
// sign here would turn the program's whole output upside down.
//
static void msort(struct merg **v, int n)
{
    struct merg *t;
    int i, j;

    for (i = 1; i < n; i++) {
        t = v[i];
        for (j = i; j > 0 && (*compare)(t->l, v[j - 1]->l) < 0; j--)
            v[j] = v[j - 1];
        v[j] = t;
    }
    if (uflg)
        for (i = n - 1; i > 0; i--)
            if ((*compare)(v[i]->l, v[i - 1]->l) == 0)
                v[i]->l[0] = '\0';
}

static int rline(struct merg *mp)
{
    char *cp;
    FILE *bp;
    int c;
    int n;

    bp = mp->b;
    cp = mp->l;
    n  = 0;
    do {
        c = getc(bp);
        if (c == EOF) {
            if (n == 0)
                return 1; // a real end of file
            // A final line with no newline of its own: v7 threw it away, as rev's and
            // uniq's readers did.  Supply the newline; the call after this one sees the end
            // of file with nothing read and reports it, so no extra state is needed.
            c = '\n';
        }
        // v7 wrote `if (cp>=ce) cp--;' -- it overwrote the last byte of the buffer for as
        // long as the line went on, so a long line came out corrupted, silently, and only on
        // the merge path, while the sort path had no limit at all.  sort.1.umm's BUGS said so.
        // It says something else now: one limit, both paths, and a diagnostic.
        // l[] is L bytes and the newline is stored INSIDE this loop, so n may reach L-1:
        // a line of L bytes, terminator included, is the longest that fits and does fit.
        // (sortpass()'s test is one tighter because it appends the newline afterwards.)
        if (n >= L) {
            diag("line too long", "");
            term(0);
        }
        cp[n++] = c;
    } while (c != '\n');
    return 0;
}

static void disorder(const char *s, char *t)
{
    char *u;

    for (u = t; *u != '\n'; u++)
        continue;
    *u = 0;
    diag(s, t);
    term(0);
}

static void newfile(void)
{
    char *f;

    // setfil()'s suffix is two letters, so it has 676 names and then walks off the end of
    // the alphabet -- split(1)'s 677th-piece bug in another shape (task C5a).  The refusal
    // goes here rather than in setfil(), which term() also calls and which must not exit.
    if (nfiles - eargc >= MAXTMPFILES) {
        diag("too many temporary files", "");
        term(0);
    }
    f = setfil(nfiles);
    if ((os = fopen(f, "w")) == NULL) {
        diag("can't create ", f);
        term(0);
    }
    nfiles++;
}

static char *setfil(int i)
{
    if (i < eargc) {
        if (eargv[i][0] == '-' && eargv[i][1] == '\0')
            return 0;
        return eargv[i];
    }
    i -= eargc;
    filep[0] = i / 26 + 'a';
    filep[1] = i % 26 + 'a';
    return file;
}

static void oldfile(void)
{
    if (outfil) {
        if ((os = fopen(outfil, "w")) == NULL) {
            diag("can't create ", outfil);
            term(0);
        }
    } else
        os = stdout;
}

static void safeoutfil(void)
{
    int i;
    struct stat obuf, ibuf2;

    if (!mflg || outfil == 0)
        return;
    if (stat(outfil, &obuf) == -1)
        return;
    for (i = eargc - N; i < eargc; i++) { /*-N is suff., not nec.*/
        if (i < 0)
            continue;
        if (stat(eargv[i], &ibuf2) == -1)
            continue;
        if (obuf.st_dev == ibuf2.st_dev && obuf.st_ino == ibuf2.st_ino)
            unsafeout++;
    }
}

static void cant(const char *f)
{
    diag("can't open ", f);
    term(0);
}

static void diag(const char *s, const char *t)
{
    fputs("sort: ", stderr);
    fputs(s, stderr);
    fputs(t, stderr);
    fputs("\n", stderr);
}

static void term(int sig)
{
    int i;

    (void)sig;
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    if (nfiles == eargc)
        nfiles++;
    for (i = eargc; i <= nfiles; i++) { /*<= in case of interrupt*/
        unlink(setfil(i));              /*with nfiles not updated*/
    }
    exit(error);
}

//
// The comparison, and the program's whole output.  A positive answer means i sorts BEFORE j:
// the array is written out backwards (sortpass() and merge() both take the high end first).
//
static int cmp(char *i, char *j)
{
    char *pa, *pb;
    char *code, *ignore;
    int a, b;
    int k;
    char *la, *lb;
    int sa;
    int sb;
    char *ipa, *ipb, *jpa, *jpb;
    struct field *fp;

    for (k = nfields > 0; k <= nfields; k++) {
        fp = &fields[k];
        pa = i;
        pb = j;
        if (k) {
            la = skip(pa, fp, 1);
            pa = skip(pa, fp, 0);
            lb = skip(pb, fp, 1);
            pb = skip(pb, fp, 0);
        } else {
            la = eol(pa);
            lb = eol(pb);
        }
        if (fp->nflg) {
            while (blank(*pa))
                pa++;
            while (blank(*pb))
                pb++;
            sa = sb = fp->rflg;
            if (*pa == '-') {
                pa++;
                sa = -sa;
            }
            if (*pb == '-') {
                pb++;
                sb = -sb;
            }
            for (ipa = pa; ipa < la && digit(*ipa); ipa++)
                continue;
            for (ipb = pb; ipb < lb && digit(*ipb); ipb++)
                continue;
            jpa = ipa;
            jpb = ipb;
            a   = 0;
            if (sa == sb)
                while (ipa > pa && ipb > pb)
                    if ((b = *--ipb - *--ipa))
                        a = b;
            while (ipa > pa)
                if (*--ipa != '0')
                    return -sa;
            while (ipb > pb)
                if (*--ipb != '0')
                    return sb;
            if (a)
                return a * sa;
            if (*(pa = jpa) == '.')
                pa++;
            if (*(pb = jpb) == '.')
                pb++;
            if (sa == sb)
                while (pa < la && digit(*pa) && pb < lb && digit(*pb))
                    if ((a = *pb++ - *pa++))
                        return a * sa;
            while (pa < la && digit(*pa))
                if (*pa++ != '0')
                    return -sa;
            while (pb < lb && digit(*pb))
                if (*pb++ != '0')
                    return sb;
            continue;
        }
        code   = fp->code;
        ignore = fp->ignore;
    loop:
        while (ignore[*pa])
            pa++;
        while (ignore[*pb])
            pb++;
        if (pa >= la || *pa == '\n') {
            if (pb < lb && *pb != '\n')
                return fp->rflg;
            continue;
        }
        if (pb >= lb || *pb == '\n')
            return -fp->rflg;
        if ((sa = code[*pb++] - code[*pa++]) == 0)
            goto loop;
        return sa * fp->rflg;
    }
    if (uflg)
        return 0;
    return cmpa(i, j);
}

//
// The whole line, byte for byte, and the default when no flag has been given.  It has no
// pointer relational in it at all: it stops on the newline both lines are known to have.
//
// `*pb > *pa' was a SIGNED byte comparison on the PDP-11 and is unsigned here, so v7 sorted
// a byte above 0177 below every ASCII one and this sorts it above.  Byte order and code-point
// order are the same thing in UTF-8, so the unsigned answer is the one a user wants; it is
// left as it stands rather than being made to imitate the PDP-11.
//
static int cmpa(char *pa, char *pb)
{
    while (*pa == *pb) {
        if (*pa++ == '\n')
            return 0;
        pb++;
    }
    return (*pa == '\n'   ? fields[0].rflg
            : *pb == '\n' ? -fields[0].rflg
            : *pb > *pa   ? fields[0].rflg
                          : -fields[0].rflg);
}

static char *skip(char *pp, struct field *fp, int j)
{
    int i;
    char *p;

    p = pp;
    if ((i = fp->m[j]) < 0)
        return eol(p);
    while (i-- > 0) {
        if (tabchar != 0) {
            while (*p != tabchar)
                if (*p != '\n')
                    p++;
                else
                    goto ret;
            p++;
        } else {
            while (blank(*p))
                p++;
            while (!blank(*p))
                if (*p != '\n')
                    p++;
                else
                    goto ret;
        }
    }
    if (fp->bflg[j])
        while (blank(*p))
            p++;
    i = fp->n[j];
    while (i-- > 0) {
        if (*p != '\n')
            p++;
        else
            goto ret;
    }
ret:
    return p;
}

static char *eol(char *p)
{
    while (*p != '\n')
        p++;
    return p;
}

//
// v7 copied `struct field' word by word through an `int *', which on this machine truncates
// the two char * members: an int is bits 41-1 and a fat pointer lives in bit 48 and bits
// 47-45.  main() already walks the same struct member by member; so does this.
//
static void copyproto(void)
{
    struct field *q;

    q          = &fields[nfields];
    q->code    = proto.code;
    q->ignore  = proto.ignore;
    q->nflg    = proto.nflg;
    q->rflg    = proto.rflg;
    q->bflg[0] = proto.bflg[0];
    q->bflg[1] = proto.bflg[1];
    q->m[0]    = proto.m[0];
    q->m[1]    = proto.m[1];
    q->n[0]    = proto.n[0];
    q->n[1]    = proto.n[1];
}

static void field(char *s, int k)
{
    struct field *p;
    int d;

    p = &fields[nfields];
    d = 0;
    for (; *s != 0; s++) {
        switch (*s) {
        case '\0':
            return;

        case 'b':
            p->bflg[k]++;
            break;

        case 'd':
            p->ignore = dict;
            break;

        case 'f':
            p->code = fold;
            break;

        case 'i':
            p->ignore = nonprint;
            break;

        case 'c':
            cflg = 1;
            continue;

        case 'm':
            mflg = 1;
            continue;

        case 'n':
            p->nflg++;
            break;

        case 't':
            tabchar = *++s;
            if (tabchar == 0)
                s--;
            continue;

        case 'r':
            p->rflg = -1;
            continue;

        case 'u':
            uflg = 1;
            break;

        case '.':
            if (p->m[k] == -1) /* -m.n with m missing */
                p->m[k] = 0;
            // The distance from m[] to n[], used as a subscript delta so that the digits
            // after the dot land in n[].  v7's, and it still says what it says here.
            d = &fields[0].n[0] - &fields[0].m[0];
            // FALLTHROUGH

        default:
            p->m[k + d] = number(&s);
        }
        compare = cmp;
    }
}

static int number(char **ppa)
{
    int n;
    char *pa;

    pa = *ppa;
    n  = 0;
    while (digit(*pa)) {
        n    = n * 10 + *pa - '0';
        *ppa = pa++;
    }
    return n;
}

static int blank(int c)
{
    if (c == ' ' || c == '\t')
        return 1;
    return 0;
}

//
// <ctype.h>'s isdigit() indexes a 129-entry table and only isascii() is safe above 0177
// (lib/libc/gen/ctype_.c, §11).  A numeric key is ASCII digits by definition, so the test is
// written out rather than guarded seven times.
//
static int digit(int c)
{
    return c >= '0' && c <= '9';
}

#define qsexc(p, q) \
    t  = *p;        \
    *p = *q;        \
    *q = t
#define qstexc(p, q, r) \
    t  = *p;            \
    *p = *r;            \
    *r = *q;            \
    *q = t

//
// v7 called this qsort().  libc has a qsort() of its own and <stdlib.h> declares C11's
// four-argument one, so the name had to go: §1's rule, and mkdir.c's readdir->listdir is the
// precedent.  b6ld would not have complained -- an archive member is pulled only for a symbol
// that is still undefined (cmd/ld/library.c) -- which is exactly why the rename is on sight.
//
static void sortlines(char **a, char **l)
{
    char **i, **j;
    char **k;
    char **lp, **hp;
    int c;
    char *t;
    unsigned n;

start:
    if ((n = l - a) <= 1)
        return;

    n /= 2;
    hp = lp = a + n;
    i       = a;
    j       = l - 1;

    for (;;) {
        if (i < lp) {
            if ((c = (*compare)(*i, *lp)) == 0) {
                --lp;
                qsexc(i, lp);
                continue;
            }
            if (c < 0) {
                ++i;
                continue;
            }
        }

    loop:
        if (j > hp) {
            if ((c = (*compare)(*hp, *j)) == 0) {
                ++hp;
                qsexc(hp, j);
                goto loop;
            }
            if (c > 0) {
                if (i == lp) {
                    ++hp;
                    qstexc(i, hp, j);
                    i = ++lp;
                    goto loop;
                }
                qsexc(i, j);
                --j;
                ++i;
                continue;
            }
            --j;
            goto loop;
        }

        if (i == lp) {
            // -u marks a duplicate by poking a NUL through the line pointer, into the text
            // arena; sortpass()'s writer reads it back as `if (*cp)'.  v7's, and it works
            // here for the reason it worked there: a line always has a byte before its
            // newline, so a NUL first byte cannot be anything else.
            if (uflg)
                for (k = lp + 1; k <= hp;)
                    **k++ = '\0';
            if (lp - a >= l - hp) {
                sortlines(hp + 1, l);
                l = lp;
            } else {
                sortlines(a, lp);
                a = hp + 1;
            }
            goto start;
        }

        --lp;
        qstexc(j, lp, i);
        j = --hp;
    }
}
