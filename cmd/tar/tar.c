/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tar -- archive files.
//
//      tar {crtx}[vwlm]f archive [b factor] [name ...]
//
// Task C7 (../TODO.md), and the whole of it: one program, and the first thing on this image
// that can move a TREE.  Everything before it -- dd, mkfs, fsck, mount -- moves BLOCKS.
// ./README.md is the account; five things belong at the head of the source.
//
// THE HEADER IS A BYTE LAYOUT AND THE UNION IS NOT ITS SIZE.  A tar record is 512 bytes with
// the name at 0, the mode at 100, the size at 124 and so on -- fixed by the format, not by
// this program, because the archive has to stay readable by every other tar.  The good news,
// MEASURED rather than assumed, is that a struct here packs char members BYTE-granularly:
// `struct header' lands on v7's exact offsets and only its overall size rounds up to a word
// (257 -> 258).  The bad news is what that rounding does to a union:
//
//      sizeof(union hblock) == 516, not 512
//
// -- 512 is not a multiple of six.  v7 declared `union hblock dblock, tbuf[NBLOCK]', and an
// ARRAY of that union has a 516-byte stride while every read(2) and write(2) on it moves 512
// bytes per record.  With the default blocking factor of 1 only record 0 is ever touched, so
// the program is accidentally self-consistent and the defect is invisible; with any `b'
// greater than 1 every record after the first is read and written four bytes further out
// than the last.  That is ../mount/README.md SS2's finding -- a byte count computed from a
// field width is not a struct's size here -- arriving from the union side, and hiding behind
// a default.  So dblock stays a union (one object, all I/O from its base, never sizeof'd) and
// tbuf is a FLAT char array indexed by hand.  The assertions below hold both halves.
//
// AND THE LAYOUT RULE THIS TREE STATED WAS WRONG IN THREE PLACES.  ../README.md said "a char
// member of a struct takes a word of its own here"; ../mount/README.md SS2 said sizeof{char
// f[32]; char s[32];} is "72 and not 64"; and ../../doc/Besm6_Data_Representation.md SS8's type
// table -- the authoritative one, the one CLAUDE.md says to read first -- gave `char' an
// alignment of one WORD.  All three are false for a struct MEMBER: it is 66, and chars pack six
// to a word inside a struct exactly as they do in an array, only the total rounding up.
//
// All three were wrong in the same direction and for the same reason, which is the thing worth
// keeping: **the word is real, it is just the OBJECT's and not the MEMBER's.**  A standalone
// `char c;' does take a word, because every allocation is rounded up to one; that is a fact
// about allocation and it does not reach into a struct.  The README's version had a second
// cause on top of it -- b6nm prints OCTAL, and sed's ptrspace spans 0600 words for 100 entries,
// six each and not the eleven that reading the addresses as decimal gives.  All three are
// corrected now.  Had any of them been true, this program's headers would have been
// incompatible with every other tar in the world, which is why the first thing task C7 did was
// measure it -- and why a reviewer asking the same question about `char linkflag' afterwards
// was right to ask.
//
// `f' IS MANDATORY AND THERE IS NO DEFAULT DEVICE.  v7 opened /dev/mt1 when told nothing, and
// the digits 0-7 picked a tape drive.  This kernel has no tape driver and no bdevsw/cdevsw
// row for one (../TODO.md's exclusion table), so the default named nothing and the digits
// named nothing.  Both are deleted rather than repointed at a disk: /dev/rmd0 is the pack the
// system is RUNNING ON, and a one-character typo that rewrites it is not a default worth
// having.  Say where the archive goes.
//
// AN ARCHIVE ON A RAW DEVICE OBEYS physio(), AND v7 BREAKS THREE OF ITS FOUR RULES.  Writing
// to /dev/rmd? is not writing to a file (../df/README.md): the count must be a whole number
// of BSIZE, the buffer's WORD address must be a multiple of MDALIGN, and the seek offset must
// be block-aligned or the wrong block is read in silence.  512*nblock is a multiple of 3072
// only for nblock 6, 12 and 18, and v7's default is 1.  So tbuf is stepped forward to an
// MDALIGN boundary the way df, dd, quot, icheck, mkfs and fsck all step theirs; a character
// special defaults to RAWNBLOCK rather than 1; a `b' that physio() would refuse is refused
// HERE, with a diagnostic, rather than surfacing as EFAULT through "tape write error"; and
// `r' on a character special is refused outright, because backtape()'s 512-byte lseek is the
// fourth rule and the fourth rule is the silent one.  ../TODO.md said `tar cf /dev/rmd0'
// worked today.  It did not.
//
// THE `u' KEY IS DELETED.  It wrote a name/mtime index to /tmp and then ran
//
//      system("sort ... -o ...; awk '$1 != prev {print; prev=$1}' ... >...X; mv ...X ...")
//
// and binary-searched the result.  There is no awk on this image -- it is ../TODO.md's C10 --
// so the dedup step would fail silently and leave `u' quietly wrong.  Rather than reimplement
// a shell pipeline in C for the one key v7's own manual page calls slow, the key goes, and
// with it tfile, mktemp, system(), checkupdate(), lookup(), bsrch(), cmp(), low, high and
// dorep()'s four strcat()s onto an UNINITIALISED stack buffer.  `r' still appends.
//
// AND THE WORKING DIRECTORY IS NEVER CHANGED.  v7 ran /bin/pwd down a pipe to learn where it
// was, chdir()'d to each argument's directory and chdir()'d back, and recursed by chdir()ing
// into each subdirectory.  putfile() takes a full path instead and opens it, which stores the
// same name in the same header -- and deletes a fork, a pipe, an exec, a 50-byte read into a
// 60-byte buffer, an unbounded scan for a newline, the re-open of "." after every recursive
// call, and the lseek that went with it.  It is also what makes `tar c' testable under b6sim
// at all: there the exec would fail and the parent would chdir the BUILD MACHINE to "/".
//
// NOT SETUID.  It reads and writes what the caller could read and write itself.  Extraction
// makes directories by exec'ing /bin/mkdir, which IS setuid root and is the only way to get a
// directory on a system with no mkdir(2) -- ../mkdir/README.md is the account.
//

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TBLOCK 512 // bytes in an archive record -- THE FORMAT'S, not the filesystem's
#define NBLOCK 20  // largest blocking factor
#define NAMSIZ 100 // bytes in the header's name and linkname fields

