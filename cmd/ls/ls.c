/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

//
// ls -- list a file or a directory.
//
// 4.2BSD's, by way of RetroBSD's src/cmd/ls, replacing the v7 program this directory
// carried before: multi-column output on a terminal, -F, -R, -A, -1, -q, and the password
// and group files reached through getpwuid(3)/getgrgid(3) rather than parsed by hand.  It
// is the first caller of this libc's directory library -- opendir(3), which arrived with
// it -- and the reason that library exists rather than a tenth open-coded reader.
//
// WHAT THE KERNEL TAKES AWAY.  There are no symbolic links here, so lstat(), readlink(),
// S_IFLNK, the `-> target' printing, the `l' type and the `@' suffix are all gone and
// `statf' collapses to plain stat(); there are no sockets, so S_IFSOCK, `s' and `=' go
// with them; and there is no st_flags, so 4.4BSD's chflags column and the whole of
// stat_flags.c are not ported.  -L AND -o ARE STILL PARSED, and set a flag nothing reads,
// so that a command line written for a BSD ls does not fail here -- each says so where it
// is handled.  There is no getopt(3) either; the parser below is by hand and takes both
// `ls -lt' and `ls -l -t'.
//
// ISARG IS S_IFREG.  Upstream packs its own flag 0x8000 into fmode beside
// `st_mode & ~S_IFMT', and 0x8000 IS 0100000 IS S_IFREG.  It got away with it on a machine
// whose fmode was a 16-bit short, where the complement cleared the whole 0170000 type field
// and nothing above it existed; here st_mode is a 41-bit int and ~S_IFMT keeps every bit
// from 16 upward, so the two would be the same bit and every regular file would sort as a
// command-line argument.  fmode is masked to 07777 instead -- everything the m1..m9 tables
// examine is <= 04000 -- and the collision cannot come back however wide st_mode becomes.
// IT LINKS EITHER WAY; only the listing is wrong.
//
// A BLOCK IS BSIZE == 3072 BYTES, and there is no st_blocks to ask.  fblks is nblock(size)
// -- a divide, 3072 not being a power of two -- and `ls -s' and the `total' line print
// KBPB (three) of them per filesystem block, so the numbers are in 1024-BYTE BLOCKS.  The
// multiply is at the two printfs and nowhere else, which is the rule ../README.md SS4
// states for all four programs that report a block count; nblock() and the accumulator
// keep meaning filesystem blocks.  Two consequences to expect: every number is a multiple
// of three, and it is half a PDP-11's rather than a sixth.
//
// THE PRINTF CONVERSIONS.  There is no `long' on this machine, so every %ld is %d.  (The
// length modifiers are parsed and ignored, so %ld would have worked; it is written %d
// because there is no long to mean.)  And an unknown conversion -- v7's %D, %O -- is
// ECHOED VERBATIM AND CONSUMES NO ARGUMENT in this libc's doprnt, which desynchronises
// every later conversion in the same format.  There are none left; keep it that way.
//
// -q MUST NOT EAT CYRILLIC.  Upstream replaces any byte `< ' ' || >= 0177' with `?', and
// -q is ON BY DEFAULT ON A TERMINAL.  A char is UNSIGNED here and the console, the clists,
// the filesystem and /bin/sh are all byte-transparent (../README.md SS11), so that test
// would turn every byte of a Cyrillic name into a question mark.  It is `< ' ' || == 0177'
// below: control characters, and nothing else.  A COLUMN IS THEN A BYTE WIDE and a
// multi-byte name is charged for its bytes; ls.1.umm says so.
//
// NO TIOCGWINSZ.  This kernel's terminal ioctls are v7's, so there is no way to ask how
// wide the screen is: twidth is 80, or $COLUMNS when that is set to something sensible.
// The rest of the probe is real -- isatty(3), TIOCGETP and XTABS decide usetabs exactly as
// upstream intends.
//
// BOUNDED WHERE UPSTREAM WAS NOT.  BUFSIZ is 3072 here, so upstream's `static char
// dfile[BUFSIZ]' and `static char fmtres[BUFSIZ]' would be 1,024 words of bss between them
// and gstat's `char buf[BUFSIZ]' 512 words of stack.  The third goes with readlink; the
// other two are sized for what they actually hold.  fmtentry() appended a name into fmtres
// with no bound at all and fmtinum() sprintf'd "%6u " into eight characters, which a
// seven-digit i-number overruns; both are bounded now.
//
// THE ENTRY ARRAY GROWS, where v7's was a fixed flist[1024].  The doubling is upstream's
// and the ceiling is this port's: an afile plus a strdup'd name plus two malloc headers is
// about sixteen words, and a 28-page address space gives out well before an unbounded
// array would.  `ls: too many files' is the v7 message, kept because the graceful failure
// was worth keeping.
//
// ERRORS GO TO STDOUT, both of them.  `%s unreadable' is upstream's own `not stderr!' and
// `%s not found' is on stdout in v7 and was in the program this replaces.  stdout is block
// buffered in the guest and stderr is not, so moving either would reorder the logs that
// kernel/test/*.expected diff -- a real constraint, not a preference.
//
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sgtty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ttyio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

