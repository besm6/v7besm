/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// dd -- copy a file, blocking and converting as asked.
//
// The v7 program, unchanged in what it does: read records of `ibs' bytes, optionally put
// every byte through a conversion, write records of `obs' bytes, and report how many whole
// and partial records went each way.  Task C4b (../README.md), and the reason it comes before
// mkfs(1) and fsck(1) is that it is how anything gets copied to or from a RAW DEVICE.  It
// reports RECORDS and not blocks, so ../README.md SS4's 1024-byte reporting unit does not
// reach this program: no number it prints changes unit, and there is no KBPB anywhere here.
//
// WHAT THE PORT HAD TO CHANGE, beyond the mechanical C11 pass:
//
//  1. A BLOCK IS 3072 BYTES, so `ibs' and `obs' default to BSIZE and the `b' suffix
//     multiplies by BSIZE.  v7's were 512, which was a PDP-11 disk block; nothing on this
//     machine is 512 bytes.  The consequence is not stylistic.  physio() rejects a count
//     that is not a whole number of words, and 512 % NBPW is 2, so with v7's default the
//     commonest invocation this program has --
//
//         dd if=/dev/rmd0 of=/tmp/x count=1
//
//     -- never reached the disk at all: EFAULT, `read: Bad address', and (see B1 below) an
//     exit status of 0.  With BSIZE it is one aligned whole-block transfer and works with
//     no bs= at all.  dd.1.umm says so and marks it Note:.
//
//  2. `w' IS SIX BYTES, and that one came free: v7 wrote `n *= sizeof(int)', which was 2 on
//     a PDP-11 and is NBPW here.  A free divergence is still a divergence, so it is spelt
//     NBPW with a _Static_assert under it and written down in dd.1.umm -- otherwise a user
//     reading v7's page asks for `512w' and gets three times what they meant.  The pleasing
//     part is that 1b, 3k and 512w are now all the same number.
//
//  3. THE ZERO-FILL LOOP WAS A char * ORDERING TEST.  v7 wrote
//
//         for (ip = ibuf+ibs; ip > ibuf;) *--ip = 0;
//
//     and when this was ported `>' between two char * gave the wrong answer (../README.md
//     SS2, which the compiler has since fixed).  Both bounds were already known as counts,
//     so it is a word loop over the same bytes now -- six times less work, which is worth
//     having on its own: under conv=noerror or conv=sync it runs before EVERY read.
//
//  4. THE BUFFERS HAVE TO BE ALIGNED, and C cannot ask for it.  A raw transfer through
//     /dev/rmd0 goes physio() -> mdstrategy() and those two impose four conditions
//     (../df/README.md is the account):
//
//       * the buffer must start at byte #0 of a word     (physio, kernel/dev/bio.c)
//       * the count must be a whole number of BSIZEs     (MDTRACK == BSIZEW, dev/md.c)
//       * the buffer's WORD ADDRESS must be MDALIGN-aligned          (dev/md.c)
//       * the seek offset must be a multiple of BSIZE    (physio truncates, silently)
//
//     sbrk() gives the first for nothing, and getbuf() below gives the third by stepping
//     the BREAK to a boundary before taking the buffer.  The second is item 1's business.
//     THE FOURTH IS UNREACHABLE, which is the property that makes dd safe to hand a device:
//     every offset dd can produce is a multiple of obs -- seek= gives n*obs and each write
//     advances by obs -- so if obs is a whole number of blocks every offset is block
//     aligned, and if it is not, the very first transfer fails loudly on the COUNT test
//     before any offset can be truncated.  dd either satisfies all four or fails on one of
//     the three noisy ones; it cannot trip the silent one.
//
//  5. `(char *)-1' AS THE sbrk FAILURE TEST, twice.  This libc's sbrk() returns NULL and
//     never -1 (lib/libc/sys/sbrk.c), so v7's test could not fire and an out-of-memory dd
//     would have fallen through and started writing bytes through word 0.  The cast is
//     forbidden besides: fabricating a fat pointer out of an integer gets the bit-48 marker
//     and the offset field wrong (lib/README.md).
//
//  6. ONE `%D', which is not a conversion here.  doprnt.c echoes an unknown conversion
//     verbatim AND consumes no argument, so it printed the two characters `%D' and the
//     number never appeared.  It is the last conversion in its format, so unlike ls(1)'s it
//     desynchronised nothing.
//
//  7. term() IS BOTH A SIGNAL HANDLER AND AN ORDINARY CALL.  A C11 handler is
//     void (*)(int), so the three internal callers pass a dummy -- ../ed/ed.c's quit(0) is
//     the precedent.  The installation itself is untouched: v7 wrote the correct
//     `signal(SIGINT, SIG_IGN) != SIG_IGN' here, not the truncating idiom ed(1) had.
//
// THREE UPSTREAM BUGS, FIXED RATHER THAN CARRIED.  None is about this machine.
//
//  B1. SIX FAILURE EXITS REPORTED SUCCESS -- `bad arg', `cannot open', `cannot create',
//      `counts: cannot be zero', `not enough memory', and term() after a read or a write
//      error, all exit(0).  So no shell script could tell a completed copy from a refused
//      one: `dd if=/dev/rmd0 of=/tmp/img count=200 && echo saved' printed `saved' after a
//      write that failed.  They are exit(1) now, through fatal().  ../df/df.c's item 5 is
//      the same fix in the neighbouring program.  Two deliberate non-changes inside it: an
//      INTERRUPT still exits 0, there being no diagnostic on that path and an interrupt not
//      being a failure of the program; and number()'s out-of-range already exited 1, which
//      is the evidence that the rest was an oversight and not a policy.
//
//  B2. conv=swab SKIPPED A PAIR.  The swap loop advances ip by two and swaps one pair per
//      iteration, so `ibc >> 1' is already the pair count -- and v7 then wrote
//      `(ibc>>1) & ~1', rounding that count DOWN TO EVEN.  It is not only a partial-record
//      bug: `dd bs=2 conv=swab' yields c == 0, the `cflag&SWAB && c' guard is then false,
//      and the whole file is copied UNSWAPPED with no diagnostic.  The claim the fix makes
//      is exactly this and no more: conv=swab skipped the final pair of a record whenever
//      the record held an odd number of pairs, and did nothing at all when it held one.  At
//      this port's defaults a record is 1536 pairs and the two spellings agree, so the risk
//      of the change is confined to the cases that were already wrong.
//
//  B3. THE READ-ERROR RECOVERY SCAN WAS OFF BY ONE.  It walks ibuf for the last non-zero
//      byte and assigns that INDEX to ibc, which is a COUNT, so the last recovered byte was
//      always dropped -- and if only ibuf[0] survived, ibc came out 0, which the very next
//      test reads as end of file: dd stopped and called it a clean finish.  It is c+1 now.
//      What is NOT wrong with that scan, and was suspected: it does not run when the
//      zero-fill has been skipped.  It cannot.  The path to it exits when NERR is clear,
//      and the fill's condition is NERR|SYNC, which NERR implies.
//
// LOOKED AT AND LEFT ALONE, with the reason:
//
//   * number() accepts trailing garbage -- the switch has no default, so `ibs=512junk' is
//     512.  A lax parser, not a wrong answer on well-formed input; diagnosing it would
//     change the accepted command language for a case nobody has hit.
//   * files= counts one phantom partial record per file boundary, ibc == 0 falling through
//     to the `ibc != ibs' test.  Unreachable at the default files=1, and there is no tape
//     on this machine, so a divergence invented for it could not be tested -- which is the
//     argument ../df/README.md makes for quot's un-cleared accumulators.
//   * skip= ignores its read()'s result.  Not invisible here: on a raw device the same ibs
//     that failed the skip fails the copy read a moment later, loudly.
//   * conv=noerror cannot skip a block this device refuses outright.  mdstrategy()'s bad:
//     path moves nothing, the buffer is left entirely zero, B3's scan yields ibc == 0 and
//     that is indistinguishable from end of file.  Fixing it means inventing a
//     skip-and-continue policy v7 never had.  dd.1.umm's BUGS says so instead.
//   * an interrupt loses the partial output record -- term() does not flush.  v7's, and a
//     "fix" would change what an interrupt means.
//   * `c &= 0377' after reading a byte is a no-op now that char is unsigned.  Harmless, and
//     removing it would claim something.
//   * stats() from a signal handler is not async-signal-safe.  stderr is _IOUNBUF here --
//     one write(2) per character, no buffer to re-enter -- and the process exits at once.
//
// NOT SETUID, and it must not become so.  /dev/rmd0 is mode 0600 because that one node is
// the contents of every file on the volume; a setuid dd would hand the whole filesystem to
// anyone who could think of an offset AND let them overwrite it.  dd is a root-only program
// where a device is concerned and dd.1.umm says so.  ../README.md SS8 is the rule.
//

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>

