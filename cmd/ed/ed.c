/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// ed -- the editor.
//
//      ed [-] [-q] [file]
//
// Task C3 (../TODO.md), and the pivot: until this program ran, nothing on this machine
// could AUTHOR text.  Every file on the image was written on the build host and staged in
// by b6fsutil; the guest could create, copy, move and re-permission files, and since task
// C2b a script could branch, but it could not produce a line of anything.  ../README.md is
// the porting recipe and a bare SS N below is one of its sections; ./README.md is what this
// port taught.
//
// FIVE THINGS ABOUT THIS FILE, in the order they cost time.
//
// 1.  TWENTY char * RELATIONAL COMPARISONS, not the ten ../README.md SS2's table claimed --
//     the densest concentration in the whole survey, and every one of them bounds a buffer
//     that the regex engine or the substitute path is writing into.  `<' between two char *
//     does not order them here: the byte offset lives in bits 47-45, above the word
//     address, and it DECREMENTS as the pointer advances, so the ordering comes out
//     scrambled and inverted within a word.  There is no relational helper; SUBTRACTION is
//     fine (b$pdiff decodes both operands) and that is the whole toolkit.  Every one is an
//     int index or an int difference now, and the ones that survive as pointer walks are
//     dereference-and-increment, which was never in question.  Two of the twenty went away
//     with -x below, so nineteen were rewritten.
//
// 2.  THE FILE MIXES BOTH POINTER WIDTHS, FREELY, AND THAT IS THE TRAP.  zero, dot, dol,
//     addr1, addr2, names[] and every `a1'/`a2'/`markp' over them are int * -- THIN word
//     pointers, which compare and subtract correctly, so the thirty-two comparisons among
//     them are left exactly as v7 wrote them.  linebuf, genbuf, expbuf, rhsbuf, globuf and
//     their cursors are char * and fat.  Identical syntax, opposite behaviour, one screen
//     apart: `while (a1 <= addr2)' in putfile() is right and `while (lp < loc1)' in dosub()
//     was wrong.  It is the pointed-to type that decides, and nothing about the shape of
//     the loop says which you are looking at.
//
// 3.  -x IS GONE.  v7's encrypting mode ran the buffer and the temp file through crypt(3):
//     getkey() read a key with ECHO off, crinit() derived a permutation from it and
//     crblock() ran it over every block.  It execs /usr/lib/makekey, which is not on this
//     image and is in no task, so the mode could not work; and crinit()'s seed arithmetic
//     depended on 32-bit wraparound, so the keys it derived would not have matched a
//     PDP-11's in any case.  Deleted whole -- getkey(), crinit(), crblock(), makekey(), the
//     xflag/xtflag/kflag state, key[], perm[], tperm[], crbuf[], the `x' command and the
//     last use of <sgtty.h>: 170 lines and 342 words of static data.  A deliberate
//     divergence, so it is written down twice, here and in ed.1, on touch(1)'s precedent
//     (../README.md SS10).
//
//     It took a WILD longjmp with it, which is the part worth keeping in mind.  main() called
//     getkey() from the -x arm, and getkey() calls error("Input not tty") -- but that arm ran
//     BEFORE the setjmp(savej) below, so the jump went through an uninitialised jmp_buf.  The
//     rule it leaves behind stands whatever else changes here: NOTHING IN THIS PROGRAM MAY
//     CALL error() BEFORE main() HAS REACHED ITS setjmp.  The two places that want to
//     complain earlier -- an over-long file name and a failed malloc -- write and exit
//     instead, and init()'s unchecked creat/open is left unchecked for the same reason: it
//     runs before the setjmp on the first call and from the `e' command, where error() is
//     right, on every later one.  v7 reports that failure at the first blkio() as `?TMP',
//     which is a real diagnostic in a reachable place, so it stands.
//
// 4.  IT IS EIGHT-BIT TRANSPARENT, and v7's was emphatically not.  ../README.md SS11 makes
//     that a rule of the recipe and task C11 had just done it to the shell, but the reason
//     it could not be skipped here is sharper: v7's getfile() calls error() on any byte with
//     0200 set, so this editor could not so much as OPEN a file that cat can already print.
//     Four masks went (getchr(), gettty(), getfile(), dosub()), and one bit had to be
//     re-encoded -- see QESC below, which is the structural piece.  What follows from it is
//     the same rule the shell's globber follows: THE PATTERN LANGUAGE MATCHES BYTES.  `.'
//     matches one byte and not one character, `*' after a multi-byte character repeats its
//     last byte, and a range over two multi-byte characters is a range of bytes and does not
//     mean what it looks like.  ed.1 says so.
//
//     What the mask at getchr() actually did is worth stating exactly, because it is worse
//     than the `echo privet' bug that prompted task C11.  Cyrillic capitals are the UTF-8
//     pairs 320 220 through 320 257, and `& 0177' folds that second byte onto 020..057 --
//     a range that contains `*' (052), `.' (056), `$' (044) and `+' (053).  So a Cyrillic
//     letter typed into a REGULAR EXPRESSION turned into a metacharacter: 320 252 is the
//     letter hard sign and masked to `*', 320 256 is yu and masked to `.'.  The editor did
//     not mangle the text so much as silently rewrite the pattern.
//
// 5.  THERE IS NOT ONE FORMAT STRING IN THE PROGRAM, which makes this the first port for
//     which SS3's %D pass is a no-op: numeric output is putd() over write(2), so ed links no
//     stdio at all.  `long count' is one 41-bit word here, which is more range than the
//     PDP-11's two-word long had, not less; the three lseek() casts are (off_t) now, off_t
//     being one word too.
//
// THE OTHER TWO THAT WOULD HAVE BITTEN SILENTLY.  mktemp() fills the trailing `X's in the
// buffer it is HANDED, and v7 handed it a string literal -- which lives in the read-only
// const segment on this machine, shared text when a program is linked pure.  It is a
// writable static now.  And `if (((int)oldintr & 01) == 0)' tested bit 0 of a function
// pointer to mean "was SIG_IGN", SIG_IGN being 1; but SIG_ERR is -1 and has bit 0 set too,
// so a signal() that FAILED read as "was ignored" and the handler was silently never
// installed.  Both are real, both are fixed, and neither is about C11.
//
// SIGNALS REALLY ARE DELIVERED HERE, which is worth saying because <signal.h>'s own header
// comment reads as though they are not: the frame is kernel/sendsig.c, kernel/test/usig
// runs a whole delivery on the machine, and lib/libc/gen/sleep.c already does the
// handler-then-longjmp dance that this program's ^C recovery is built on.  So onintr() and
// onhup() are live code, and ed.hup is a file this machine can really write.
//
// THE TEMP FILE'S BLOCKS ARE 512 BYTES AND STAY THAT WAY.  They are ed's own unit, not the
// filesystem's, so SS4 does not apply and BSIZE has no business here: the line token packs a
// block number and a byte offset into one word (`bno = (atl >> 8) & 0377'), bit 0 of a token
// is stolen as the global-command mark, and putline() rounds a line to a four-byte slot to
// keep that bit free.  Change any of it and the pager stops addressing itself.
//
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define FNSIZE 64
#define LBSIZE 512
#define GBSIZE 256
#define NBRA   5