// The record stays 512 bytes and the blocking factor stays 20.  ../README.md SS4 converts every
// count that means A FILESYSTEM BLOCK to BSIZE, and rules that a block which is a program's
// own business is converted to neither -- ed's temp file, tail -b, dd's bs=.  A tar record is
// the sharpest case of that: it is not this machine's number at all, it is the archive's, and
// changing it would make every archive written here unreadable everywhere else.
_Static_assert(TBLOCK % 2 == 0, "a tar record is a power of two by the format's definition");

// physio() takes a whole number of BSIZEs and mdstrategy() a buffer aligned to a half-zone
// (../df/README.md's four conditions).  512*RAWNBLOCK is exactly BSIZE, so RAWNBLOCK is both
// the smallest blocking factor a raw device accepts and the divisor every larger one needs.
#define MDALIGN   BSIZEW
#define RAWNBLOCK (BSIZE / TBLOCK)
_Static_assert(PGSZ % MDALIGN == 0, "a page must be a whole number of MDALIGNs");
_Static_assert(BSIZE % TBLOCK == 0, "a filesystem block must be a whole number of records");
_Static_assert(RAWNBLOCK *TBLOCK == BSIZE, "RAWNBLOCK records must be one block");
_Static_assert(NBLOCK >= RAWNBLOCK, "the largest blocking factor must reach a whole block");

// THE HEADER.  Every offset below is the archive format's and is NOT this machine's to
// choose.  b6cc packs char members byte-granularly, so the struct lands on them exactly:
//
//      name 0   mode 100   uid 108   gid 116   size 124   mtime 136   chksum 148
//      linkflag 156   linkname 157        sizeof(struct header) == 258
//
// Those are MEASURED, not derived, and the assertions after the struct are what keep them
// that way: the member sizes prove the field widths, and sizeof() against btow(HDRBYTES)
// proves there is no interior padding ANYWHERE -- a word-aligned member would put the total
// at 264 or beyond.
//
// `char linkflag' IS ONE BYTE, and the doubt is worth answering here because a reviewer has
// already had it.  A standalone `char c;' does occupy a whole word -- every ALLOCATION is
// rounded up to one -- but a char MEMBER has alignment 1 and sits at a byte offset, which is
// why linkname begins at 157 and not at 162.  Writing it `char linkflag[1]' would change
// nothing at all: the compiler takes an array's alignment from its element and its size from
// the count, so both spellings are alignment 1, size 1, same offset, same addressing mode.
// ../../doc/Besm6_Data_Representation.md SS4 is the account and has the measured examples;
// its SS8 table said `1w' alignment for a char until task C7, which is where the doubt came
// from and which is the third place in this tree to have stated the rule wrongly.
struct header {
    char name[NAMSIZ];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char linkflag;
    char linkname[NAMSIZ];
};

#define HDRBYTES (NAMSIZ + 8 + 8 + 8 + 12 + 12 + 8 + 1 + NAMSIZ) // 257

_Static_assert(sizeof(((struct header *)0)->name) == NAMSIZ, "name is 100 bytes");
_Static_assert(sizeof(((struct header *)0)->mode) == 8, "mode is 8 bytes");
_Static_assert(sizeof(((struct header *)0)->size) == 12, "size is 12 bytes");
_Static_assert(sizeof(((struct header *)0)->chksum) == 8, "chksum is 8 bytes");
// EXACT, not an inequality: any interior padding at all moves a field, and a field that has
// moved makes the archives readable by nothing.  btow() is <sys/param.h>'s bytes-to-words, so
// btow(257) * 6 is the 258 that a struct with no interior padding and one byte of TRAILING
// pad must measure.
_Static_assert(sizeof(struct header) == btow(HDRBYTES) * NBPW,
               "struct header must have no interior padding: char members pack six to a word");
_Static_assert(HDRBYTES <= TBLOCK, "the header must fit one record");

