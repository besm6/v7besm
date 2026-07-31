/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// ls -- list a file or a directory.
//
// The v7 program, and the largest of the commands task 24 put on the image.  The C11
// pass is the usual one -- prototypes, explicit return types, `static' on everything
// file-local, no untyped `register t;' -- and on top of it are the changes the MACHINE and
// the FILESYSTEM force.  Each of these is a silent failure if it is missed: the link
// succeeds and only the printed listing is wrong.
//
// THE PRINTF CONVERSIONS.  v7 wrote `%D' for a long; this libc's doprnt does not know that
// conversion, and an unknown conversion is ECHOED VERBATIM AND CONSUMES NO ARGUMENT
// (lib/libc/stdio/doprnt.c).  So `printf("total %D\n", tblocks)' would print the two
// characters `%D' and leave every later conversion in the same format reading the wrong
// argument.  Every %D is %d here.  (The length modifiers l/h/L/j/z/t are parsed and ignored
// -- a long IS an int -- so %ld was harmless; it is written %d anyway, since there is no
// long on this machine to mean.)
//
// A BLOCK IS BSIZE == 3072 BYTES.  nblock() was `(size+511)>>9', 512-byte blocks, and there
// is no BSHIFT/BMASK to replace the shift with -- 3072 is not a power of two, and
// sys/param.h says so in as many words.  It is a divide now.
//
// BUT `ls -s' AND `total' REPORT IN 1024-BYTE BLOCKS, not in that one: they print KBPB == 3
// of them per filesystem block (sys/param.h), which is what df(1M), du(1) and quot(1M) do
// too, so that a number means something without knowing BSIZE.  Two consequences worth
// expecting: every count is a MULTIPLE OF 3, the smallest thing that can be allocated being
// one 3072-byte block; and a number is now half a PDP-11's rather than a sixth, v7's block
// having been 512 bytes.  The multiply is at the printf -- nblock() and tblocks stay in
// filesystem blocks.
//
// ISARG IS S_IFREG.  v7 packed its own flag 0100000 into lflags beside `st_mode & ~S_IFMT'
// and got away with it because lflags was a 16-bit short, so the complement cleared the
// whole 0170000 type nibble and nothing above it existed.  Here st_mode is a 41-bit int and
// ~S_IFMT keeps every bit from 16 upward, so the two would be the same bit.  lflags is
// masked to 07777 instead -- everything the m1..m9 tables examine is <= 04000 -- and ISARG
// can no longer collide however wide st_mode becomes.
//
// DIRSIZ IS 18, not 14, and a struct direct is four words with a full-word d_ino.  So
// lname[15] is lname[DIRSIZ + 1], and `%.14s' is `%s' -- with the NUL that listdir() now
// writes, which is needed anyway because compar() calls strcmp() on that array and v7 left
// it unterminated.
//
// A FAT POINTER USED NOT TO SORT WITH `<'.  A char */void * carries a marker in bit 48 and a
// byte offset in bits 47-45, and the offset DECREMENTS as the pointer advances
// (doc/Besm6_Runtime_Library.md, b$pinc), so a raw integer comparison of the two words came
// out scrambled.  The compiler fixed that on 2026-06-17 -- the relational goes through
// b$pdiff now -- but makename() below still bounds itself with explicit indices, which is
// what this port wrote instead of introducing the first such comparison in the file, and
// which is a register compare where the relational is two calls.
//
// The rest, briefly:
//   - `extern char *malloc();' inside gstat() conflicts with <stdlib.h> and is deleted.  The
//     cast of malloc's void * to a struct pointer FLOORS -- drops the byte offset -- which is
//     exactly right, since lib/libc/gen/malloc.c guarantees every block starts at byte #0.
//   - the anonymous `struct { char dminor, dmajor; };' inside pentry() is a C11 constraint
//     violation (6.7p2: no declarator, no tag, not an enum) and was dead in v7 too -- it is a
//     fossil of a PDP-11 ls that took st_rdev apart by hand instead of using major()/minor().
//   - `argv = &dotp - 1;' formed a pointer before the start of an object.  It works here (a
//     char ** is thin, so it is a plain word decrement) but it is undefined and it is the
//     kind of thing that stops working in silence; it is a two-element array now.
//   - readdir() and select() were file-local names.  Neither collides today -- this tree has
//     no <dirent.h> and no BSD select -- but both are names a later header will want, so they
//     are listdir() and selbit().
//   - getname() copied a login name into a 16-character buffer with no bound.  It takes the
//     size now.  Its `c = '0'' on a colon looks like a typo and is not: it makes the digit
//     accumulator's `c - '0'' a harmless +0 on the colon that ENTERS field 2.
//   - the m1..m9 tables are const, which moves them out of data.
//
// WHAT IS NOT CHANGED.  ctime()'s cp+4 / cp+20 slicing is correct: lib/libc/gen/ctime.c
// builds the canonical 26-character "Day Mon dd hh:mm:ss yyyy\n", and those offsets depend on
// the column positions, not on the century patching v7 did.  NFILES stays 1024: at 12 words a
// struct lbuf plus a malloc header, the heap runs out at about 1,500 entries in this address
// space, so 1024 is by luck the right order of magnitude -- and both limits (`ls: too many
// files', `ls: out of memory') are graceful.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