// ESIZE was 128.  A compiled expression is opcodes and, for a character class, the class's
// members LAID OUT LITERALLY -- so a class over UTF-8 costs two bytes a letter and 128
// would hold about sixty of them.  expbuf is 86 words at 512 and there is no reason to be
// tight.  The one thing that had to come with the raise is CCLMAX: the class's length is a
// single byte, which advance() adds to its cursor to step over the class, so a class may
// not have more than 255 bytes in it.  v7 never needed that check -- ESIZE bounded the
// count below 255 all by itself -- and without it, raising ESIZE would have turned a
// diagnostic into a silently truncated pattern.
#define ESIZE  512
#define CCLMAX 255

#define CBRA  1
#define CCHR  2
#define CDOT  4
#define CCL   6
#define NCCL  8
#define CDOL  10
#define CEOF  11
#define CKET  12
#define CBACK 14

#define STAR 01

// The escape mark in rhsbuf, and the whole of what item 4 above cost.  compsub() has to
// remember which bytes of a substitution's replacement text arrived backslash-escaped:
// `\&' is a literal ampersand where a bare `&' is the matched text, and `\1' is a
// back-reference where a bare `1' is a digit.  v7 recorded it by setting bit 0200 of the
// byte, which a byte-transparent editor cannot spare, so the mark is a PREFIX BYTE now --
// 0377, as the shell's QESC is (../sh/README.md):
//
//      an ordinary byte c, c != QESC    c
//      an escaped byte \c               QESC c
//      a QESC byte, escaped or not      QESC QESC
//      a newline inside a g command     QESC \n
//
// A bare QESC therefore never appears -- compsub() only ever writes the pair -- so dosub()'s
// decode cannot be ambiguous.  Escaped and unescaped 0377 deliberately share one encoding,
// for the reason they do in the shell: 0377 is not a metacharacter in a replacement, so its
// escapedness cannot be observed by anything.
#define QESC 0377

// rhsbuf was LBSIZE/2.  A prefix mark costs a byte where a set bit cost none, so a
// replacement that v7 accepted -- 200 bytes of text carrying sixty escapes -- would newly
// have overflowed.  512 is 43 words and removes the regression; it is the one place the
// encoding costs anything at all.
#define RHSIZE LBSIZE

static char Q[] = "";
static char T[] = "TMP";

#define READ  0
#define WRITE 1

static int peekc;
static int lastc;
static char savedfile[FNSIZE];
static char file[FNSIZE];
static char linebuf[LBSIZE];
static char rhsbuf[RHSIZE];
static char expbuf[ESIZE + 4];
static int circfl;

// The thin ones: word pointers into the line-token array.  See item 2 in the header.
static int *zero;
static int *dot;
static int *dol;
static int *addr1;
static int *addr2;

static char genbuf[LBSIZE];
static long count;
static char *nextip;
static char *linebp;
static int ninbuf;
static int io = -1;
static int pflag;
static void (*oldhup)(int);
static void (*oldquit)(int);
static int vflag = 1;
static int listf;
static int col;
static char *globp;
static int tfile = -1;
static int tline;

// mktemp() writes into this, which is why it is an array and not the string literal v7
// passed: a literal is const-segment storage here.
static char tfnbuf[] = "/tmp/eXXXXX";
static char *tfname;

static char *loc1;
static char *loc2;
static char *locs;
static char ibuff[512];
static int iblock = -1;
static char obuff[512];
static int oblock = -1;
static int ichanged;
static int nleft;
static char WRERR[] = "WRITE ERROR";
static int names[26];
static int anymarks;
static char *braslist[NBRA];
static char *braelist[NBRA];
static int nbra;
static int subnewa;
static int subolda;
static int fchange;
static int wrapp;

// v7 wrote `unsigned', for no reason: it is a count of ints in a malloc'd array and it is
// compared against a signed pointer difference.  SS3 prefers int wherever unsigned bought
// nothing.
static int nlall = 128;

static jmp_buf savej;

static int *address(void);
static int advance(char *lp, char *ep);
static int append(int (*f)(void), int *a);
static int backref(int i, char *lp);
static void blkio(int b, char *buf, int iofcn);
static void callunix(void);
static int cclass(char *set, int c, int af);
static void commands(void);
static void compile(int aeof);
static int compsub(void);
static void delete(void);
static void dosub(void);
static void error(char *s);
static void exfile(void);
static int execute(int gf, int *addr);
static void filename(int comm);
static void gdelete(void);
static char *getblock(int atl, int iof);
static int getchr(void);
static int getcopy(void);
static int getfile(void);
static char *getlin(int tl);
static int getsub(void);
static int gettty(void);
static void global(int k);
static void init(void);
static void join(void);
static void move(int cflag);
static void newline(void);
static void nonzero(void);
static void onhup(int sig);
static void onintr(int sig);
static int place(int sn, char *l1, char *l2);
static void putchr(int ac);
static void putd(void);
static void putfile(void);
static int putline(void);
static void putstr(char *sp);
static void quit(int sig);
static void rdelete(int *ad1, int *ad2);
static void reverse(int *a1, int *a2);
static void setall(void);
static void setdot(void);
static void setnoaddr(void);
static void substitute(int inglob);