// The `w' suffix multiplies by one word, and the whole point of naming it NBPW rather than
// leaving v7's sizeof(int) is that the constant then has a home to be read out of.
_Static_assert(sizeof(int) == NBPW, "the `w' suffix multiplies by one word");
_Static_assert(BSIZE == BSIZEW * NBPW, "a block must be BSIZEW words of NBPW bytes");

// mdstrategy()'s half-zone, which is also a block: kernel/dev/md.c refuses a transfer whose
// physical address is not a multiple of it.  A page is PGSZ words and mapping preserves the
// offset within a page, so aligning the virtual address aligns the physical one -- provided
// PGSZ is a multiple of this, which it is.
#define MDALIGN BSIZEW
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");

#define BIG   32767
#define LCASE 01
#define UCASE 02
#define SWAB  04
#define NERR  010
#define SYNC  020

static int cflag;
static int fflag;
static int skip;
static int seekn;
static int count;
static int files = 1;
static char *string;
static char *ifile;
static char *ofile;
static char *ibuf;
static char *obuf;
static int ibs = BSIZE;
static int obs = BSIZE;
static int bs;
static int cbs;
static int ibc;
static int obc;
static int cbc;
static int nifr;
static int nipr;
static int nofr;
static int nopr;
static int ntrunc;
static int ibf;
static int obf;
static char *op;
static int nspace;