// ONE union, never an array of one: sizeof(union hblock) is 516 and every transfer on it is
// TBLOCK.  See the head comment.  tbuf below is flat for exactly that reason.
union hblock {
    char dummy[TBLOCK];
    struct header dbuf;
};

static union hblock dblock;

// THE NAME AS A C STRING, and it has to be a copy.  A name of exactly NAMSIZ characters fills
// the header field and carries NO terminator -- that is the format, not a defect -- so the
// field itself may never be handed to creat(), link(), unlink(), utime(), chown() or strcmp:
// each would run on into the mode field and past it.  v7 passed dblock.dbuf.name to all six.
// The bound is ../README.md SS5's rule, with NAMSIZ where a directory entry has DIRSIZ, and
// the terminated copies are what let the read side go back to a plain %s.
static char hname[NAMSIZ + 1];
static char hlink[NAMSIZ + 1];

// The blocking buffer.  It is reached through a char * stepped forward to an MDALIGN word
// boundary, because a raw device demands that and a file does not care; the pad is at most
// MDALIGN-1 words and is charged once.  ptrword() is <sys/param.h>'s -- a pointer's word
// address -- and aligning the VIRTUAL address is enough because a page is a whole number of
// MDALIGNs and mapping preserves the offset within a page (../df/README.md again).
static int tbufspace[btow(NBLOCK * TBLOCK) + MDALIGN];
static char *tbuf;

// putempty() writes a record of zeros and passtape() throws one away; neither needs its own
// storage and neither may take 86 words of the 4,096-word stack to get it (../README.md SS6's
// third ceiling, the one nothing checks).  zblock is never written to.
static char zblock[TBLOCK];
static char iobuf[TBLOCK];

// ONE data buffer for a walk that RE-ENTERS ITSELF, and that is a decision rather than an
// oversight (../README.md's C4e rule: share or one-per-level is settled by whether the walk
// recurses through the buffer).  putfile() recurses only on the DIRECTORY branch, which
// returns before it reaches the read loop, so no two frames are ever inside iobuf at once.

struct linkbuf {
    ino_t inum;
    dev_t devnum;
    int count;
    char pathname[NAMSIZ];
    struct linkbuf *nextp;
};

static struct linkbuf *ihead;
static struct stat stbuf;

static int rflag, xflag, vflag, tflag, cflag, mflag, wflag;
static int mt = -1; // the archive
static int term;    // set by a signal handler: stop at the next record
static int chksum;  // the checksum read out of the header just parsed
static int linkerrok;
static int freemem = 1;

static int nblock = 1; // records per write(2)
static int readsize;   // records per read(2) -- see setblocking()
static int recno;      // the next record to take out of tbuf, or to put into it
static int nrec;       // records the last read(2) actually returned
static int bgiven;     // `b' was named, so nblock is the user's and not ours
static int israw;      // the archive is a character special: physio()'s rules apply

static char *usefile; // `f' is mandatory: there is no default

// HOW DEEP THE WALK MAY GO, and both halves of it are measured rather than estimated.
//
// putfile() recurses, so its frame is paid once per level, and ../README.md SS6's third
// ceiling -- 4,096 words of stack at 070000 -- is the one NOTHING checks: past it the stores
// do not fault, they just land somewhere else.  This port's frame was ESTIMATED at thirty
// words (a 101-byte path, a four-word struct direct and a dozen scalars) and b6disasm says
//
//      1444:   15 utm 0357
//
// -- 0357 is 239 words, eight times the guess.  That is C5f's rule about find(1) arriving
// again and costing more: read the prologue.  Against 4,096, with _doprnt's 281-word frame
// underneath the diagnostic that reports the refusal (C5e's rule -- ask what a program still
// has to do after it has decided to stop), 14 levels is the arithmetic:
//
//      (14 + 1) * 239 + 281 + 100 = 3,966
//
// The walk also holds one open descriptor per level -- v7 closed and re-opened each directory
// because it chdir()'d away and this port does not -- and NOFILE is 20, of which stdin,
// stdout, stderr and the archive are gone already.  So the descriptors bind first, and the
// limit is theirs; a tree deeper than this gets a diagnostic naming the real reason rather
// than "cannot open file" on an arbitrary directory part way down.
#define STACKDEPTH 14
#define MAXDEPTH   (NOFILE - 8)
_Static_assert(MAXDEPTH <= STACKDEPTH, "the descriptor limit must be the tighter of the two");

static void usage(void);
static void done(int n);
static void dorep(char **argv);
static int endtape(void);
static void getdir(void);
static void passtape(void);
static void putfile(char *path, int depth);
static void doxtract(char **argv);
static void dotable(void);
static void putempty(void);
static void longt(struct stat *st);
static void pmode(struct stat *st);
static void selbit(const int *pairp, struct stat *st);
static void checkdir(char *name);
static void onintr(int sig);
static void onquit(int sig);
static void onhup(int sig);
static void tomodes(struct stat *sp);
static int checksum(void);
static int checkw(int c, char *name);
static int response(void);
static int prefix(char *s1, char *s2);
static void readtape(char *buffer);
static void writetape(char *buffer);
static void backtape(void);
static void flushtape(void);
static int octfield(char *field, int n, char *what, unsigned int v);
static void setblocking(void);