int main(int argc, char **argv)
{
    char *p1;
    int n;
    void (*oldintr)(int);

    oldquit = signal(SIGQUIT, SIG_IGN);
    oldhup  = signal(SIGHUP, SIG_IGN);
    oldintr = signal(SIGINT, SIG_IGN);

    // v7: `if ((int)signal(SIGTERM, SIG_IGN) == 0)'.  Zero is SIG_DFL, and the comparison
    // means "nobody has arranged otherwise, so die tidily on a TERM".
    if (signal(SIGTERM, SIG_IGN) == SIG_DFL)
        signal(SIGTERM, quit);

    argv++;
    while (argc > 1 && **argv == '-') {
        switch ((*argv)[1]) {
        case '\0':
            vflag = 0;
            break;

        case 'q':
            signal(SIGQUIT, SIG_DFL);
            vflag = 1;
            break;
        }
        argv++;
        argc--;
    }

    if (argc > 1) {
        // v7 copied argv[1] into savedfile with an unbounded loop.  Every port so far has
        // had to bound one of these (../README.md SS6); a name longer than the field is the
        // same `?' any other bad file name gets.
        p1 = *argv;
        for (n = 0; p1[n]; n++) {
            if (n >= FNSIZE - 1) {
                putchr('?');
                putstr(Q);
                exit(2);
            }
            savedfile[n] = p1[n];
        }
        savedfile[n] = 0;
        globp        = "r";
    }
    if ((zero = (int *)malloc(nlall * sizeof(int))) == NULL) {
        // v7 did not look.  There is no buffer yet, so there is nowhere to say `?' from.
        write(2, "ed: MEM?\n", 9);
        exit(2);
    }
    tfname = mktemp(tfnbuf);
    init();

    // v7 tested bit 0 of the old disposition, SIG_IGN being 1 -- but SIG_ERR is -1 and has
    // bit 0 set as well, so a signal() that failed was read as "the caller ignored this"
    // and the handler was never installed.  Ask the question that was meant.
    if (oldintr != SIG_IGN)
        signal(SIGINT, onintr);
    if (oldhup != SIG_IGN)
        signal(SIGHUP, onhup);

    // Every error() in the program longjmps back to here, from any depth: out of the
    // recursive advance(), out of the pager, out of a global's nested commands().
    setjmp(savej);
    commands();
    quit(0);
    return 0;
}

static void commands(void)
{
    int *a1;
    int c;

    for (;;) {
        if (pflag) {
            pflag = 0;
            addr1 = addr2 = dot;
            goto print;
        }
        addr1 = 0;
        addr2 = 0;
        do {
            addr1 = addr2;
            if ((a1 = address()) == 0) {
                c = getchr();
                break;
            }
            addr2 = a1;
            if ((c = getchr()) == ';') {
                c   = ',';
                dot = a1;
            }
        } while (c == ',');
        if (addr1 == 0)
            addr1 = addr2;
        switch (c) {
        case 'a':
            setdot();
            newline();
            append(gettty, addr2);
            continue;

        case 'c':
            delete();
            append(gettty, addr1 - 1);
            continue;

        case 'd':
            delete();
            continue;

        case 'E':
            fchange = 0;
            c       = 'e';
            // fall through
        case 'e':
            setnoaddr();
            if (vflag && fchange) {
                fchange = 0;
                error(Q);
            }
            filename(c);
            init();
            addr2 = zero;
            goto caseread;

        case 'f':
            setnoaddr();
            filename(c);
            putstr(savedfile);
            continue;

        case 'g':
            global(1);
            continue;

        case 'i':
            setdot();
            nonzero();
            newline();
            append(gettty, addr2 - 1);
            continue;

        case 'j':
            if (addr2 == 0) {
                addr1 = dot;
                addr2 = dot + 1;
            }
            setdot();
            newline();
            nonzero();
            join();
            continue;

        case 'k':
            if ((c = getchr()) < 'a' || c > 'z')
                error(Q);
            newline();
            setdot();
            nonzero();
            names[c - 'a'] = *addr2 & ~01;
            anymarks |= 01;
            continue;

        case 'm':
            move(0);
            continue;

        case '\n':
            if (addr2 == 0)
                addr2 = dot + 1;
            addr1 = addr2;
            goto print;

        case 'l':
            listf++;
            // fall through
        case 'p':
        case 'P':
            newline();
        print:
            setdot();
            nonzero();
            a1 = addr1;
            do {
                putstr(getlin(*a1++));
            } while (a1 <= addr2);
            dot   = addr2;
            listf = 0;
            continue;

        case 'Q':
            fchange = 0;
            // fall through
        case 'q':
            setnoaddr();
            newline();
            quit(0);

        case 'r':
            filename(c);
        caseread:
            if ((io = open(file, O_RDONLY)) < 0) {
                lastc = '\n';
                error(file);
            }
            setall();
            ninbuf = 0;
            c      = zero != dol;
            append(getfile, addr2);
            exfile();
            fchange = c;
            continue;

        case 's':
            setdot();
            nonzero();
            substitute(globp != 0);
            continue;

        case 't':
            move(1);
            continue;

        case 'u':
            setdot();
            nonzero();
            newline();
            if ((*addr2 & ~01) != subnewa)
                error(Q);
            *addr2 = subolda;
            dot    = addr2;
            continue;

        case 'v':
            global(0);
            continue;

        case 'W':
            wrapp++;
            // fall through
        case 'w':
            setall();
            nonzero();
            filename(c);
            if (!wrapp || ((io = open(file, O_WRONLY)) == -1) ||
                ((lseek(io, (off_t)0, SEEK_END)) == -1))
                if ((io = creat(file, 0666)) < 0)
                    error(file);
            wrapp = 0;
            putfile();
            exfile();
            if (addr1 == zero + 1 && addr2 == dol)
                fchange = 0;
            continue;

        case '=':
            setall();
            newline();
            // The mask is a PDP-11 16-bit-ism and it is left where it is: it cannot bite,
            // because getblock()'s own `bno >= 255' caps the buffer at 130,560 bytes of
            // temp file -- about 32,640 four-byte line slots -- before a line number can
            // reach 32,767.  Removing it would change nothing and claim something.
            count = (addr2 - zero) & 077777;
            putd();
            putchr('\n');
            continue;

        case '!':
            callunix();
            continue;

        case EOF:
            return;
        }
        error(Q);
    }
}