// The three conversion tables, verbatim from v7.  The bound is written out because a table
// indexed by a character must have 256 entries on this machine (../README.md SS11) and an
// explicit 256 makes that a thing the compiler checks rather than a thing this comment
// claims.  const, so they sit in the constant pool rather than in data.
static const char etoa[256] = {
    0000, 0001, 0002, 0003, 0234, 0011, 0206, 0177, 0227, 0215, 0216, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0235, 0205, 0010, 0207, 0030, 0031, 0222, 0217, 0034, 0035, 0036, 0037,
    0200, 0201, 0202, 0203, 0204, 0012, 0027, 0033, 0210, 0211, 0212, 0213, 0214, 0005, 0006, 0007,
    0220, 0221, 0026, 0223, 0224, 0225, 0226, 0004, 0230, 0231, 0232, 0233, 0024, 0025, 0236, 0032,
    0040, 0240, 0241, 0242, 0243, 0244, 0245, 0246, 0247, 0250, 0133, 0056, 0074, 0050, 0053, 0041,
    0046, 0251, 0252, 0253, 0254, 0255, 0256, 0257, 0260, 0261, 0135, 0044, 0052, 0051, 0073, 0136,
    0055, 0057, 0262, 0263, 0264, 0265, 0266, 0267, 0270, 0271, 0174, 0054, 0045, 0137, 0076, 0077,
    0272, 0273, 0274, 0275, 0276, 0277, 0300, 0301, 0302, 0140, 0072, 0043, 0100, 0047, 0075, 0042,
    0303, 0141, 0142, 0143, 0144, 0145, 0146, 0147, 0150, 0151, 0304, 0305, 0306, 0307, 0310, 0311,
    0312, 0152, 0153, 0154, 0155, 0156, 0157, 0160, 0161, 0162, 0313, 0314, 0315, 0316, 0317, 0320,
    0321, 0176, 0163, 0164, 0165, 0166, 0167, 0170, 0171, 0172, 0322, 0323, 0324, 0325, 0326, 0327,
    0330, 0331, 0332, 0333, 0334, 0335, 0336, 0337, 0340, 0341, 0342, 0343, 0344, 0345, 0346, 0347,
    0173, 0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0350, 0351, 0352, 0353, 0354, 0355,
    0175, 0112, 0113, 0114, 0115, 0116, 0117, 0120, 0121, 0122, 0356, 0357, 0360, 0361, 0362, 0363,
    0134, 0237, 0123, 0124, 0125, 0126, 0127, 0130, 0131, 0132, 0364, 0365, 0366, 0367, 0370, 0371,
    0060, 0061, 0062, 0063, 0064, 0065, 0066, 0067, 0070, 0071, 0372, 0373, 0374, 0375, 0376, 0377,
};