// `ls -s' and `total' are in these, not in filesystem blocks.  ../README.md SS4 asks for
// the assertion as well as the multiply, so that retuning BSIZE cannot leave this program
// quietly reporting in a unit that no longer divides.
_Static_assert(BSIZE % KBYTE == 0, "a reported block must divide a filesystem block");

// ls's own bit in fmode, marking an entry that came from the command line rather than out
// of a directory.  It is NOT upstream's 0x8000 -- see the head comment; fmode is masked to
// 07777, so this is the first bit above everything the mode tables look at.
#define ISARG 010000

// The longest "directory/name" cat() will build.  Upstream wrote BUFSIZ, which is 3072
// here; a path is bounded by what the kernel will accept and this is generous for it.
#define MAXPATH 256

// One formatted line: the i-number and size columns, the long listing, a full-width name,
// and the -F suffix.  Sized so that nothing on any path has to test it -- ../README.md SS6,
// a bound test that is not on every path reads exactly like one.
#define FMTSIZE 256

// How many entries one listing may hold before ls gives up.  See the head comment: the
// heap gives out at about this order of magnitude anyway, and this way it says so.
#define MAXFILES 2048

// The uid and gid name caches.  Upstream has 64 entries of each; /etc/passwd here holds a
// handful of users, and 2 x 64 x 3 words of bss for a directory that names two of them is
// not a trade this address space makes.
#define NCACHE 16
#define CAMASK (NCACHE - 1)

// The login-name field of a utmp record, which is what a name column is sized for.  It is
// written out rather than taken as a sizeof, because b6cc will not take a sizeof in an
// array bound; the assertion is what keeps the two in step.
#define NMAX 8

_Static_assert(sizeof(((struct utmp *)0)->ut_name) == NMAX, "NMAX must be the ut_name field");

//
// One line of the listing.  Every short and long of upstream's is one 48-bit word here, so
// each field is spelled as the thing it holds.  Ten words, and the array of them is sorted
// BY VALUE: the element is a multiple of NBPW and calloc returns byte #0 of a word, so
// lib/libc/gen/qsort.c takes its word-at-a-time exchange path over them.
//
struct afile {
    char ftype;    // 'd', 'b', 'c' or '-' -- the first column of `ls -l'
    ino_t fnum;    // i-number
    int fmode;     // the permission bits (07777), with ISARG possibly on top
    int fnl;       // link count
    uid_t fuid;    //
    gid_t fgid;    //
    off_t fsize;   // the size -- OR the device number, when ftype is 'b' or 'c'
    int fblks;     // FILESYSTEM blocks; the printfs multiply by KBPB
    time_t fmtime; // whichever of mtime/atime/ctime the flags asked for
    char *fname;   // the name
};

struct subdirs {
    char *sd_name;
    struct subdirs *sd_next;
};

static struct subdirs *subdirs;

static int aflg, dflg, gflg, lflg, sflg, tflg, uflg, iflg, fflg, cflg;
static int rflg = 1; // +1 normally, -1 for `ls -r'; fcmp multiplies by it
static int oflg, qflg, Aflg, Cflg, Fflg, Lflg, Rflg, usetabs;

static time_t now, sixmonthsago;

static char *dotp = ".";

static int twidth;