// How many entries one listing can hold.  See the header: the heap gives out at roughly this
// order of magnitude anyway, and overflowing it prints `ls: too many files' rather than
// scribbling.
#define NFILES 1024

// ls's own flag in lflags, beside the permission bits.  It marks an entry that came from the
// COMMAND LINE rather than from a directory, which is what decides whether ln holds a pointer
// to the argument or a copy of the directory entry's name.
#define ISARG 0100000

// The longest "directory/name" makename() will build.  v7 wrote 100; a component is 18
// characters here rather than 14, and the copy is bounded now, so there is no reason to be
// tight about it.
#define DFILE 256

//
// One line of the listing.  Every short and long of v7's is one 48-bit word here, so each
// field is spelled as the thing it holds instead.
//
// THE UNION IS SAFE, and only because the ISARG discipline is airtight: main() is the sole
// writer of namep and sets ISARG on the very next line; listdir() is the sole writer of
// lname and its entries never carry ISARG; and both readers -- pentry() and compar() -- test
// the flag first.  The two arms are wholly incompatible (writing six characters fills the
// byte fields of the union's first word, and reading that back as a fat pointer gives an
// arbitrary marker, offset and address), so reading the wrong one is silent garbage rather
// than a fault.  Do not add a third accessor without checking the flag.
//
struct lbuf {
    union {
        char lname[DIRSIZ + 1]; // a directory entry's name, NUL-terminated by listdir()
        char *namep;            // ... or the command-line argument, when ISARG is set
    } ln;
    char ltype; // '-', 'd', 'b' or 'c' -- the first column of `ls -l'
    ino_t lnum; // inode number; -1 marks an entry stat() could not read
    int lflags; // the permission bits (07777) with ISARG possibly on top
    int lnl;    // link count
    uid_t luid;
    gid_t lgid;
    off_t lsize;   // the file's size -- OR its device number, when ltype is 'b' or 'c'
    time_t lmtime; // whichever of mtime/atime/ctime the flags asked for
};

static FILE *pwdf, *dirf;
static char stdbuf[BUFSIZ]; // the standard output's buffer: 3072 chars, 512 words

static int aflg, dflg, lflg, sflg, tflg, uflg, iflg, fflg, gflg, cflg;
static int rflg = 1; // +1 normally, -1 for `ls -r'; compar() multiplies by it
static time_t year;  // six months ago: older files print a year instead of a time
static int pmflags;  // the mode being printed -- the only channel from pmode() to selbit()
static int lastuid = -1;
static char tbuf[16]; // the login name getname() last looked up
static int tblocks;   // blocks used by the directory being listed, for the `total' line
static int statreq;   // set by any flag that needs a stat(), so gstat() can skip it otherwise
static struct lbuf *flist[NFILES];
static struct lbuf **lastp  = flist;
static struct lbuf **firstp = flist;

// The argument vector used when ls is given no arguments at all.  Slot 0 is never read: the
// loop below starts with *++argv, exactly as it does for a real argv.
static char *dotv[2] = { NULL, "." };

static void pentry(struct lbuf *ap);
static int getname(int uid, char *buf, int bufsz);
static int nblock(off_t size);
static void pmode(int aflag);
static void selbit(const int *pairp);
static char *makename(char *dirn, char *filen);
static void listdir(char *dirn);
static struct lbuf *gstat(char *file, int argfl);
static int compar(const void *pp1, const void *pp2);