static const char atoe[256] = {
    0000, 0001, 0002, 0003, 0067, 0055, 0056, 0057, 0026, 0005, 0045, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0074, 0075, 0062, 0046, 0030, 0031, 0077, 0047, 0034, 0035, 0036, 0037,
    0100, 0117, 0177, 0173, 0133, 0154, 0120, 0175, 0115, 0135, 0134, 0116, 0153, 0140, 0113, 0141,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367, 0370, 0371, 0172, 0136, 0114, 0176, 0156, 0157,
    0174, 0301, 0302, 0303, 0304, 0305, 0306, 0307, 0310, 0311, 0321, 0322, 0323, 0324, 0325, 0326,
    0327, 0330, 0331, 0342, 0343, 0344, 0345, 0346, 0347, 0350, 0351, 0112, 0340, 0132, 0137, 0155,
    0171, 0201, 0202, 0203, 0204, 0205, 0206, 0207, 0210, 0211, 0221, 0222, 0223, 0224, 0225, 0226,
    0227, 0230, 0231, 0242, 0243, 0244, 0245, 0246, 0247, 0250, 0251, 0300, 0152, 0320, 0241, 0007,
    0040, 0041, 0042, 0043, 0044, 0025, 0006, 0027, 0050, 0051, 0052, 0053, 0054, 0011, 0012, 0033,
    0060, 0061, 0032, 0063, 0064, 0065, 0066, 0010, 0070, 0071, 0072, 0073, 0004, 0024, 0076, 0341,
    0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0121, 0122, 0123, 0124, 0125, 0126, 0127,
    0130, 0131, 0142, 0143, 0144, 0145, 0146, 0147, 0150, 0151, 0160, 0161, 0162, 0163, 0164, 0165,
    0166, 0167, 0170, 0200, 0212, 0213, 0214, 0215, 0216, 0217, 0220, 0232, 0233, 0234, 0235, 0236,
    0237, 0240, 0252, 0253, 0254, 0255, 0256, 0257, 0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
    0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277, 0312, 0313, 0314, 0315, 0316, 0317, 0332, 0333,
    0334, 0335, 0336, 0337, 0352, 0353, 0354, 0355, 0356, 0357, 0372, 0373, 0374, 0375, 0376, 0377,
};

static const char atoibm[256] = {
    0000, 0001, 0002, 0003, 0067, 0055, 0056, 0057, 0026, 0005, 0045, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0074, 0075, 0062, 0046, 0030, 0031, 0077, 0047, 0034, 0035, 0036, 0037,
    0100, 0132, 0177, 0173, 0133, 0154, 0120, 0175, 0115, 0135, 0134, 0116, 0153, 0140, 0113, 0141,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367, 0370, 0371, 0172, 0136, 0114, 0176, 0156, 0157,
    0174, 0301, 0302, 0303, 0304, 0305, 0306, 0307, 0310, 0311, 0321, 0322, 0323, 0324, 0325, 0326,
    0327, 0330, 0331, 0342, 0343, 0344, 0345, 0346, 0347, 0350, 0351, 0255, 0340, 0275, 0137, 0155,
    0171, 0201, 0202, 0203, 0204, 0205, 0206, 0207, 0210, 0211, 0221, 0222, 0223, 0224, 0225, 0226,
    0227, 0230, 0231, 0242, 0243, 0244, 0245, 0246, 0247, 0250, 0251, 0300, 0117, 0320, 0241, 0007,
    0040, 0041, 0042, 0043, 0044, 0025, 0006, 0027, 0050, 0051, 0052, 0053, 0054, 0011, 0012, 0033,
    0060, 0061, 0032, 0063, 0064, 0065, 0066, 0010, 0070, 0071, 0072, 0073, 0004, 0024, 0076, 0341,
    0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110, 0111, 0121, 0122, 0123, 0124, 0125, 0126, 0127,
    0130, 0131, 0142, 0143, 0144, 0145, 0146, 0147, 0150, 0151, 0160, 0161, 0162, 0163, 0164, 0165,
    0166, 0167, 0170, 0200, 0212, 0213, 0214, 0215, 0216, 0217, 0220, 0232, 0233, 0234, 0235, 0236,
    0237, 0240, 0252, 0253, 0254, 0255, 0256, 0257, 0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
    0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277, 0312, 0313, 0314, 0315, 0316, 0317, 0332, 0333,
    0334, 0335, 0336, 0337, 0352, 0353, 0354, 0355, 0356, 0357, 0372, 0373, 0374, 0375, 0376, 0377,
};