int main(int argc, char **argv)
{
    char *cp;
    int *p;

    if (argc < 2)
        usage();

    argv[argc] = NULL;
    argv++;
    for (cp = *argv++; *cp; cp++)
        switch (*cp) {
        case 'f':
            if (*argv == NULL) {
                fprintf(stderr, "tar: f needs the name of an archive\n");
                done(1);
            }
            usefile = *argv++;
            break;
        case 'c':
            cflag++;
            rflag++;
            break;
        case 'r':
            rflag++;
            break;
        case 'v':
            vflag++;
            break;
        case 'w':
            wflag++;
            break;
        case 'x':
            xflag++;
            break;
        case 't':
            tflag++;
            break;
        case 'm':
            mflag++;
            break;
        case 'l':
            linkerrok++;
            break;
        case '-':
            break;
        case 'b':
            if (*argv == NULL) {
                fprintf(stderr, "tar: b needs a blocking factor\n");
                done(1);
            }
            nblock = atoi(*argv++);
            bgiven++;
            if (nblock > NBLOCK || nblock <= 0) {
                fprintf(stderr, "tar: invalid blocking factor (max %d)\n", NBLOCK);
                done(1);
            }
            break;
        default:
            fprintf(stderr, "tar: %c: unknown option\n", *cp);
            usage();
        }

    // `f' is mandatory.  v7 fell back on /dev/mt1 and this machine has no tape; see the head
    // comment for why the fallback is not a disk instead.
    if (usefile == NULL) {
        fprintf(stderr, "tar: no archive named -- f is not optional here\n");
        usage();
    }

    // The blocking buffer, aligned before anything can transfer through it.
    p = tbufspace;
    while (ptrword(p) % MDALIGN != 0)
        p++;
    tbuf = (char *)p;

    if (rflag) {
        if (signal(SIGINT, SIG_IGN) != SIG_IGN)
            signal(SIGINT, onintr);
        if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
            signal(SIGHUP, onhup);
        if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
            signal(SIGQUIT, onquit);
        if (strcmp(usefile, "-") == 0) {
            if (cflag == 0) {
                fprintf(stderr, "tar: can only create standard output archives\n");
                done(1);
            }
            mt = dup(1);
        } else if ((mt = open(usefile, 2)) < 0) {
            if (cflag == 0 || (mt = creat(usefile, 0666)) < 0) {
                fprintf(stderr, "tar: cannot open %s\n", usefile);
                done(1);
            }
        }
        setblocking();
        dorep(argv);
    } else if (xflag) {
        if (strcmp(usefile, "-") == 0)
            mt = dup(0);
        else if ((mt = open(usefile, 0)) < 0) {
            fprintf(stderr, "tar: cannot open %s\n", usefile);
            done(1);
        }
        setblocking();
        doxtract(argv);
    } else if (tflag) {
        if (strcmp(usefile, "-") == 0)
            mt = dup(0);
        else if ((mt = open(usefile, 0)) < 0) {
            fprintf(stderr, "tar: cannot open %s\n", usefile);
            done(1);
        }
        setblocking();
        dotable();
    } else
        usage();
    done(0);
    return 0;
}

// What the blocking factor is, and whether physio() will accept it.  This is the one place
// that knows a raw device from a file, and it runs after the open because only an open
// descriptor can be asked.  ../df/README.md is the account of the four conditions.
static void setblocking(void)
{
    if (fstat(mt, &stbuf) < 0) {
        fprintf(stderr, "tar: cannot stat %s\n", usefile);
        done(1);
    }
    israw = (stbuf.st_mode & S_IFMT) == S_IFCHR;

    if (israw) {
        if (!bgiven)
            nblock = RAWNBLOCK;
        else if (nblock % RAWNBLOCK != 0) {
            // physio() would take this as EFAULT and tar would report a tape error, which
            // names the symptom and not the cause.  ../README.md's C5b rule: a loud limit
            // beats a quiet one, and a bound is worth stating from the side that can explain
            // it.
            fprintf(stderr,
                    "tar: %s is a raw device: the blocking factor must be a multiple of %d\n",
                    usefile, RAWNBLOCK);
            done(1);
        }
        // backtape() steps back one 512-byte record, and an lseek that is not a whole number
        // of blocks is physio()'s FOURTH condition -- the one nobody enforces and that reads
        // the wrong block without saying so.  Only `r' reaches it, so only `r' is refused.
        if (rflag && !cflag) {
            fprintf(stderr, "tar: cannot append to %s: a raw device cannot be backspaced\n",
                    usefile);
            done(1);
        }
    } else if (!bgiven)
        nblock = 1;

    if (rflag && !cflag && nblock != 1) {
        fprintf(stderr, "tar: cannot append to a blocked archive\n");
        done(1);
    }

    // HOW MUCH TO ASK FOR ON A READ, which is a different question from how much to write and
    // v7 ran the two together.  Its readtape() asked for NBLOCK records, divided what came
    // back by TBLOCK and announced the answer as "blocksize = N" -- which is a TAPE property
    // being read off a FILE: a tape read returns exactly one physical block, a file read
    // returns whatever is left.  On this machine every archive is a file or a disk, so what
    // v7 printed was the archive's LENGTH in records, never its blocking, and `tar tf' of any
    // archive shorter than NBLOCK said so on stderr.  Reading needs no blocking factor at
    // all: records come out of the buffer in order however they went in.  So the message is
    // deleted, the guess with it, and a file is read NBLOCK records at a time -- twenty times
    // fewer read(2)s than v7's default of one, which is C12's point about counting syscalls
    // from the source.  Two exceptions: a raw device must ask for a physio()-legal count, and
    // `r' must ask for exactly one record, because it appends where the archive ends and a
    // buffered read would carry the file offset past it.
    if (rflag && !cflag)
        readsize = 1;
    else if (israw)
        readsize = nblock;
    else
        readsize = NBLOCK;
}