static void formatf(struct afile *fp0, struct afile *fplast);
static struct afile *gstat(struct afile *fp, char *file, int statarg, int *pnb);
static int fcmp(const void *a, const void *b);
static void formatd(char *name, int dotitle);
static int getdir(char *dir, struct afile **pfp0, struct afile **pfplast, int *isadir);
static char *savestr(char *str);
static char *cat(char *dir, char *file);
static char *fmtentry(struct afile *fp);
static char *fmtinum(struct afile *p);
static char *fmtsize(struct afile *p);
static char *fmtlstuff(struct afile *p);
static char *getname(uid_t uid);
static char *getgroup(gid_t gid);
static char *fmtmode(char *lp, int flags);
static int nblock(off_t size);

//
// A byte count in FILESYSTEM blocks, rounded up.  A block is BSIZE == 3072 bytes, which is
// not a power of two, so this is a divide where a PDP-11 ls had a shift -- and it is
// deliberately not in the unit `-s' prints.  See the head comment.
//
static int nblock(off_t size)
{
    return (size + BSIZE - 1) / BSIZE;
}

//
// The option parser, in place of getopt(3), which this libc has not got.  It takes the
// clustered form and the separate one alike -- `ls -lt' and `ls -l -t' -- and stops at the
// first argument that is not an option, or at `--'.  A lone `-' is a file name, as it is
// everywhere.
//
static int options(int argc, char *argv[])
{
    int i, ch;
    char *p;

    for (i = 1; i < argc; i++) {
        p = argv[i];
        if (p[0] != '-' || p[1] == '\0')
            break;
        if (p[1] == '-' && p[2] == '\0') {
            i++;
            break;
        }
        while ((ch = *++p) != '\0')
            switch (ch) {
            // -1, -C and -l override each other, so that shell aliasing works right.
            case '1':
                lflg = 0;
                Cflg = 0;
                break;
            case 'C':
                lflg = 0;
                Cflg = 1;
                break;
            case 'l':
                Cflg = 0;
                lflg++;
                break;
            case 'A':
                Aflg++;
                break;
            case 'F':
                Fflg++;
                break;
            case 'R':
                Rflg++;
                break;
            case 'a':
                aflg++;
                break;
            case 'c':
                uflg = 0; // -c overrides -u
                cflg++;
                break;
            case 'd':
                Rflg = 0; // -d overrides -R
                dflg++;
                break;
            case 'f':
                fflg++;
                break;
            case 'g':
                gflg++;
                break;
            case 'i':
                iflg++;
                break;
            case 'q':
                qflg = 1;
                break;
            case 'r':
                rflg = -1;
                break;
            case 's':
                sflg++;
                break;
            case 't':
                tflg++;
                break;
            case 'u':
                cflg = 0; // -u overrides -c
                uflg++;
                break;

            // ACCEPTED AND IGNORED, both of them, so that a command line written for a
            // BSD ls runs here instead of failing.  -L follows a symbolic link and this
            // kernel has none, so every stat already behaves as -L asks.  -o prints the
            // 4.4BSD chflags column and there is no st_flags to print.  The flags are set
            // and nothing reads them; ls.1.umm says so under Note.
            case 'L':
                Lflg++;
                break;
            case 'o':
                oflg++;
                break;

            default:
                fputs("usage: ls [ -1ACLFRacdfgiloqrstu ] [ file ]\n", stderr);
                exit(1);
            }
    }
    return i;
}