static void flsh(void);
static int match(const char *s);
static int number(int big);
static void cnull(int cc);
static void null(int c);
static void ascii(int cc);
static void ebcdic(int cc);
static void ibm(int cc);
static void term(int sig);
static void fatal(void);
static void stats(void);
static char *getbuf(int nbytes);

int main(int argc, char **argv)
{
    void (*conv)(int);
    char *ip;
    int *iw;
    int a, c, i;

    conv = null;
    for (c = 1; c < argc; c++) {
        string = argv[c];
        if (match("ibs=")) {
            ibs = number(BIG);
            continue;
        }
        if (match("obs=")) {
            obs = number(BIG);
            continue;
        }
        if (match("cbs=")) {
            cbs = number(BIG);
            continue;
        }
        if (match("bs=")) {
            bs = number(BIG);
            continue;
        }
        if (match("if=")) {
            ifile = string;
            continue;
        }
        if (match("of=")) {
            ofile = string;
            continue;
        }
        if (match("skip=")) {
            skip = number(BIG);
            continue;
        }
        if (match("seek=")) {
            seekn = number(BIG);
            continue;
        }
        if (match("count=")) {
            count = number(BIG);
            continue;
        }
        if (match("files=")) {
            files = number(BIG);
            continue;
        }
        if (match("conv=")) {
        cloop:
            if (match(","))
                goto cloop;
            if (*string == '\0')
                continue;
            if (match("ebcdic")) {
                conv = ebcdic;
                goto cloop;
            }
            if (match("ibm")) {
                conv = ibm;
                goto cloop;
            }
            if (match("ascii")) {
                conv = ascii;
                goto cloop;
            }
            if (match("lcase")) {
                cflag |= LCASE;
                goto cloop;
            }
            if (match("ucase")) {
                cflag |= UCASE;
                goto cloop;
            }
            if (match("swab")) {
                cflag |= SWAB;
                goto cloop;
            }
            if (match("noerror")) {
                cflag |= NERR;
                goto cloop;
            }
            if (match("sync")) {
                cflag |= SYNC;
                goto cloop;
            }
        }
        fprintf(stderr, "bad arg: %s\n", string);
        exit(1); // B1: v7 exited 0
    }
    if (conv == null && cflag & (LCASE | UCASE))
        conv = cnull;
    if (ifile)
        ibf = open(ifile, O_RDONLY);
    else
        ibf = dup(0);
    if (ibf < 0) {
        fprintf(stderr, "cannot open: %s\n", ifile ? ifile : "standard input");
        exit(1); // B1
    }
    if (ofile)
        obf = creat(ofile, 0666);
    else
        obf = dup(1);
    if (obf < 0) {
        fprintf(stderr, "cannot create: %s\n", ofile ? ofile : "standard output");
        exit(1); // B1
    }
    if (bs) {
        ibs = obs = bs;
        if (conv == null)
            fflag++;
    }
    if (ibs == 0 || obs == 0) {
        fprintf(stderr, "counts: cannot be zero\n");
        exit(1); // B1
    }
    ibuf = getbuf(ibs);
    if (fflag)
        obuf = ibuf;
    else
        obuf = getbuf(obs);
    sbrk(64); /* For good measure */
    if (ibuf == NULL || obuf == NULL) {
        fprintf(stderr, "not enough memory\n");
        exit(1); // B1
    }
    ibc = 0;
    obc = 0;
    cbc = 0;
    op  = obuf;

    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, term);
    while (skip) {
        read(ibf, ibuf, ibs);
        skip--;
    }
    while (seekn) {
        lseek(obf, (off_t)obs, SEEK_CUR);
        seekn--;
    }