static int *address(void)
{
    int *a1;
    int minus, c;
    int n, relerr;

    minus = 0;
    a1    = 0;
    for (;;) {
        c = getchr();
        if ('0' <= c && c <= '9') {
            n = 0;
            do {
                n *= 10;
                n += c - '0';
            } while ((c = getchr()) >= '0' && c <= '9');
            peekc = c;
            if (a1 == 0)
                a1 = zero;
            if (minus < 0)
                n = -n;
            a1 += n;
            minus = 0;
            continue;
        }
        relerr = 0;
        if (a1 || minus)
            relerr++;
        switch (c) {
        case ' ':
        case '\t':
            continue;

        case '+':
            minus++;
            if (a1 == 0)
                a1 = dot;
            continue;

        case '-':
        case '^':
            minus--;
            if (a1 == 0)
                a1 = dot;
            continue;

        case '?':
        case '/':
            compile(c);
            a1 = dot;
            for (;;) {
                if (c == '/') {
                    a1++;
                    if (a1 > dol)
                        a1 = zero;
                } else {
                    a1--;
                    if (a1 < zero)
                        a1 = dol;
                }
                if (execute(0, a1))
                    break;
                if (a1 == dot)
                    error(Q);
            }
            break;

        case '$':
            a1 = dol;
            break;

        case '.':
            a1 = dot;
            break;

        case '\'':
            if ((c = getchr()) < 'a' || c > 'z')
                error(Q);
            for (a1 = zero; a1 <= dol; a1++)
                if (names[c - 'a'] == (*a1 & ~01))
                    break;
            break;

        default:
            peekc = c;
            if (a1 == 0)
                return 0;
            a1 += minus;
            if (a1 < zero || a1 > dol)
                error(Q);
            return a1;
        }
        if (relerr)
            error(Q);
    }
}

static void setdot(void)
{
    if (addr2 == 0)
        addr1 = addr2 = dot;
    if (addr1 > addr2)
        error(Q);
}

static void setall(void)
{
    if (addr2 == 0) {
        addr1 = zero + 1;
        addr2 = dol;
        if (dol == zero)
            addr1 = zero;
    }
    setdot();
}

static void setnoaddr(void)
{
    if (addr2)
        error(Q);
}

static void nonzero(void)
{
    if (addr1 <= zero || addr2 > dol)
        error(Q);
}

static void newline(void)
{
    int c;

    if ((c = getchr()) == '\n')
        return;
    if (c == 'p' || c == 'l') {
        pflag++;
        if (c == 'l')
            listf++;
        if (getchr() == '\n')
            return;
    }
    error(Q);
}

static void filename(int comm)
{
    int c, n;

    count = 0;
    c     = getchr();
    if (c == '\n' || c == EOF) {
        if (savedfile[0] == 0 && comm != 'f')
            error(Q);
        strcpy(file, savedfile);
        return;
    }
    if (c != ' ')
        error(Q);
    while ((c = getchr()) == ' ')
        ;
    if (c == '\n')
        error(Q);
    n = 0;
    do {
        // The bound v7 had not: it filled file[FNSIZE] from the command line with no test
        // at all, so a long name wrote past the array (../README.md SS6).
        if (n >= FNSIZE - 1)
            error(Q);
        file[n++] = c;
        if (c == ' ' || c == EOF)
            error(Q);
    } while ((c = getchr()) != '\n');
    file[n] = 0;
    if (savedfile[0] == 0 || comm == 'e' || comm == 'f')
        strcpy(savedfile, file);
}

static void exfile(void)
{
    close(io);
    io = -1;
    if (vflag) {
        putd();
        putchr('\n');
    }
}

static void onintr(int sig)
{
    signal(SIGINT, onintr);
    putchr('\n');
    lastc = '\n';
    error(Q);
}

static void onhup(int sig)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    if (dol > zero) {
        addr1 = zero + 1;
        addr2 = dol;
        io    = creat("ed.hup", 0666);
        if (io > 0)
            putfile();
    }
    fchange = 0;
    quit(0);
}

static void error(char *s)
{
    int c;

    wrapp = 0;
    listf = 0;
    putchr('?');
    putstr(s);
    count = 0;
    lseek(0, (off_t)0, SEEK_END);
    pflag = 0;
    if (globp)
        lastc = '\n';
    globp = 0;
    peekc = lastc;
    if (lastc)
        while ((c = getchr()) != '\n' && c != EOF)
            ;
    if (io > 0) {
        close(io);
        io = -1;
    }
    longjmp(savej, 1);
}

static int getchr(void)
{
    char c;

    if ((lastc = peekc)) {
        peekc = 0;
        return lastc;
    }
    if (globp) {
        if ((lastc = *globp++) != 0)
            return lastc;
        globp = 0;
        return EOF;
    }
    if (read(0, &c, 1) <= 0)
        return lastc = EOF;
    // v7: `lastc = c & 0177'.  A char is unsigned here, so this is 0..255 and EOF's -1 is
    // still a value no byte can be (SS11).
    lastc = c;
    return lastc;
}

static int gettty(void)
{
    int c;
    char *gf;
    int n;

    n  = 0;
    gf = globp;
    while ((c = getchr()) != '\n') {
        if (c == EOF) {
            if (gf)
                peekc = c;
            return c;
        }
        if (c == 0) // v7 masked to 0177 first; a NUL is still not storable
            continue;
        linebuf[n++] = c;
        if (n >= LBSIZE - 2)
            error(Q);
    }
    linebuf[n++] = 0;
    if (linebuf[0] == '.' && linebuf[1] == 0)
        return EOF;
    return 0;
}

static int getfile(void)
{
    int c;
    char *fp;
    int n;

    n  = 0;
    fp = nextip;
    do {
        if (--ninbuf < 0) {
            if ((ninbuf = read(io, genbuf, LBSIZE) - 1) < 0)
                return EOF;
            fp = genbuf;
        }
        c = *fp++;
        if (c == '\0')
            continue;
        // v7 read this as `if (c & 0200 || lp >= &linebuf[LBSIZE])' -- so a byte above 0177
        // was an ERROR and no file this machine's cat can print could be edited at all.
        // The bound is all that was ever wanted here.
        if (n >= LBSIZE) {
            lastc = '\n';
            error(Q);
        }
        linebuf[n++] = c;
        count++;
    } while (c != '\n');
    linebuf[n - 1] = 0;
    nextip         = fp;
    return 0;
}