int main(int argc, char *argv[])
{
    struct afile *fp0, *fplast;
    struct afile *fp;
    struct sgttyb sgbuf;
    char *cp;
    int i, optind;

    Aflg = (getuid() == 0);
    time(&now);
    sixmonthsago = now - 6 * 30 * 24 * 60 * 60;
    now += 60;

    // There is no TIOCGWINSZ here and no way to ask the kernel how wide the terminal is,
    // so 80 unless the environment says otherwise.  A silly $COLUMNS is ignored rather
    // than obeyed: a width below 1 makes no columns at all.
    twidth = 80;
    if ((cp = getenv("COLUMNS")) != NULL) {
        i = atoi(cp);
        if (i > 0)
            twidth = i;
    }

    if (isatty(1)) {
        qflg = Cflg = 1;
        if (ioctl(1, TIOCGETP, (char *)&sgbuf) < 0 || (sgbuf.sg_flags & XTABS) != XTABS)
            usetabs = 1;
    } else
        usetabs = 1;

    optind = options(argc, argv);

    if (!lflg)
        oflg = 0;
    if (fflg) {
        Aflg++;
        aflg++;
        lflg = 0;
        sflg = 0;
        tflg = 0;
    }
    if (lflg)
        Cflg = 0;

    argc -= optind;
    argv += optind;
    if (argc == 0) {
        argc++;
        argv = &dotp;
    }

    fp = (struct afile *)calloc(argc, sizeof(struct afile));
    if (fp == NULL) {
        fputs("ls: out of memory\n", stderr);
        exit(1);
    }
    fp0 = fp;
    for (i = 0; i < argc; i++) {
        if (gstat(fp, *argv, 1, NULL) != NULL) {
            fp->fname = *argv;
            fp->fmode |= ISARG;
            fp++;
        }
        argv++;
    }
    fplast = fp;
    if (fflg == 0)
        qsort(fp0, fplast - fp0, sizeof(struct afile), fcmp);
    if (dflg) {
        formatf(fp0, fplast);
        exit(0);
    }

    if (fflg)
        fp = fp0;
    else {
        for (fp = fp0; fp < fplast && fp->ftype != 'd'; fp++)
            continue;
        formatf(fp0, fp);
    }

    if (fp < fplast) {
        if (fp > fp0)
            putchar('\n');
        for (;;) {
            formatd(fp->fname, argc > 1);
            while (subdirs != NULL) {
                struct subdirs *t;

                t       = subdirs;
                subdirs = t->sd_next;
                putchar('\n');
                formatd(t->sd_name, 1);
                free(t->sd_name);
                free(t);
            }
            if (++fp == fplast)
                break;
            putchar('\n');
        }
    }
    exit(0);
}

//
// One directory: read it, sort it, print it, and -- under -R -- push its subdirectories on
// to the list main() drains.  The push is backwards through the array so that the LIFO
// list comes out in sorted order.
//
static void formatd(char *name, int dotitle)
{
    struct afile *fp;
    struct subdirs *dp;
    struct afile *dfp0, *dfplast;
    int isadir;
    int nblk;

    nblk = getdir(name, &dfp0, &dfplast, &isadir);
    if (dfp0 == NULL)
        return;
    if (fflg == 0)
        qsort(dfp0, dfplast - dfp0, sizeof(struct afile), fcmp);
    if (dotitle)
        printf("%s%s\n", name, isadir ? ":" : "");
    if (lflg || sflg)
        printf("total %d\n", nblk * KBPB); // the multiply lives here; see the head comment
    formatf(dfp0, dfplast);
    if (Rflg)
        for (fp = dfplast - 1; fp >= dfp0; fp--) {
            if (fp->ftype != 'd' || strcmp(fp->fname, ".") == 0 || strcmp(fp->fname, "..") == 0)
                continue;
            dp = (struct subdirs *)malloc(sizeof(struct subdirs));
            if (dp == NULL) {
                fputs("ls: out of memory\n", stderr);
                exit(1);
            }
            dp->sd_name = savestr(cat(name, fp->fname));
            dp->sd_next = subdirs;
            subdirs     = dp;
        }
    for (fp = dfp0; fp < dfplast; fp++)
        if ((fp->fmode & ISARG) == 0 && fp->fname != NULL)
            free(fp->fname);
    free(dfp0);
}