static void usage(void)
{
    fprintf(stderr, "tar: usage: tar {crtx}[vwlm]f archive [b factor] [name ...]\n");
    done(1);
}

static void dorep(char **argv)
{
    if (!cflag) {
        getdir();
        do {
            passtape();
            if (term)
                done(0);
            getdir();
        } while (!endtape());
        // Reading is over and writing begins at the offset backtape() left.  tbuf holds the
        // last record READ; saying so here is what stops writetape() flushing it back out.
        recno = 0;
        nrec  = 0;
    }

    while (*argv && !term)
        putfile(*argv++, 0);

    putempty();
    putempty();
    flushtape();
    if (linkerrok == 1)
        for (; ihead != NULL; ihead = ihead->nextp)
            if (ihead->count != 0)
                fprintf(stderr, "tar: missing links to %.*s\n", NAMSIZ, ihead->pathname);
}

static int endtape(void)
{
    if (dblock.dbuf.name[0] == '\0') {
        backtape();
        return 1;
    }
    return 0;
}

static void getdir(void)
{
    struct stat *sp;
    int i;

    readtape(dblock.dummy);
    memcpy(hname, dblock.dbuf.name, NAMSIZ);
    hname[NAMSIZ] = '\0';
    memcpy(hlink, dblock.dbuf.linkname, NAMSIZ);
    hlink[NAMSIZ] = '\0';
    if (dblock.dbuf.name[0] == '\0')
        return;
    sp = &stbuf;
    sscanf(dblock.dbuf.mode, "%o", &i);
    sp->st_mode = i;
    sscanf(dblock.dbuf.uid, "%o", &i);
    sp->st_uid = i;
    sscanf(dblock.dbuf.gid, "%o", &i);
    sp->st_gid = i;
    sscanf(dblock.dbuf.size, "%o", &i);
    sp->st_size = i;
    sscanf(dblock.dbuf.mtime, "%o", &i);
    sp->st_mtime = i;
    sscanf(dblock.dbuf.chksum, "%o", &chksum);
    if (chksum != checksum()) {
        fprintf(stderr, "tar: directory checksum error\n");
        done(2);
    }
}

static void passtape(void)
{
    int blocks;

    if (dblock.dbuf.linkflag == '1')
        return;
    blocks = (stbuf.st_size + (TBLOCK - 1)) / TBLOCK;

    while (blocks-- > 0)
        readtape(iobuf);
}