static void putfile(void)
{
    int *a1;
    int n, nib;
    char *lp;

    nib = 512;
    n   = 0;
    a1  = addr1;
    do {
        lp = getlin(*a1++);
        for (;;) {
            if (--nib < 0) {
                if (write(io, genbuf, n) != n) {
                    putstr(WRERR);
                    error(Q);
                }
                nib = 511;
                n   = 0;
            }
            count++;
            if ((genbuf[n++] = *lp++) == 0) {
                genbuf[n - 1] = '\n';
                break;
            }
        }
    } while (a1 <= addr2);
    if (write(io, genbuf, n) != n) {
        putstr(WRERR);
        error(Q);
    }
}

static int append(int (*f)(void), int *a)
{
    int *a1, *a2, *rdot;
    int nline, tl;

    nline = 0;
    dot   = a;
    while ((*f)() == 0) {
        if ((dol - zero) + 1 >= nlall) {
            // v7 wrote free(zero) and THEN realloc(zero, ...), which this libc still
            // honours (lib/libc/gen/malloc.c reallocates a block freed since the last
            // malloc) but which C11 does not, and which depends on nothing calling malloc
            // in between.  Two bugs came with it: nlall was bumped before the attempt, so a
            // failure left it inflated; and the failure path restored `zero = ozero' after
            // the block had already been freed.  A plain realloc has neither.
            int *ozero = zero;
            int *nzero = (int *)realloc((char *)zero, (nlall + 512) * sizeof(int));
            if (nzero == NULL) {
                lastc = '\n';
                error("MEM?");
            }
            nlall += 512;
            zero = nzero;
            dot += zero - ozero;
            dol += zero - ozero;
        }
        tl = putline();
        nline++;
        a1   = ++dol;
        a2   = a1 + 1;
        rdot = ++dot;
        while (a1 > rdot)
            *--a2 = *--a1;
        *rdot = tl;
    }
    return nline;
}

static void callunix(void)
{
    void (*savint)(int);
    int pid, rpid;
    int retcode;

    setnoaddr();
    if ((pid = fork()) == 0) {
        signal(SIGHUP, oldhup);
        signal(SIGQUIT, oldquit);
        // v7 passed a bare 0 as the variadic terminator; a pointer here is not an int, and
        // lib/libc/README.md records the same hazard for execle().
        execl("/bin/sh", "sh", "-t", (char *)0);
        exit(0100);
    }
    savint = signal(SIGINT, SIG_IGN);
    while ((rpid = wait(&retcode)) != pid && rpid != -1)
        ;
    signal(SIGINT, savint);
    putstr("!");
}

static void quit(int sig)
{
    if (vflag && fchange && dol != zero) {
        fchange = 0;
        error(Q);
    }
    unlink(tfname);
    exit(0);
}

static void delete(void)
{
    setdot();
    newline();
    nonzero();
    rdelete(addr1, addr2);
}

static void rdelete(int *ad1, int *ad2)
{
    int *a1, *a2, *a3;

    a1 = ad1;
    a2 = ad2 + 1;
    a3 = dol;
    dol -= a2 - a1;
    do {
        *a1++ = *a2++;
    } while (a2 <= a3);
    a1 = ad1;
    if (a1 > dol)
        a1 = dol;
    dot     = a1;
    fchange = 1;
}

static void gdelete(void)
{
    int *a1, *a2, *a3;

    a3 = dol;
    for (a1 = zero + 1; (*a1 & 01) == 0; a1++)
        if (a1 >= a3)
            return;
    for (a2 = a1 + 1; a2 <= a3;) {
        if (*a2 & 01) {
            a2++;
            dot = a1;
        } else
            *a1++ = *a2++;
    }
    dol = a1 - 1;
    if (dot > dol)
        dot = dol;
    fchange = 1;
}

static char *getlin(int tl)
{
    char *bp, *lp;
    int nl;

    lp = linebuf;
    bp = getblock(tl, READ);
    nl = nleft;
    tl &= ~0377;
    while ((*lp++ = *bp++))
        if (--nl == 0) {
            bp = getblock(tl += 0400, READ);
            nl = nleft;
        }
    return linebuf;
}

static int putline(void)
{
    char *bp, *lp;
    int nl;
    int tl;

    fchange = 1;
    lp      = linebuf;
    tl      = tline;
    bp      = getblock(tl, WRITE);
    nl      = nleft;
    tl &= ~0377;
    while ((*bp = *lp++)) {
        if (*bp++ == '\n') {
            *--bp  = 0;
            linebp = lp;
            break;
        }
        if (--nl == 0) {
            bp = getblock(tl += 0400, WRITE);
            nl = nleft;
        }
    }
    nl = tline;
    // The one place a char * difference feeds a line token, and it must stay exactly as it
    // is: subtraction is the operation that works on this machine, and the `& 077776'
    // rounds the line to a four-byte slot so that bit 0 of the token stays free for the
    // global-command mark.
    tline += (((lp - linebuf) + 03) >> 1) & 077776;
    return nl;
}

static char *getblock(int atl, int iof)
{
    int bno, off;

    bno = (atl >> 8) & 0377;
    off = (atl << 1) & 0774;
    if (bno >= 255) {
        lastc = '\n';
        error(T);
    }
    nleft = 512 - off;
    if (bno == iblock) {
        ichanged |= iof;
        return ibuff + off;
    }
    if (bno == oblock)
        return obuff + off;
    if (iof == READ) {
        if (ichanged)
            blkio(iblock, ibuff, WRITE);
        ichanged = 0;
        iblock   = bno;
        blkio(bno, ibuff, READ);
        return ibuff + off;
    }
    if (oblock >= 0)
        blkio(oblock, obuff, WRITE);
    oblock = bno;
    return obuff + off;
}