//
// Read one directory into a grown array, and return the filesystem blocks its entries
// occupy.  This is the first caller in the tree of opendir(3) -- the free slots and the
// unterminated name are the library's business now, not this program's.
//
static int getdir(char *dir, struct afile **pfp0, struct afile **pfplast, int *isadir)
{
    struct afile *fp;
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    int nb;
    int nent = 20;

    dirp = opendir(dir);
    if (dirp == NULL) {
        *pfp0 = *pfplast = NULL;
        printf("%s unreadable\n", dir); // not stderr -- see the head comment
        return 0;
    }
    *isadir = (fstat(dirfd(dirp), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR);

    fp = *pfp0 = (struct afile *)calloc(nent, sizeof(struct afile));
    if (fp == NULL) {
        fputs("ls: out of memory\n", stderr);
        exit(1);
    }
    *pfplast = *pfp0 + nent;
    nb       = 0;
    while ((dp = readdir(dirp)) != NULL) {
        if (aflg == 0 && dp->d_name[0] == '.' &&
            (Aflg == 0 || dp->d_name[1] == 0 || (dp->d_name[1] == '.' && dp->d_name[2] == 0)))
            continue;
        if (gstat(fp, cat(dir, dp->d_name), Fflg + Rflg, &nb) == NULL)
            continue;
        fp->fnum  = dp->d_ino;
        fp->fname = savestr(dp->d_name);
        fp++;
        if (fp == *pfplast) {
            if (nent >= MAXFILES) {
                fputs("ls: too many files\n", stderr);
                break;
            }
            // realloc here FREES BEFORE IT ALLOCATES (lib/libc/gen/malloc.c), so a
            // failure has already lost the array: there is nothing to fall back to and
            // exit is the only honest answer.
            *pfp0 = (struct afile *)realloc(*pfp0, 2 * nent * sizeof(struct afile));
            if (*pfp0 == NULL) {
                fputs("ls: out of memory\n", stderr);
                exit(1);
            }
            fp       = *pfp0 + nent;
            *pfplast = fp + nent;
            nent *= 2;
        }
    }
    closedir(dirp);
    *pfplast = fp;
    return nb;
}

//
// Fill in one afile, stat'ing the file if any flag asked for that.  Upstream chooses
// between stat() and lstat() through a function pointer; there are no symbolic links here,
// so there is one call and no pointer.
//
static struct afile *gstat(struct afile *fp, char *file, int statarg, int *pnb)
{
    // memset rather than upstream's assignment from a static zero struct: the array this
    // fills came from calloc, so all-bits-zero is already the value every field starts at
    // here -- fname included, which the free() loop in formatd() tests against NULL.
    memset(fp, 0, sizeof *fp);
    fp->ftype = '-';
    if (statarg || sflg || lflg || tflg) {
        struct stat stb;

        if (stat(file, &stb) < 0) {
            printf("%s not found\n", file); // not stderr -- see the head comment
            return NULL;
        }
        fp->fblks = nblock(stb.st_size);
        fp->fsize = stb.st_size;
        switch (stb.st_mode & S_IFMT) {
        case S_IFDIR:
            fp->ftype = 'd';
            break;
        case S_IFBLK:
            fp->ftype = 'b';
            fp->fsize = stb.st_rdev;
            break;
        case S_IFCHR:
            fp->ftype = 'c';
            fp->fsize = stb.st_rdev;
            break;
        }
        fp->fnum = stb.st_ino;

        // 07777, NOT ~S_IFMT: ISARG would otherwise be S_IFREG.  See the head comment.
        fp->fmode = stb.st_mode & 07777;
        fp->fnl   = stb.st_nlink;
        fp->fuid  = stb.st_uid;
        fp->fgid  = stb.st_gid;
        if (uflg)
            fp->fmtime = stb.st_atime;
        else if (cflg)
            fp->fmtime = stb.st_ctime;
        else
            fp->fmtime = stb.st_mtime;
        if (pnb != NULL)
            *pnb += fp->fblks;
    }
    return fp;
}

//
// Print a run of entries, in columns when -C is on.  fmtentry() is called twice for each --
// once to measure and once to print -- which is safe only because the measuring pass
// finishes before the printing one begins; they share one static buffer.
//
static void formatf(struct afile *fp0, struct afile *fplast)
{
    struct afile *fp;
    int i, j, w;
    int width = 0, nentry = fplast - fp0;
    int columns, lines;
    char *cp;

    if (fp0 == fplast)
        return;
    if (lflg || Cflg == 0)
        columns = 1;
    else {
        for (fp = fp0; fp < fplast; fp++) {
            int len = strlen(fmtentry(fp));

            if (len > width)
                width = len;
        }
        if (usetabs)
            width = (width + 8) & ~7;
        else
            width += 2;
        columns = twidth / width;
        if (columns == 0)
            columns = 1;
    }
    lines = (nentry + columns - 1) / columns;
    for (i = 0; i < lines; i++) {
        for (j = 0; j < columns; j++) {
            fp = fp0 + j * lines + i;
            cp = fmtentry(fp);
            fputs(cp, stdout);
            if (fp + lines >= fplast) {
                putchar('\n');
                break;
            }
            w = strlen(cp);
            while (w < width) {
                if (usetabs) {
                    w = (w + 8) & ~7;
                    putchar('\t');
                } else {
                    w++;
                    putchar(' ');
                }
            }
        }
    }
}

//
// qsort's comparison, over the array of afile BY VALUE.  Command-line directories sort
// after everything else, so that `ls file dir' prints the file and then opens the
// directory.
//
static int fcmp(const void *a, const void *b)
{
    const struct afile *f1 = (const struct afile *)a;
    const struct afile *f2 = (const struct afile *)b;

    if (dflg == 0 && fflg == 0) {
        if ((f1->fmode & ISARG) && f1->ftype == 'd') {
            if ((f2->fmode & ISARG) == 0 || f2->ftype != 'd')
                return 1;
        } else {
            if ((f2->fmode & ISARG) && f2->ftype == 'd')
                return -1;
        }
    }
    if (tflg) {
        if (f2->fmtime == f1->fmtime)
            return 0;
        if (f2->fmtime > f1->fmtime)
            return rflg;
        return -rflg;
    }
    return rflg * strcmp(f1->fname, f2->fname);
}

//
// "dir/file" in a static buffer, overwritten per entry and read immediately.
//
static char *cat(char *dir, char *file)
{
    static char dfile[MAXPATH];
    int dlen;

    dlen = strlen(dir);
    if (dlen + 1 + (int)strlen(file) + 1 > MAXPATH) {
        fputs("ls: filename too long\n", stderr);
        exit(1);
    }
    if (dir[0] == '\0' || (dir[0] == '.' && dir[1] == '\0'))
        return strcpy(dfile, file);
    strcpy(dfile, dir);
    if (dir[dlen - 1] != '/' && *file != '/')
        dfile[dlen++] = '/';
    strcpy(dfile + dlen, file);
    return dfile;
}

static char *savestr(char *str)
{
    char *cp = strdup(str);

    if (cp == NULL) {
        fputs("ls: out of memory\n", stderr);
        exit(1);
    }
    return cp;
}

//
// One entry, formatted into a static buffer: the optional i-number and size columns, the
// optional long listing, the name, and the -F suffix.
//
// UPSTREAM APPENDED THE NAME WITH NO BOUND AT ALL.  Everything before the name is of known
// width and FMTSIZE is sized for it (see the head comment), but the loop is written
// against the end of the buffer anyway, because the one thing this function copies that it
// did not measure is the name.
//
static char *fmtentry(struct afile *fp)
{
    static char fmtres[FMTSIZE];
    char *dp, *ep;
    char *cp;
    int c;

    sprintf(fmtres, "%s%s%s", iflg ? fmtinum(fp) : "", sflg ? fmtsize(fp) : "",
            lflg ? fmtlstuff(fp) : "");
    dp = fmtres + strlen(fmtres);
    ep = fmtres + FMTSIZE - 2; // room for the -F suffix and the NUL

    for (cp = fp->fname; *cp != '\0' && dp < ep; cp++) {
        c = *cp;
        // Control characters only.  A byte above 0177 is a letter here, not junk;
        // see the head comment and ../README.md SS11.
        if (qflg && (c < ' ' || c == 0177))
            *dp++ = '?';
        else
            *dp++ = c;
    }
    if (Fflg) {
        if (fp->ftype == 'd')
            *dp++ = '/';
        else if (fp->fmode & 0111)
            *dp++ = '*';
    }
    *dp = '\0';
    return fmtres;
}

static char *fmtinum(struct afile *p)
{
    static char inumbuf[16]; // upstream had eight, which a seven-digit i-number overruns

    sprintf(inumbuf, "%6d ", p->fnum);
    return inumbuf;
}

static char *fmtsize(struct afile *p)
{
    static char sizebuf[16];

    sprintf(sizebuf, "%4d ", p->fblks * KBPB); // the multiply lives here too
    return sizebuf;
}

static char *fmtlstuff(struct afile *p)
{
    static char lstuffbuf[128];
    char gname[16], uname[16], fsize[16], ftime[32];
    char *lp = lstuffbuf;
    char *cp;

    // the owner
    cp = getname(p->fuid);
    if (cp != NULL)
        sprintf(uname, "%-9.9s", cp);
    else
        sprintf(uname, "%-9d", p->fuid);

    // the group, under -g
    if (gflg) {
        cp = getgroup(p->fgid);
        if (cp != NULL)
            sprintf(gname, "%-9.9s", cp);
        else
            sprintf(gname, "%-9d", p->fgid);
    }

    // the size -- or the device number, for a device
    if (p->ftype == 'b' || p->ftype == 'c')
        sprintf(fsize, "%3d,%4d", major(p->fsize), minor(p->fsize));
    else
        sprintf(fsize, "%8d", p->fsize);

    // the time.  ctime() gives "Day Mon dd hh:mm:ss yyyy\n"; +4 skips the day name and
    // +20 lands on the year.  Anything older than six months, or dated in the future,
    // shows the year instead of the time -- which is what makes the two forms line up at
    // twelve characters either way.
    cp = ctime(&p->fmtime);
    if (p->fmtime < sixmonthsago || p->fmtime > now)
        sprintf(ftime, " %-7.7s %-4.4s ", cp + 4, cp + 20);
    else
        sprintf(ftime, " %-12.12s ", cp + 4);

    *lp++ = p->ftype;
    lp    = fmtmode(lp, p->fmode);
    sprintf(lp, "%3d %s%s%s%s", p->fnl, uname, gflg ? gname : "", fsize, ftime);
    return lstuffbuf;
}

// The permission columns, one table per character printed.  Each is a count, then that
// many (mask, character) pairs, then the character to print when no mask matched.  const,
// which keeps them out of data.
static const int m1[] = { 1, S_IREAD, 'r', '-' };
static const int m2[] = { 1, S_IWRITE, 'w', '-' };
static const int m3[] = { 3, S_ISUID | S_IEXEC, 's', S_ISUID, 'S', S_IEXEC, 'x', '-' };
static const int m4[] = { 1, S_IREAD >> 3, 'r', '-' };
static const int m5[] = { 1, S_IWRITE >> 3, 'w', '-' };
static const int m6[] = { 3,           S_ISGID | (S_IEXEC >> 3), 's', S_ISGID, 'S',
                          S_IEXEC >> 3, 'x',                     '-' };
static const int m7[] = { 1, S_IREAD >> 6, 'r', '-' };
static const int m8[] = { 1, S_IWRITE >> 6, 'w', '-' };
static const int m9[] = { 3,           S_ISVTX | (S_IEXEC >> 6), 't', S_ISVTX, 'T',
                          S_IEXEC >> 6, 'x',                     '-' };

static const int *const m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

static char *fmtmode(char *lp, int flags)
{
    int i, n;
    const int *pairp;

    for (i = 0; i < (int)(sizeof(m) / sizeof(m[0])); i++) {
        pairp = m[i];
        n     = *pairp++;
        while (--n >= 0 && (flags & *pairp) != *pairp)
            pairp += 2;
        *lp++ = pairp[n >= 0];
    }
    return lp;
}

//
// uid and gid to name, through the password and group files.
//
// THE STRINGS MUST BE COPIED OUT, not pointed at: <pwd.h> and <grp.h> both say that every
// pointer in the returned struct aims into ONE SHARED LINE BUFFER which the next call
// overwrites.  That is what the cache below stores rather than the pointer, and it is not
// merely an optimisation -- a cache of pointers into that buffer would be a cache of the
// last name read, repeated.
//
// There is no setpassent(3) here to hold the file open; getpwuid() reopens it per miss,
// which is what the cache is for.
//
static char *getname(uid_t uid)
{
    static struct ncache {
        uid_t uid;
        char name[NMAX + 1];
    } c_uid[NCACHE];
    struct passwd *pw;
    struct ncache *cp;

    cp = c_uid + (uid & CAMASK);
    if (cp->uid == uid && *cp->name != '\0')
        return cp->name;
    if ((pw = getpwuid(uid)) == NULL)
        return NULL;
    cp->uid = uid;
    strncpy(cp->name, pw->pw_name, NMAX);
    cp->name[NMAX] = '\0';
    return cp->name;
}

static char *getgroup(gid_t gid)
{
    static struct ncache {
        gid_t gid;
        char name[NMAX + 1];
    } c_gid[NCACHE];
    struct group *gr;
    struct ncache *cp;

    cp = c_gid + (gid & CAMASK);
    if (cp->gid == gid && *cp->name != '\0')
        return cp->name;
    if ((gr = getgrgid(gid)) == NULL)
        return NULL;
    cp->gid = gid;
    strncpy(cp->name, gr->gr_name, NMAX);
    cp->name[NMAX] = '\0';
    return cp->name;
}