// Archive one path, which is the name that goes in the header AND the name that is opened.
// v7 took two -- a long one to store and a short one to open, the difference being made up by
// a chdir() -- and see the head comment for why that is gone.
static void putfile(char *path, int depth)
{
    int infile;
    int blocks;
    char *cp, *cp2;
    struct direct dbuf;
    char child[NAMSIZ + 1];
    int i, n;

    infile = open(path, 0);
    if (infile < 0) {
        fprintf(stderr, "tar: %s: cannot open file\n", path);
        return;
    }

    fstat(infile, &stbuf);

    if (checkw('r', path) == 0) {
        close(infile);
        return;
    }

    if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
        // The path of each entry is this one plus '/' plus the name, and it is built ONCE per
        // level in the frame rather than by walking a shared buffer, because the walk
        // re-enters itself here.  v7 copied `path' into a 512-byte automatic with no bound at
        // all -- on the branch its NAMSIZ test never reaches -- and paths grow by up to
        // DIRSIZ+1 per level, so a deep tree overran the frame.  The bound is NAMSIZ because
        // a path the header cannot hold is a path nothing under it can be archived under.
        n = strlen(path);
        if (n + 1 + DIRSIZ > NAMSIZ) {
            fprintf(stderr, "tar: %s: directory name too long\n", path);
            close(infile);
            return;
        }
        if (depth >= MAXDEPTH) {
            fprintf(stderr, "tar: %s: more than %d directories deep\n", path, MAXDEPTH);
            close(infile);
            return;
        }
        memcpy(child, path, n);
        child[n] = '/';
        cp       = &child[n + 1];

        while (read(infile, (char *)&dbuf, sizeof(dbuf)) == sizeof(dbuf) && !term) {
            if (dbuf.d_ino == 0)
                continue;
            // d_name is DIRSIZ bytes and is NOT terminated when a name fills it, so it may
            // not be handed to strcmp -- ../README.md SS5, and ls(1)'s finding.
            if (strncmp(dbuf.d_name, ".", DIRSIZ) == 0 || strncmp(dbuf.d_name, "..", DIRSIZ) == 0)
                continue;
            cp2 = cp;
            for (i = 0; i < DIRSIZ && dbuf.d_name[i] != '\0'; i++)
                *cp2++ = dbuf.d_name[i];
            *cp2 = '\0';
            putfile(child, depth + 1);
        }
        close(infile);
        return;
    }
    if ((stbuf.st_mode & S_IFMT) != S_IFREG) {
        fprintf(stderr, "tar: %s is not a file. Not dumped\n", path);
        close(infile);
        return;
    }

    tomodes(&stbuf);

    // Exactly NAMSIZ is legal and is the format's documented limit: the field simply carries
    // no terminator then, which is why every print of it goes through %.*s (SS5's rule with
    // NAMSIZ in place of DIRSIZ).  v7 copied first and tested afterwards, through a loop that
    // could put NAMSIZ+1 bytes into a NAMSIZ field.
    n = strlen(path);
    if (n > NAMSIZ) {
        fprintf(stderr, "tar: %s: file name too long\n", path);
        close(infile);
        return;
    }
    memcpy(dblock.dbuf.name, path, n);

    if (stbuf.st_nlink > 1) {
        struct linkbuf *lp;
        int found = 0;

        for (lp = ihead; lp != NULL; lp = lp->nextp) {
            if (lp->inum == stbuf.st_ino && lp->devnum == stbuf.st_dev) {
                found++;
                break;
            }
        }
        if (found) {
            memcpy(dblock.dbuf.linkname, lp->pathname, strlen(lp->pathname));
            dblock.dbuf.linkflag = '1';
            octfield(dblock.dbuf.chksum, 6, "checksum", (unsigned)checksum());
            writetape(dblock.dummy);
            if (vflag)
                fprintf(stderr, "a %s link to %.*s\n", path, NAMSIZ, lp->pathname);
            lp->count--;
            close(infile);
            return;
        }
        lp = (struct linkbuf *)malloc(sizeof(*lp));
        if (lp == NULL) {
            if (freemem) {
                fprintf(stderr, "tar: out of memory. Link information lost\n");
                freemem = 0;
            }
        } else {
            lp->nextp  = ihead;
            ihead      = lp;
            lp->inum   = stbuf.st_ino;
            lp->devnum = stbuf.st_dev;
            lp->count  = stbuf.st_nlink - 1;
            memset(lp->pathname, 0, NAMSIZ);
            memcpy(lp->pathname, path, n);
        }
    }

    blocks = (stbuf.st_size + (TBLOCK - 1)) / TBLOCK;
    if (vflag)
        fprintf(stderr, "a %s %d blocks\n", path, blocks);
    octfield(dblock.dbuf.chksum, 6, "checksum", (unsigned)checksum());
    writetape(dblock.dummy);

    // The tail of the last record is zero-filled.  v7 wrote whatever the buffer happened to
    // hold, which is bytes of the PREVIOUS file in the same archive -- an information leak
    // and, for a byte-for-byte oracle, a source of noise.  Fixed rather than carried, and it
    // is a visible bug rather than a defensive change.
    while ((i = read(infile, iobuf, TBLOCK)) > 0 && blocks > 0) {
        if (i < TBLOCK)
            memset(iobuf + i, 0, TBLOCK - i);
        writetape(iobuf);
        blocks--;
    }
    close(infile);
    if (blocks != 0 || i != 0)
        fprintf(stderr, "tar: %s: file changed size\n", path);
    while (blocks-- > 0)
        putempty();
}

static void doxtract(char **argv)
{
    int blocks, bytes;
    char **cp;
    int ofile;

    for (;;) {
        getdir();
        if (endtape())
            break;

        if (*argv == 0)
            goto gotit;

        for (cp = argv; *cp; cp++)
            if (prefix(*cp, hname))
                goto gotit;
        passtape();
        continue;

    gotit:
        if (checkw('x', hname) == 0) {
            passtape();
            continue;
        }

        checkdir(hname);

        if (dblock.dbuf.linkflag == '1') {
            unlink(hname);
            if (link(hlink, hname) < 0) {
                fprintf(stderr, "tar: %s: cannot link\n", hname);
                continue;
            }
            if (vflag)
                fprintf(stderr, "%s linked to %s\n", hname, hlink);
            continue;
        }
        if ((ofile = creat(hname, stbuf.st_mode & 07777)) < 0) {
            fprintf(stderr, "tar: %s - cannot create\n", hname);
            passtape();
            continue;
        }

        chown(hname, stbuf.st_uid, stbuf.st_gid);

        blocks = ((bytes = stbuf.st_size) + TBLOCK - 1) / TBLOCK;
        if (vflag)
            fprintf(stderr, "x %s, %d bytes, %d tape blocks\n", hname, bytes, blocks);
        while (blocks-- > 0) {
            readtape(iobuf);
            if (write(ofile, iobuf, bytes > TBLOCK ? TBLOCK : bytes) < 0) {
                fprintf(stderr, "tar: %s: HELP - extract write error\n", hname);
                done(2);
            }
            bytes -= TBLOCK;
        }
        close(ofile);
        if (mflag == 0) {
            time_t timep[2];

            timep[0] = time(NULL);
            timep[1] = stbuf.st_mtime;
            utime(hname, timep);
        }
    }
}