// v7 passed `read' and `write' themselves, declared old-style as `extern read(), write();'
// so that one untyped function-pointer parameter could hold either.  Their real prototypes
// differ in the constness of the buffer, so no single pointer type fits both; the caller
// already had a READ/WRITE flag to hand, so the flag is what comes down here.
static void blkio(int b, char *buf, int iofcn)
{
    ssize_t n;

    lseek(tfile, (off_t)b << 9, SEEK_SET);
    n = (iofcn == READ) ? read(tfile, buf, 512) : write(tfile, buf, 512);
    if (n != 512)
        error(T);
}

static void init(void)
{
    int *markp;

    close(tfile);
    tline = 2;
    // A thin int * walk, and left as v7 wrote it: `markp < &names[26]' orders correctly
    // because names is an int array.  See item 2 in the header.
    for (markp = names; markp < &names[26];)
        *markp++ = 0;
    subnewa  = 0;
    anymarks = 0;
    iblock   = -1;
    oblock   = -1;
    ichanged = 0;
    close(creat(tfname, 0600));
    tfile = open(tfname, O_RDWR);
    dot = dol = zero;
}

static void global(int k)
{
    int c;
    int *a1;
    char globuf[GBSIZE];
    int n;

    if (globp)
        error(Q);
    setall();
    nonzero();
    if ((c = getchr()) == '\n')
        error(Q);
    compile(c);
    n = 0;
    while ((c = getchr()) != '\n') {
        if (c == EOF)
            error(Q);
        if (c == '\\') {
            c = getchr();
            if (c != '\n')
                globuf[n++] = '\\';
        }
        globuf[n++] = c;
        if (n >= GBSIZE - 2)
            error(Q);
    }
    globuf[n++] = '\n';
    globuf[n++] = 0;
    for (a1 = zero; a1 <= dol; a1++) {
        *a1 &= ~01;
        if (a1 >= addr1 && a1 <= addr2 && execute(0, a1) == k)
            *a1 |= 01;
    }
    //
    // Special case: g/.../d (avoid n^2 algorithm)
    //
    if (globuf[0] == 'd' && globuf[1] == '\n' && globuf[2] == '\0') {
        gdelete();
        return;
    }
    for (a1 = zero; a1 <= dol; a1++) {
        if (*a1 & 01) {
            *a1 &= ~01;
            dot   = a1;
            globp = globuf;
            commands();
            a1 = zero;
        }
    }
}

static void join(void)
{
    char *lp, *gp;
    int *a1;
    int n;

    n = 0;
    for (a1 = addr1; a1 <= addr2; a1++) {
        lp = getlin(*a1);
        while ((genbuf[n] = *lp++))
            if (n++ >= LBSIZE - 2)
                error(Q);
    }
    lp = linebuf;
    gp = genbuf;
    while ((*lp++ = *gp++))
        ;
    *addr1 = putline();
    if (addr1 < addr2)
        rdelete(addr1 + 1, addr2);
    dot = addr1;
}

static void substitute(int inglob)
{
    int *markp, *a1;
    int nl;
    int gsubf;

    gsubf = compsub();
    for (a1 = addr1; a1 <= addr2; a1++) {
        int *ozero;
        if (execute(0, a1) == 0)
            continue;
        inglob |= 01;
        dosub();
        if (gsubf) {
            while (*loc2) {
                if (execute(1, (int *)0) == 0)
                    break;
                dosub();
            }
        }
        subnewa = putline();
        *a1 &= ~01;
        if (anymarks) {
            for (markp = names; markp < &names[26]; markp++)
                if (*markp == *a1)
                    *markp = subnewa;
        }
        subolda = *a1;
        *a1     = subnewa;
        ozero   = zero;
        nl      = append(getsub, a1);
        nl += zero - ozero;
        a1 += nl;
        addr2 += nl;
    }
    if (inglob == 0)
        error(Q);
}

// Read the replacement text of an `s' command into rhsbuf, marking the bytes that arrived
// backslash-escaped.  See QESC at the head of the file: the mark is a prefix byte and no
// longer bit 0200 of the byte itself.
//
// The `esc' flag is what carries v7's control flow across the change.  There, an escaped
// byte could not compare equal to '\n' or to the delimiter because the 0200 bit had already
// made it unequal -- which is how `s/a/b\<newline>c/' inserts a newline and how
// `s/x/\//' inserts the delimiter.  Testing `!esc' asks the same question.
static int compsub(void)
{
    int seof, c, esc;
    int n;

    if ((seof = getchr()) == '\n' || seof == ' ')
        error(Q);
    compile(seof);
    n = 0;
    for (;;) {
        esc = 0;
        c   = getchr();
        if (c == '\\') {
            c   = getchr();
            esc = 1;
        }
        if (!esc && c == '\n') {
            if (globp)
                esc = 1;
            else
                error(Q);
        }
        if (!esc && c == seof)
            break;
        // An unterminated replacement.  v7 stored the EOF as a byte and went round again,
        // filling rhsbuf until the bound below caught it -- the same `?', reached the long
        // way, and with a bare 0377 left in the buffer on the way there.
        if (c == EOF)
            error(Q);
        if (esc || c == QESC) {
            if (n >= RHSIZE - 1)
                error(Q);
            rhsbuf[n++] = QESC;
        }
        rhsbuf[n++] = c;
        if (n >= RHSIZE)
            error(Q);
    }
    rhsbuf[n] = 0;
    if ((peekc = getchr()) == 'g') {
        peekc = 0;
        newline();
        return 1;
    }
    newline();
    return 0;
}

static int getsub(void)
{
    char *p1, *p2;

    p1 = linebuf;
    if ((p2 = linebp) == 0)
        return EOF;
    while ((*p1++ = *p2++))
        ;
    linebp = 0;
    return 0;
}