loop:
    if (ibc-- == 0) {
        ibc = 0;
        if (count == 0 || nifr + nipr != count) {
            // v7's `for (ip = ibuf+ibs; ip > ibuf;) *--ip = 0' -- see item 3 in the header.
            // ibuf starts at byte #0 of a word and sbrk() rounded the allocation up to
            // whole words, so btow(ibs) words is inside it.
            if (cflag & (NERR | SYNC)) {
                iw = (int *)ibuf;
                for (i = btow(ibs); --i >= 0;)
                    iw[i] = 0;
            }
            ibc = read(ibf, ibuf, ibs);
        }
        if (ibc == -1) {
            perror("read");
            if ((cflag & NERR) == 0) {
                flsh();
                fatal(); // B1: v7 called term(), which exited 0
            }
            // Reachable only under NERR, which implies the fill above ran; see B3.
            ibc = 0;
            for (c = 0; c < ibs; c++)
                if (ibuf[c] != 0)
                    ibc = c + 1; // B3: v7 assigned the index to a count
            stats();
        }
        if (ibc == 0 && --files <= 0) {
            flsh();
            term(0);
        }
        if (ibc != ibs) {
            nipr++;
            if (cflag & SYNC)
                ibc = ibs;
        } else
            nifr++;
        ip = ibuf;
        c  = ibc >> 1; // B2: v7 wrote `(ibc>>1) & ~1'
        if (cflag & SWAB && c)
            do {
                a      = *ip++;
                ip[-1] = *ip;
                *ip++  = a;
            } while (--c);
        ip = ibuf;
        if (fflag) {
            obc = ibc;
            flsh();
            ibc = 0;
        }
        goto loop;
    }
    c = 0;
    c |= *ip++;
    c &= 0377;
    (*conv)(c);
    goto loop;
}

//
// One buffer, aligned for the raw path.
//
// MDALIGN is the finest unit the disk can name -- the half-zone -- and mdstrategy() refuses
// a transfer whose buffer address is not a multiple of it (../df/README.md, condition 3).
// C cannot ask for that alignment: there is no _Alignas reaching 512 words, and
// aligned_alloc(3) refuses anything wider than one word outright.  So the BREAK is stepped
// up to a boundary first and the buffer taken above it.
//
// Everything this needs sbrk() already promises (lib/libc/sys/sbrk.c): it hands back byte #0
// of a word, which is condition 1 for free, and it moves the break by whole words, so
// ptrword() is exact and the pad is a whole number of words.  sbrk(0) is well defined there
// and returns the current break without moving it.
//
// The pad is at most MDALIGN-1 words, is never reused, and is charged once: with ibs and obs
// whole blocks -- which is the default -- obuf follows ibuf already aligned and pays nothing.
// The bss alternative df(1) and quot(1) use is wrong here: their buffer is one block and
// known at compile time, and dd's is the user's number, so a bss version would have to carry
// two BIG-sized arrays -- some 11,900 words -- on every invocation to serve the rare one.
//
static char *getbuf(int nbytes)
{
    char *p;
    int pad;

    p = sbrk(0);
    if (p == NULL)
        return NULL;
    pad = ptrword(p) % MDALIGN;
    if (pad != 0)
        pad = MDALIGN - pad;
    p = sbrk(wtob(pad) + nbytes);
    if (p == NULL)
        return NULL;
    return p + wtob(pad);
}

static void flsh(void)
{
    int c;

    if (obc) {
        if (obc == obs)
            nofr++;
        else
            nopr++;
        c = write(obf, obuf, obc);
        if (c != obc) {
            perror("write");
            fatal(); // B1: v7 called term(), which exited 0
        }
        obc = 0;
    }
}

static int match(const char *s)
{
    char *cs;

    cs = string;
    while (*cs++ == *s)
        if (*s++ == '\0')
            goto true;
    if (*s != '\0')
        return 0;

    true : cs--;
    string = cs;
    return 1;
}