int main(int argc, char *argv[])
{
    int i;
    struct lbuf *ep, **ep1;
    struct lbuf **slastp;
    struct lbuf **epp;
    char *t;
    time_t now;

    setbuf(stdout, stdbuf);
    time(&now);
    year = now - 6L * 30L * 24L * 60L * 60L; // 6 months ago

    // The options, all in one argument in the v7 manner: `ls -lt', not `ls -l -t'.  Note
    // that --argc is evaluated whether or not the test succeeds, which is how argc is
    // decremented in the no-flags case as well.
    if (--argc > 0 && *argv[1] == '-') {
        argv++;
        while (*++*argv)
            switch (**argv) {
            case 'a': // list entries whose names begin with a dot
                aflg++;
                continue;

            case 's': // print the size in blocks
                sflg++;
                statreq++;
                continue;

            case 'd': // list a directory itself, not its contents
                dflg++;
                continue;

            case 'g': // with -l, show the group instead of the owner
                gflg++;
                continue;

            case 'l': // the long listing
                lflg++;
                statreq++;
                continue;

            case 'r': // reverse the sort
                rflg = -1;
                continue;

            case 't': // sort by time rather than by name
                tflg++;
                statreq++;
                continue;

            case 'u': // ... and use the access time
                uflg++;
                continue;

            case 'c': // ... or the inode-change time
                cflg++;
                continue;

            case 'i': // print the inode number
                iflg++;
                continue;

            case 'f': // force: treat every argument as a directory and do not sort or stat
                fflg++;
                continue;

            default:
                continue;
            }
        argc--;
    }

    if (fflg) {
        aflg++;
        lflg    = 0;
        sflg    = 0;
        tflg    = 0;
        statreq = 0;
    }

    // -l prints owner names rather than numbers, which means reading the password file.
    if (lflg) {
        t = "/etc/passwd";
        if (gflg)
            t = "/etc/group";
        pwdf = fopen(t, "r");
    }

    // No arguments: list the current directory.
    if (argc == 0) {
        argc++;
        argv = dotv;
    }

    for (i = 0; i < argc; i++) {
        if ((ep = gstat(*++argv, 1)) == NULL)
            continue;
        ep->ln.namep = *argv;
        ep->lflags |= ISARG; // and so the union's namep arm is the live one -- see struct lbuf
    }

    qsort(firstp, lastp - firstp, sizeof *lastp, compar);
    slastp = lastp;

    // Each argument in turn.  A directory is read out here, into the part of flist above
    // slastp, and that part is then sorted and printed by itself.
    for (epp = firstp; epp < slastp; epp++) {
        ep = *epp;
        if ((ep->ltype == 'd' && dflg == 0) || fflg) {
            if (argc > 1)
                printf("\n%s:\n", ep->ln.namep);
            lastp = slastp;
            listdir(ep->ln.namep);
            if (fflg == 0)
                qsort(slastp, lastp - slastp, sizeof *lastp, compar);
            if (lflg || sflg)
                printf("total %d\n", tblocks * KBPB);
            for (ep1 = slastp; ep1 < lastp; ep1++)
                pentry(*ep1);
        } else
            pentry(ep);
    }
    return 0;
}

//
// Print one entry, in whichever of the four shapes the flags ask for.
//
static void pentry(struct lbuf *ap)
{
    int t;
    struct lbuf *p;
    char *cp;

    p = ap;
    if (p->lnum == -1) // gstat() could not stat it, and has already said so
        return;
    if (iflg)
        printf("%5d ", p->lnum);
    if (sflg)
        printf("%4d ", nblock(p->lsize) * KBPB);

    if (lflg) {
        putchar(p->ltype);
        pmode(p->lflags);
        printf("%2d ", p->lnl);

        t = p->luid;
        if (gflg)
            t = p->lgid;
        if (getname(t, tbuf, (int)sizeof tbuf) == 0)
            printf("%-6.6s", tbuf);
        else
            printf("%-6d", t);

        // For a device, lsize is not a size at all: gstat() put st_rdev there.
        if (p->ltype == 'b' || p->ltype == 'c')
            printf("%3d,%3d", major(p->lsize), minor(p->lsize));
        else
            printf("%7d", p->lsize);

        // ctime() gives "Day Mon dd hh:mm:ss yyyy\n"; +4 skips the day name and +20 lands on
        // the year.  Anything older than six months shows the year instead of the time, which
        // is what makes the two columns line up at 12 characters either way.
        cp = ctime(&p->lmtime);
        if (p->lmtime < year)
            printf(" %-7.7s %-4.4s ", cp + 4, cp + 20);
        else
            printf(" %-12.12s ", cp + 4);
    }

    if (p->lflags & ISARG)
        printf("%s\n", p->ln.namep);
    else
        printf("%s\n", p->ln.lname); // listdir() terminated it; see struct lbuf
}