static void dotable(void)
{
    for (;;) {
        getdir();
        if (endtape())
            break;
        if (vflag)
            longt(&stbuf);
        // The name field is NOT terminated when a name fills it, so %s would run into the
        // mode field -- ../README.md SS5's rule with NAMSIZ where a directory has DIRSIZ.
        printf("%s", hname);
        if (dblock.dbuf.linkflag == '1')
            printf(" linked to %s", hlink);
        printf("\n");
        passtape();
    }
}

static void putempty(void)
{
    writetape(zblock);
}

static void longt(struct stat *st)
{
    char *cp;

    pmode(st);
    printf("%3d/%1d", st->st_uid, st->st_gid);
    // v7 wrote %7D, a PDP-11 long.  doprnt() here does not know that conversion, echoes it
    // VERBATIM and consumes no argument (../README.md SS3), so the whole size column of
    // `tar tv' printed the two characters `%D'.  Second hit in twenty-five sources, after
    // grep -c, and the same shape: a number the program did not compute itself.
    printf("%7d", st->st_size);
    cp = ctime(&st->st_mtime);
    printf(" %-12.12s %-4.4s ", cp + 4, cp + 20);
}

#define SUID 04000
#define SGID 02000
#define ROWN 0400
#define WOWN 0200
#define XOWN 0100
#define RGRP 040
#define WGRP 020
#define XGRP 010
#define ROTH 04
#define WOTH 02
#define XOTH 01
#define STXT 01000

static const int m1[] = { 1, ROWN, 'r', '-' };
static const int m2[] = { 1, WOWN, 'w', '-' };
static const int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
static const int m4[] = { 1, RGRP, 'r', '-' };
static const int m5[] = { 1, WGRP, 'w', '-' };
static const int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
static const int m7[] = { 1, ROTH, 'r', '-' };
static const int m8[] = { 1, WOTH, 'w', '-' };
static const int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

static const int *const m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

static void pmode(struct stat *st)
{
    int i;

    for (i = 0; i < 9; i++)
        selbit(m[i], st);
}

// v7 called this select().  Renamed on sight, as ../README.md SS1 requires and as ls(1) did
// with the identical routine: nothing collides today, this tree having no BSD select, but it
// is a name a later header will want.
static void selbit(const int *pairp, struct stat *st)
{
    const int *ap;
    int n;

    ap = pairp;
    n  = *ap++;
    while (--n >= 0 && (st->st_mode & *ap++) == 0)
        ap++;
    printf("%c", *ap);
}

// Make the directories a name needs.  This system has no mkdir(2) -- /bin/mkdir is setuid
// root and calls mknod(2) -- so exec'ing it is not laziness, it is the only route.  v7 tried
// /usr/bin/mkdir as well; there is no /usr/bin on this image (rm(1) records the same for
// rmdir), and a v7 execl needs a (char *)0 sentinel rather than a bare 0.
static void checkdir(char *name)
{
    char *cp;
    int i;

    for (cp = name; *cp; cp++) {
        if (*cp == '/') {
            *cp = '\0';
            if (access(name, 01) < 0) {
                if (fork() == 0) {
                    execl("/bin/mkdir", "mkdir", name, (char *)0);
                    fprintf(stderr, "tar: cannot find mkdir!\n");
                    // v7 exited 0 here, so the parent could not tell that nothing was made.
                    _exit(1);
                }
                while (wait(&i) >= 0)
                    ;
                chown(name, stbuf.st_uid, stbuf.st_gid);
            }
            *cp = '/';
        }
    }
}

static void onintr(int sig)
{
    (void)sig;
    signal(SIGINT, SIG_IGN);
    term++;
}

static void onquit(int sig)
{
    (void)sig;
    signal(SIGQUIT, SIG_IGN);
    term++;
}

static void onhup(int sig)
{
    (void)sig;
    signal(SIGHUP, SIG_IGN);
    term++;
}

// Write v as `n' octal digits, right-justified and blank-filled, into EXACTLY n bytes of
// field -- no terminator.  v7 spelt this sprintf("%6o ") and sprintf("%11lo "), which write
// one byte MORE than the field holds: each spilt a NUL into the next field, which the next
// sprintf overwrote, and the last spilt into chksum, which checksum() then blanked.  It
// worked, but it made the order of five calls load-bearing and it truncated in silence when
// a value did not fit.  The bytes on the tape are identical; what is gone is the ordering
// and the quiet lie (../README.md's C5b rule).
static int octfield(char *field, int n, char *what, unsigned int v)
{
    int i;

    for (i = n - 1; i >= 0; i--) {
        field[i] = (char)('0' + (v & 7));
        v >>= 3;
        if (v == 0) {
            while (--i >= 0)
                field[i] = ' ';
            return 1;
        }
    }
    fprintf(stderr, "tar: %s does not fit the archive format's %d octal digits\n", what, n);
    return 0;
}