//
// A number with an optional multiplier suffix.  A long is one word here, so v7's `long n' is
// an int; and with it goes v7's `n < 0' guard, which caught a 16-bit wraparound that a
// 41-bit int does not do.  The accumulation is bounded instead, which is one comparison and
// depends on nothing about how this machine overflows.
//
static int number(int big)
{
    char *cs;
    int n;

    cs = string;
    n  = 0;
    while (*cs >= '0' && *cs <= '9') {
        n = n * 10 + *cs++ - '0';
        if (n >= big)
            break;
    }
    for (;;)
        switch (*cs++) {
        case 'k':
            n *= 1024;
            continue;

        case 'w':
            n *= NBPW; // v7: sizeof(int), which was 2 there and is 6 here
            continue;

        case 'b':
            n *= BSIZE; // v7: 512, a PDP-11 disk block
            continue;

        case '*':
        case 'x':
            string = cs;
            n *= number(BIG);
            /* FALLTHROUGH */

        case '\0':
            if (n >= big || n < 0) {
                fprintf(stderr, "dd: argument %d out of range\n", n); // v7: %D
                exit(1);
            }
            return n;
        }
    /* never gets here */
}

static void cnull(int cc)
{
    int c;

    c = cc;
    if (cflag & UCASE && c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    if (cflag & LCASE && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    null(c);
}

static void null(int c)
{
    *op = c;
    op++;
    if (++obc >= obs) {
        flsh();
        op = obuf;
    }
}

static void ascii(int cc)
{
    int c;

    c = etoa[cc] & 0377;
    if (cbs == 0) {
        cnull(c);
        return;
    }
    if (c == ' ') {
        nspace++;
        goto out;
    }
    while (nspace > 0) {
        null(' ');
        nspace--;
    }
    cnull(c);

out:
    if (++cbc >= cbs) {
        null('\n');
        cbc    = 0;
        nspace = 0;
    }
}

static void ebcdic(int cc)
{
    int c;

    c = cc;
    if (cflag & UCASE && c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    if (cflag & LCASE && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    c = atoe[c] & 0377;
    if (cbs == 0) {
        null(c);
        return;
    }
    if (cc == '\n') {
        while (cbc < cbs) {
            null(atoe[' ']);
            cbc++;
        }
        cbc = 0;
        return;
    }
    if (cbc == cbs)
        ntrunc++;
    cbc++;
    if (cbc <= cbs)
        null(c);
}

static void ibm(int cc)
{
    int c;

    c = cc;
    if (cflag & UCASE && c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    if (cflag & LCASE && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    c = atoibm[c] & 0377;
    if (cbs == 0) {
        null(c);
        return;
    }
    if (cc == '\n') {
        while (cbc < cbs) {
            null(atoibm[' ']);
            cbc++;
        }
        cbc = 0;
        return;
    }
    if (cbc == cbs)
        ntrunc++;
    cbc++;
    if (cbc <= cbs)
        null(c);
}

//
// SIGINT, and the normal end of the copy.  v7 made this both the handler and an ordinary
// call; a C11 handler is void (*)(int), so the internal callers pass a dummy -- ../ed/ed.c's
// quit(0) is the precedent.  The status stays 0: there is no diagnostic on this path, and an
// interrupt is not a failure of the program.  As in v7, an interrupt does NOT flush the
// partial output record.
//
static void term(int sig)
{
    (void)sig;
    stats();
    exit(0);
}

//
// The same report, and a status that says the copy did not happen.  v7 had no such thing:
// every one of its failure exits reported success.  See B1 in the header.
//
static void fatal(void)
{
    stats();
    exit(1);
}

static void stats(void)
{
    // v7 printed these with %u.  The counters are ints that cannot go negative, and
    // ../README.md SS3 prefers int wherever v7 wrote unsigned for no reason.
    fprintf(stderr, "%d+%d records in\n", nifr, nipr);
    fprintf(stderr, "%d+%d records out\n", nofr, nopr);
    if (ntrunc)
        fprintf(stderr, "%d truncated records\n", ntrunc);
}