static void dosub(void)
{
    char *lp, *rp, *gp;
    int sn;
    int c, esc;

    lp = linebuf;
    rp = rhsbuf;
    sn = 0;
    // The head of the line, up to the match.  Two independent cursors, and the one place in
    // this file where getting the ordering wrong would have corrupted every `s' silently.
    while (lp - loc1 < 0)
        genbuf[sn++] = *lp++;
    while ((c = *rp++) != 0) {
        esc = 0;
        if (c == QESC) {
            c   = *rp++;
            esc = 1;
        }
        if (!esc && c == '&') {
            sn = place(sn, loc1, loc2);
            continue;
        }
        if (esc && c >= '1' && c < nbra + '1') {
            sn = place(sn, braslist[c - '1'], braelist[c - '1']);
            continue;
        }
        genbuf[sn++] = c;
        if (sn >= LBSIZE)
            error(Q);
    }
    lp   = loc2;
    loc2 = linebuf + sn;
    while ((genbuf[sn++] = *lp++))
        if (sn >= LBSIZE)
            error(Q);
    lp = linebuf;
    gp = genbuf;
    while ((*lp++ = *gp++))
        ;
}

// Copy linebuf[l1 .. l2) into genbuf at sn, and return the new fill.  v7 took and returned
// the genbuf cursor itself; an index is what can be bounded.
static int place(int sn, char *l1, char *l2)
{
    while (l2 - l1 > 0) {
        genbuf[sn++] = *l1++;
        if (sn >= LBSIZE)
            error(Q);
    }
    return sn;
}

static void move(int cflag)
{
    int *adt, *ad1, *ad2;

    setdot();
    nonzero();
    if ((adt = address()) == 0)
        error(Q);
    newline();
    if (cflag) {
        int *ozero, delta;
        ad1   = dol;
        ozero = zero;
        append(getcopy, ad1++);
        ad2   = dol;
        delta = zero - ozero;
        ad1 += delta;
        adt += delta;
    } else {
        ad2 = addr2;
        for (ad1 = addr1; ad1 <= ad2;)
            *ad1++ &= ~01;
        ad1 = addr1;
    }
    ad2++;
    if (adt < ad1) {
        dot = adt + (ad2 - ad1);
        if ((++adt) == ad1)
            return;
        reverse(adt, ad1);
        reverse(ad1, ad2);
        reverse(adt, ad2);
    } else if (adt >= ad2) {
        dot = adt++;
        reverse(ad1, ad2);
        reverse(ad2, adt);
        reverse(ad1, adt);
    } else
        error(Q);
    fchange = 1;
}

static void reverse(int *a1, int *a2)
{
    int t;

    for (;;) {
        t = *--a2;
        if (a2 <= a1)
            return;
        *a2   = *a1;
        *a1++ = t;
    }
}

static int getcopy(void)
{
    if (addr1 > addr2)
        return EOF;
    getlin(*addr1++);
    return 0;
}

// Compile a regular expression into expbuf.  The cursor is an INDEX rather than the char *
// v7 walked, because every bound in here is a relational test against the end of the
// buffer, and there were five of them.
static void compile(int aeof)
{
    int eof, c;
    int ei;     // fill index into expbuf
    int lastei; // index of the last single item, or -1 for none
    char bracket[NBRA];
    int bi; // fill index into bracket
    int cclcnt;

    ei  = 0;
    eof = aeof;
    bi  = 0;
    if ((c = getchr()) == eof) {
        if (expbuf[0] == 0)
            error(Q);
        return;
    }
    circfl = 0;
    nbra   = 0;
    if (c == '^') {
        c = getchr();
        circfl++;
    }
    peekc  = c;
    lastei = -1;
    for (;;) {
        if (ei >= ESIZE)
            goto cerror;
        c = getchr();
        if (c == eof) {
            if (bi != 0)
                goto cerror;
            expbuf[ei++] = CEOF;
            return;
        }
        if (c != '*')
            lastei = ei;
        switch (c) {
        case '\\':
            if ((c = getchr()) == '(') {
                if (nbra >= NBRA)
                    goto cerror;
                bracket[bi++] = nbra;
                expbuf[ei++]  = CBRA;
                expbuf[ei++]  = nbra++;
                continue;
            }
            if (c == ')') {
                if (bi <= 0)
                    goto cerror;
                expbuf[ei++] = CKET;
                expbuf[ei++] = bracket[--bi];
                continue;
            }
            if (c >= '1' && c < '1' + NBRA) {
                expbuf[ei++] = CBACK;
                expbuf[ei++] = c - '1';
                continue;
            }
            expbuf[ei++] = CCHR;
            if (c == '\n')
                goto cerror;
            expbuf[ei++] = c;
            continue;

        case '.':
            expbuf[ei++] = CDOT;
            continue;

        case '\n':
            goto cerror;

        case '*':
            if (lastei < 0 || expbuf[lastei] == CBRA || expbuf[lastei] == CKET)
                goto defchar;
            expbuf[lastei] |= STAR;
            continue;

        case '$':
            if ((peekc = getchr()) != eof)
                goto defchar;
            expbuf[ei++] = CDOL;
            continue;

        case '[':
            expbuf[ei++] = CCL;
            expbuf[ei++] = 0;
            cclcnt       = 1;
            if ((c = getchr()) == '^') {
                c              = getchr();
                expbuf[ei - 2] = NCCL;
            }
            do {
                if (c == '\n')
                    goto cerror;
                // A range.  Both ends are plain chars, which are UNSIGNED here, so this
                // orders correctly for a byte above 0177 where the PDP-11's signed char
                // would not have.  It is a range of BYTES, which is the pattern language's
                // unit -- see item 4 in the header, and ed.1.
                if (c == '-' && expbuf[ei - 1] != 0) {
                    if ((c = getchr()) == ']') {
                        expbuf[ei++] = '-';
                        cclcnt++;
                        break;
                    }
                    while (expbuf[ei - 1] < c) {
                        expbuf[ei] = expbuf[ei - 1] + 1;
                        ei++;
                        cclcnt++;
                        if (ei >= ESIZE || cclcnt > CCLMAX)
                            goto cerror;
                    }
                }
                expbuf[ei++] = c;
                cclcnt++;
                if (ei >= ESIZE || cclcnt > CCLMAX)
                    goto cerror;
            } while ((c = getchr()) != ']');
            expbuf[lastei + 1] = cclcnt;
            continue;

        defchar:
        default:
            expbuf[ei++] = CCHR;
            expbuf[ei++] = c;
        }
    }
cerror:
    expbuf[0] = 0;
    nbra      = 0;
    error(Q);
}