static void tomodes(struct stat *sp)
{
    memset(dblock.dummy, 0, TBLOCK);
    // The trailing blank of each field is v7's and is part of the format; the byte after it
    // stays NUL from the memset above, which is where v7's sprintf put its terminator.
    octfield(dblock.dbuf.mode, 6, "mode", (unsigned)(sp->st_mode & 07777));
    dblock.dbuf.mode[6] = ' ';
    octfield(dblock.dbuf.uid, 6, "uid", (unsigned)sp->st_uid);
    dblock.dbuf.uid[6] = ' ';
    octfield(dblock.dbuf.gid, 6, "gid", (unsigned)sp->st_gid);
    dblock.dbuf.gid[6] = ' ';
    octfield(dblock.dbuf.size, 11, "file size", (unsigned)sp->st_size);
    dblock.dbuf.size[11] = ' ';
    octfield(dblock.dbuf.mtime, 11, "modification time", (unsigned)sp->st_mtime);
    dblock.dbuf.mtime[11] = ' ';
}

// The sum of all 512 bytes with the checksum field blanked.  `char' is UNSIGNED here where
// the PDP-11's was signed, so a header carrying a UTF-8 name sums differently from what a
// real v7 tar would have computed -- and this is the right side of that difference: GNU and
// BSD tar both sum unsigned, and a signed sum was the historical bug.  It is ../README.md
// SS11's fourth table shape, the one where this machine's unsigned char REPAIRS a v7 defect,
// and it must not be "fixed" back.
static int checksum(void)
{
    int i, sum;

    for (i = 0; i < 8; i++)
        dblock.dbuf.chksum[i] = ' ';
    sum = 0;
    for (i = 0; i < TBLOCK; i++)
        sum += dblock.dummy[i];
    return sum;
}

static int checkw(int c, char *name)
{
    if (wflag) {
        printf("%c ", c);
        if (vflag)
            longt(&stbuf);
        printf("%s: ", name);
        if (response() == 'y')
            return 1;
        return 0;
    }
    return 1;
}

// v7 read the answer into a `char', which on this machine is UNSIGNED: EOF became 255, the
// test against '\n' never matched, and the discard loop spun forever on a closed input.
// ../README.md SS11's input side, and C5a's rule about grepping a candidate for a hang before
// designing its tests.
static int response(void)
{
    int c, answer;

    answer = getchar();
    if (answer == EOF || answer == '\n')
        return 'n';
    while ((c = getchar()) != '\n' && c != EOF)
        ;
    return answer;
}

static void done(int n)
{
    exit(n);
}

static int prefix(char *s1, char *s2)
{
    while (*s1)
        if (*s1++ != *s2++)
            return 0;
    if (*s2)
        return *s2 == '/';
    return 1;
}

static void readtape(char *buffer)
{
    int i;

    if (recno >= nrec) {
        if ((i = read(mt, tbuf, TBLOCK * readsize)) < 0) {
            fprintf(stderr, "tar: tape read error\n");
            done(3);
        }
        if ((i % TBLOCK) != 0) {
            fprintf(stderr, "tar: tape blocksize error\n");
            done(3);
        }
        nrec = i / TBLOCK;
        // Past the end of the archive, hand back a record of zeros: that is what endtape()
        // is looking for, and it is what a tar file's own two trailing records say.  v7 kept
        // no count of what the last read returned and would have handed back whatever the
        // buffer still held from the read before it.
        if (nrec == 0) {
            memset(tbuf, 0, TBLOCK);
            nrec = 1;
        }
        recno = 0;
    }
    memcpy(buffer, tbuf + recno++ * TBLOCK, TBLOCK);
}

static void writetape(char *buffer)
{
    memcpy(tbuf + recno++ * TBLOCK, buffer, TBLOCK);
    if (recno >= nblock) {
        if (write(mt, tbuf, TBLOCK * nblock) < 0) {
            fprintf(stderr, "tar: tape write error\n");
            done(2);
        }
        recno = 0;
    }
}

// Step back over the record endtape() has just read, so that `r' appends where the archive
// ends rather than one record past it.  It is exact only because setblocking() gives `r' a
// readsize of one record, which is why that is not merely an optimisation; for x and t the
// seek lands somewhere inside the last buffered read and does not matter, both of them being
// about to stop.  On a pipe the lseek fails and there is nothing to be done about that.  On a
// raw device it would be physio()'s FOURTH condition -- an offset that is not a whole number
// of blocks, truncated in silence -- which is why setblocking() refuses `r' there outright.
static void backtape(void)
{
    lseek(mt, (off_t)-TBLOCK, 1);
    recno = 0;
    nrec  = 0;
}

// v7 wrote a full physical block whether or not anything was in it, appending one block of
// whatever the buffer last held.  Fixed rather than carried: this is a visible bug, not a
// defensive change -- `tar cf x' on an empty argument list wrote a block of garbage.
static void flushtape(void)
{
    if (recno > 0)
        write(mt, tbuf, TBLOCK * nblock);
}