//
// Look a uid up in /etc/passwd (or a gid in /etc/group) and leave the name in buf.  Returns
// 0 when it found one and -1 otherwise, and remembers the last answer, since a directory
// listing usually names the same owner over and over.
//
// The parse is v7's: count colons, take field 0 as the name and field 2 as the number.  THE
// `c = '0'' IS DELIBERATE -- it makes the accumulator's `c - '0'' a harmless +0 for the colon
// that enters field 2, so the digits can be summed without a separate test.  It looks like a
// typo.  It is not.
//
static int getname(int uid, char *buf, int bufsz)
{
    int j, c, n, i;

    if (uid == lastuid)
        return (0);
    if (pwdf == NULL)
        return (-1);
    rewind(pwdf);
    lastuid = -1;

    do {
        i = 0;
        j = 0;
        n = 0;
        while ((c = fgetc(pwdf)) != '\n') {
            if (c == EOF)
                return (-1);
            if (c == ':') {
                j++;
                c = '0';
            }
            if (j == 0 && i < bufsz - 1) // v7 was unbounded here
                buf[i++] = c;
            if (j == 2)
                n = n * 10 + c - '0';
        }
    } while (n != uid);

    buf[i]  = '\0';
    lastuid = uid;
    return (0);
}

//
// A byte count in FILESYSTEM blocks, rounded up.  A block is BSIZE == 3072 bytes here, which
// is not a power of two, so this is a divide where v7 had a shift.
//
// It is deliberately not in the unit `ls -s' prints, and tblocks accumulates what it returns:
// the multiply by KBPB happens at the two printfs and nowhere else, so that this function and
// that accumulator both keep meaning what their names say.  See the header.
//
static int nblock(off_t size)
{
    return ((size + BSIZE - 1) / BSIZE);
}

// The permission columns, one table per character printed.  Each is a count, then that many
// (mask, character) pairs, then the character to print when no mask matched.
static const int m1[] = { 1, S_IREAD >> 0, 'r', '-' };
static const int m2[] = { 1, S_IWRITE >> 0, 'w', '-' };
static const int m3[] = { 2, S_ISUID, 's', S_IEXEC >> 0, 'x', '-' };
static const int m4[] = { 1, S_IREAD >> 3, 'r', '-' };
static const int m5[] = { 1, S_IWRITE >> 3, 'w', '-' };
static const int m6[] = { 2, S_ISGID, 's', S_IEXEC >> 3, 'x', '-' };
static const int m7[] = { 1, S_IREAD >> 6, 'r', '-' };
static const int m8[] = { 1, S_IWRITE >> 6, 'w', '-' };
static const int m9[] = { 2, S_ISVTX, 't', S_IEXEC >> 6, 'x', '-' };

static const int *const m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

//
// Print the nine permission characters of one mode.
//
static void pmode(int aflag)
{
    const int *const *mp;

    pmflags = aflag;
    for (mp = &m[0]; mp < &m[sizeof(m) / sizeof(m[0])];)
        selbit(*mp++);
}

//
// One column of the above: walk the table until a mask matches pmflags, and print that
// entry's character -- or the table's last character if none did.
//
static void selbit(const int *pairp)
{
    int n;

    n = *pairp++;
    while (--n >= 0 && (pmflags & *pairp++) == 0)
        pairp++;
    putchar(*pairp);
}

// The path makename() builds; one buffer, overwritten per entry, and read immediately.
static char dfile[DFILE];

//
// "directory" + "/" + "name" in dfile[].  Written with explicit indices rather than walking
// pointers -- see the header.
//
static char *makename(char *dirn, char *filen)
{
    int d, i;

    // Leave room for the slash, a full-length component and the NUL.
    for (d = 0; d < DFILE - DIRSIZ - 2 && dirn[d] != 0; d++)
        dfile[d] = dirn[d];
    dfile[d++] = '/';
    for (i = 0; i < DIRSIZ && filen[i] != 0; i++)
        dfile[d++] = filen[i];
    dfile[d] = 0;
    return (dfile);
}