static int execute(int gf, int *addr)
{
    char *p1, *p2;
    int i, c;

    for (i = 0; i < NBRA; i++) {
        braslist[i] = 0;
        braelist[i] = 0;
    }
    if (gf) {
        if (circfl)
            return 0;
        p1 = linebuf;
        p2 = genbuf;
        while ((*p1++ = *p2++))
            ;
        locs = p1 = loc2;
    } else {
        if (addr == zero)
            return 0;
        p1   = getlin(*addr);
        locs = 0;
    }
    p2 = expbuf;
    if (circfl) {
        loc1 = p1;
        return advance(p1, p2);
    }
    // fast check for first character
    if (*p2 == CCHR) {
        c = p2[1];
        do {
            if (*p1 != c)
                continue;
            if (advance(p1, p2)) {
                loc1 = p1;
                return 1;
            }
        } while (*p1++);
        return 0;
    }
    // regular algorithm
    do {
        if (advance(p1, p2)) {
            loc1 = p1;
            return 1;
        }
    } while (*p1++);
    return 0;
}

static int advance(char *lp, char *ep)
{
    char *curlp;
    int i;

    for (;;)
        switch (*ep++) {
        case CCHR:
            if (*ep++ == *lp++)
                continue;
            return 0;

        case CDOT:
            if (*lp++)
                continue;
            return 0;

        case CDOL:
            if (*lp == 0)
                continue;
            return 0;

        case CEOF:
            loc2 = lp;
            return 1;

        case CCL:
            if (cclass(ep, *lp++, 1)) {
                ep += *ep;
                continue;
            }
            return 0;

        case NCCL:
            if (cclass(ep, *lp++, 0)) {
                ep += *ep;
                continue;
            }
            return 0;

        case CBRA:
            braslist[(int)*ep++] = lp;
            continue;

        case CKET:
            braelist[(int)*ep++] = lp;
            continue;

        case CBACK:
            if (braelist[i = *ep++] == 0)
                error(Q);
            if (backref(i, lp)) {
                lp += braelist[i] - braslist[i];
                continue;
            }
            return 0;

        case CBACK | STAR:
            if (braelist[i = *ep++] == 0)
                error(Q);
            curlp = lp;
            while (backref(i, lp))
                lp += braelist[i] - braslist[i];
            while (lp - curlp >= 0) {
                if (advance(lp, ep))
                    return 1;
                lp -= braelist[i] - braslist[i];
            }
            continue;

        case CDOT | STAR:
            curlp = lp;
            while (*lp++)
                ;
            goto star;

        case CCHR | STAR:
            curlp = lp;
            while (*lp++ == *ep)
                ;
            ep++;
            goto star;

        case CCL | STAR:
        case NCCL | STAR:
            curlp = lp;
            while (cclass(ep, *lp++, ep[-1] == (CCL | STAR)))
                ;
            ep += *ep;
            goto star;

        star:
            // The closure's backtrack bound, and the sharpest of the nineteen: `lp > curlp'
            // between two fat pointers walks the matcher off the end of the line buffer.
            do {
                lp--;
                if (lp == locs)
                    break;
                if (advance(lp, ep))
                    return 1;
            } while (lp - curlp > 0);
            return 0;

        default:
            error(Q);
        }
}

static int backref(int i, char *lp)
{
    char *bp;

    bp = braslist[i];
    while (*bp++ == *lp++)
        if (bp - braelist[i] >= 0)
            return 1;
    return 0;
}

// Is c in the class at `set'?  The class is a length byte followed by its members laid out
// LITERALLY -- not the CCL bitmap that grep and sed use, which is what ../TODO.md's brief
// for this task described.  So it was byte-capable already: the only thing that had ever
// kept a byte above 0177 out of a class was getchr() masking the pattern on its way in.
static int cclass(char *set, int c, int af)
{
    int n;

    if (c == 0)
        return 0;
    n = *set++;
    while (--n)
        if (*set++ == c)
            return af;
    return !af;
}

static void putd(void)
{
    int r;

    r = count % 10;
    count /= 10;
    if (count)
        putd();
    putchr(r + '0');
}

static void putstr(char *sp)
{
    col = 0;
    while (*sp)
        putchr(*sp++);
    putchr('\n');
}

static char line[70];
static int linp; // the fill count: a char * cursor here could not be bounded (SS2)

static void putchr(int ac)
{
    int n, c;

    n = linp;
    c = ac;
    if (listf) {
        col++;
        if (col >= 72) {
            col       = 0;
            line[n++] = '\\';
            line[n++] = '\n';
        }
        if (c == '\t') {
            c = '>';
            goto esc;
        }
        if (c == '\b') {
            c = '<';
        esc:
            line[n++] = '-';
            line[n++] = '\b';
            line[n++] = c;
            goto out;
        }
        // A byte above 0177 is not a control character here, because a plain char is
        // unsigned (SS11) -- so `l' passes UTF-8 through rather than escaping each of its
        // bytes in octal, which is what a console that speaks UTF-8 wants.  On the PDP-11
        // the same line printed `\3', the octal of a negative number.
        //
        // 0177 is the exception, and fixing it is the one BUG ed.1 itself owned up to: DEL
        // is a control character that is not `< ' '', so `l' printed it raw and garbled the
        // display it exists to make legible.  Escaping it costs eight-bit transparency
        // nothing -- 0177 is ASCII, and no byte of a UTF-8 sequence can be it.
        //
        // THE ESCAPE IS THREE OCTAL DIGITS NOW, where v7's was two, and that is the whole of
        // why DEL was mishandled rather than merely unescaped: two digits cannot spell 0177,
        // and v7's `(c >> 3) + '0'' on 127 gives 15 + '0' = `?', so the honest fix prints
        // `\177' and not `\?7'.  ed.1 promised two digits and an "unambiguous" listing, which
        // could not both be true once anything above 037 reached here; the page is corrected.
        if (c == 0177 || (c < ' ' && c != '\n')) {
            line[n++] = '\\';
            line[n++] = ((c >> 6) & 07) + '0';
            line[n++] = ((c >> 3) & 07) + '0';
            line[n++] = (c & 07) + '0';
            col += 3;
            goto out;
        }
    }
    line[n++] = c;
out:
    if (c == '\n' || n >= 64) {
        linp = 0;
        write(1, line, n);
        return;
    }
    linp = n;
}