//
// Read a directory into flist, one struct lbuf per entry.  v7 called this readdir(); the name
// belongs to <dirent.h> now, which this tree does not have yet but will.
//
static void listdir(char *dirn)
{
    static struct direct dentry;
    int j;
    struct lbuf *ep;

    if ((dirf = fopen(dirn, "r")) == NULL) {
        printf("%s unreadable\n", dirn);
        return;
    }
    tblocks = 0;

    // One struct direct is 24 bytes and BUFSIZ is one filesystem block, so a refill holds
    // exactly DIRPB entries and no entry ever straddles the stdio buffer.
    for (;;) {
        if (fread((char *)&dentry, sizeof(dentry), 1, dirf) != 1)
            break;
        if (dentry.d_ino == 0 // a free slot
            ||
            (aflg == 0 && dentry.d_name[0] == '.' &&
             (dentry.d_name[1] == '\0' || (dentry.d_name[1] == '.' && dentry.d_name[2] == '\0'))))
            continue;

        ep = gstat(makename(dirn, dentry.d_name), 0);
        if (ep == NULL)
            continue;
        if (ep->lnum != -1)
            ep->lnum = dentry.d_ino;

        for (j = 0; j < DIRSIZ; j++)
            ep->ln.lname[j] = dentry.d_name[j];
        ep->ln.lname[DIRSIZ] = '\0'; // v7 left this array unterminated; compar() strcmp()s it
    }
    fclose(dirf);
}

//
// Make one struct lbuf: allocate it, hang it on flist, and stat() the file into it if
// anything asked for that.  Returns NULL when there is no memory left, or when a command-line
// argument does not exist.
//
static struct lbuf *gstat(char *file, int argfl)
{
    struct stat statb;
    struct lbuf *rep;
    static int nomocore;

    if (nomocore)
        return (NULL);
    rep = (struct lbuf *)malloc(sizeof(struct lbuf));
    if (rep == NULL) {
        fprintf(stderr, "ls: out of memory\n");
        nomocore = 1;
        return (NULL);
    }
    if (lastp >= &flist[NFILES]) {
        static int msg;
        lastp--; // overwrite the last entry rather than run off the array
        if (msg == 0) {
            fprintf(stderr, "ls: too many files\n");
            msg++;
        }
    }
    *lastp++ = rep;

    rep->lflags = 0;
    rep->lnum   = 0;
    rep->ltype  = '-';

    if (argfl || statreq) {
        if (stat(file, &statb) < 0) {
            printf("%s not found\n", file);
            statb.st_ino  = -1;
            statb.st_size = 0;
            statb.st_mode = 0;
            if (argfl) {
                lastp--;
                return (NULL);
            }
        }
        rep->lnum  = statb.st_ino;
        rep->lsize = statb.st_size;

        switch (statb.st_mode & S_IFMT) {
        case S_IFDIR:
            rep->ltype = 'd';
            break;

        case S_IFBLK:
            rep->ltype = 'b';
            rep->lsize = statb.st_rdev;
            break;

        case S_IFCHR:
            rep->ltype = 'c';
            rep->lsize = statb.st_rdev;
            break;
        }

        // 07777, not ~S_IFMT: ISARG is 0100000 and so is S_IFREG.  See the header.
        rep->lflags = statb.st_mode & 07777;
        rep->luid   = statb.st_uid;
        rep->lgid   = statb.st_gid;
        rep->lnl    = statb.st_nlink;

        if (uflg)
            rep->lmtime = statb.st_atime;
        else if (cflg)
            rep->lmtime = statb.st_ctime;
        else
            rep->lmtime = statb.st_mtime;

        tblocks += nblock(statb.st_size);
    }
    return (rep);
}

//
// qsort's comparison.  C11 spells it (const void *, const void *), where v7 could hand qsort
// a function of any shape at all; the two arguments are fat pointers at byte #0 of a word,
// so the cast back to a thin struct lbuf ** floors to exactly the right word.
//
static int compar(const void *pp1, const void *pp2)
{
    const struct lbuf *p1 = *(struct lbuf *const *)pp1;
    const struct lbuf *p2 = *(struct lbuf *const *)pp2;

    // Command-line DIRECTORIES sort after everything else, so that `ls file dir' prints the
    // file first and then opens the directory.
    if (dflg == 0) {
        if ((p1->lflags & ISARG) && p1->ltype == 'd') {
            if (!((p2->lflags & ISARG) && p2->ltype == 'd'))
                return (1);
        } else {
            if ((p2->lflags & ISARG) && p2->ltype == 'd')
                return (-1);
        }
    }

    if (tflg) {
        if (p2->lmtime == p1->lmtime)
            return (0);
        if (p2->lmtime > p1->lmtime)
            return (rflg);
        return (-rflg);
    }

    return (rflg * strcmp((p1->lflags & ISARG) ? p1->ln.namep : p1->ln.lname,
                          (p2->lflags & ISARG) ? p2->ln.namep : p2->ln.lname));
}
