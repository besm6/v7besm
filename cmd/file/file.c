/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// file -- determine the type of a file.
//
//      file [ -f listfile ] file ...
//
// One of task C5f's seven (../TODO.md), and the one the task brief says gets a DELIBERATE
// change rather than a faithful port.  ./README.md is the account; two of the changes are
// worth having at the head of the source.
//
// THE MAGIC NUMBERS ARE THIS MACHINE'S.  v7 switched on `*(int *)buf' over 0407, 0410, 0411,
// 0177555 and 0177545 -- a PDP-11 16-bit a_magic.  Here:
//
//   * a header field is one 48-bit WORD, six bytes, most significant first, and the magic
//     is the one field whose high half is not zero padding: FMAGIC is "BESM" 01 07 and
//     NMAGIC is "BESM" 01 08 (doc/File_Magic.md, cross/besm6/b.out.h).  Both are in
//     <sys/param.h>, which is where a guest program can reach them.
//   * an `int' here is 41 bits and the magic is 48, so the compare must be UNSIGNED -- the
//     same note <sys/user.h> carries on ux_mag.
//   * `*(int *)buf' would not have worked in any case: a cast from `char *' to `int *'
//     FLOORS the fat pointer to a word boundary (../README.md §2's third hazard).  The word
//     is assembled from bytes, as od(1)'s wordat() and getw(3) assemble theirs.
//   * 0411 (separate I&D) and 0177555 (the pre-v7 archive) name nothing here and are gone.
//   * "not stripped" reads a_syms, which is word 5 / byte 30 here and was word 4 there.
//   * AND RELFLG SAYS MORE THAN a_syms DOES.  a_flag's bit 0 is set by the linker on a fully
//     linked file (cmd/ld/output.c) and forced by strip(1), so FMAGIC with it is an
//     executable and FMAGIC without it is a relocatable object -- a distinction v7's file
//     could not draw at all.
//
// UTF-8 IS TEXT.  This is the sixth deliberate divergence, after touch, rev, col, grep -b
// and sort -d.  v7 declares any file with a byte of 0200 set to be "data", and appends
// " with garbage" to anything else that has one:
//
//      $ file motd
//      motd:   data
//
// -- for a file of perfectly ordinary Russian prose.  This machine's text is UTF-8 end to
// end (../README.md §11), so a faithful file(1) reports the image's own text as binary,
// which is col(1)'s and sort(1)'s failure exactly: plausible output that is quietly wrong.
// utf8ok() validates the high-bit runs instead, so a well-formed sequence is text and a
// malformed byte is still data.  A file that is valid UTF-8 but not all ASCII says "text"
// rather than "ascii text", which is the honest word for it.
//
// AND THE ASSEMBLER TABLES ARE THIS MACHINE'S TOO.  `asc[]' was {sys, mov, tst, clr, jmp}
// and `as[]' was {globl, byte, even, text, data, bss, comm}: PDP-11 mnemonics and PDP-11 as
// pseudo-ops.  They are b6as's now -- cmd/as/symtab.c's directives and cmd/as/tables.c's
// Madlen mnemonics -- and `.even' is gone, a word machine having nothing to align to.
// `troff output' went with them: ../TODO.md's exclusion table drops the whole typesetting
// suite, so nothing here produces it.
//
// THREE READS PAST THE BOUND.  type(), lookup(), ccom() and ascom() share one file-scope
// cursor `i' into buf, and about twenty `if (i >= in)' tests guard reads of buf[i+1] and
// buf[j+2].  A wild read of C5d's class, and cheap to fix.
//
// `if (buf[i] <= 0)' MEANT SOMETHING ELSE ON A PDP-11.  With a signed char it read "a high
// byte or a NUL -- give up on C"; here char is unsigned and it means `== 0' alone.  That is
// the behaviour this port wants, now that a high byte is text, and it is spelt out.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

// cross/besm6/ar.h's ARMAG.  The value is v7's; what changed is the width -- it is one
// 48-bit word now, so an archive begins 00 00 00 00 FF 65.  A guest program cannot include
// that header (it is the host cross tree), so the number is written here with its source.
#define ARMAG 0177545

#define HDRSZ  (BADDR * (int)sizeof(int)) // 8 words = 48 bytes; <sys/param.h> owns BADDR
#define RELFLG 1                          // a_flag bit 0: fully linked, no relocation left
#define O_SYMS 30                         // byte offset of a_syms
#define O_FLAG 42                         // byte offset of a_flag

#define NASC 128 // english()'s histogram: ASCII only, by construction

static int in;
static int i = 0;
static char buf[512];
static int ifile;

static const char *const fort[] = {
    "function", "subroutine", "common", "dimension", "block", "integer",
    "real",     "data",       "double", 0
};

// Madlen mnemonics, from cmd/as/tables.c: store, load, unconditional jump, and the two that
// set an index register.  v7 listed sys, mov, tst, clr and jmp.
static const char *const asc[] = { "atx", "xta", "uj", "vtm", "vjm", 0 };

static const char *const c[] = { "int", "char", "float", "double", "struct", "extern", 0 };

// b6as directives, from cmd/as/symtab.c.  v7 listed globl, byte, even, text, data, bss and
// comm; `byte' and `even' name nothing on a word machine.
static const char *const as[] = {
    "globl", "const", "text", "data", "bss", "comm", "half", "word", "equ", "ascii",
    "strng", "org",   0
};

static void type(const char *file);
static int lookup(const char *const *tab);
static int ccom(void);
static int ascom(void);
static int english(const char *bp, int n);
static int utf8ok(const char *b, int n);
static int allascii(const char *b, int n);
static unsigned wordat(const char *b);

int main(int argc, char **argv)
{
    FILE *fl;
    char *p;
    char ap[128];

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'f') {
        if ((fl = fopen(argv[2], "r")) == NULL) {
            printf("Can't open %s\n", argv[2]);
            exit(2);
        }
        while ((p = fgets(ap, sizeof(ap), fl)) != NULL) {
            int l = strlen(p);
            if (l > 0 && p[l - 1] == '\n')
                p[l - 1] = '\0';
            printf("%s:	", p);
            type(p);
            if (ifile >= 0)
                close(ifile);
        }
        fclose(fl);
        exit(0); // v7 exited 1 here, having done exactly what it was asked
    }
    while (argc > 1) {
        printf("%s:	", argv[1]);
        type(argv[1]);
        argc--;
        argv++;
        if (ifile >= 0)
            close(ifile);
    }
    return 0;
}

//
// Assemble one 48-bit word from six bytes, most significant first -- the order this
// machine's a.out is written in (cmd/libaout/fputw.c) and the order getw(3) reads in.  An
// `unsigned' is 48 bits here; an `int' is 41 and would truncate the magic.
//
static unsigned wordat(const char *b)
{
    unsigned w;
    int k;

    w = 0;
    for (k = 0; k < (int)sizeof(int); k++)
        w = (w << 8) | (unsigned char)b[k];
    return w;
}

static void type(const char *file)
{
    int j, nl;
    char ch;
    struct stat mbuf;
    unsigned magic;

    ifile = -1;
    if (stat(file, &mbuf) < 0) {
        printf("cannot stat\n");
        return;
    }
    switch (mbuf.st_mode & S_IFMT) {

    case S_IFCHR:
        printf("character");
        goto spcl;

    case S_IFDIR:
        printf("directory\n");
        return;

    case S_IFBLK:
        printf("block");

    spcl:
        printf(" special (%d/%d)\n", major(mbuf.st_rdev), minor(mbuf.st_rdev));
        return;
    }

    ifile = open(file, 0);
    if (ifile < 0) {
        printf("cannot open\n");
        return;
    }
    in = read(ifile, buf, sizeof(buf));
    if (in <= 0) {
        printf("empty\n");
        return;
    }

    // The a.out and archive magics.  See the head of this file: one 48-bit word, unsigned,
    // assembled from bytes rather than cast through an `int *'.
    if (in >= (int)sizeof(int)) {
        magic = wordat(buf);
        if (magic == FMAGIC || magic == NMAGIC) {
            if (magic == NMAGIC)
                printf("pure executable");
            else if (in >= HDRSZ && (wordat(buf + O_FLAG) & RELFLG) == 0)
                printf("relocatable object");
            else
                printf("executable");
            if (in >= HDRSZ && wordat(buf + O_SYMS) != 0)
                printf(" not stripped");
            printf("\n");
            goto out;
        }
        if (magic == ARMAG) {
            printf("archive\n");
            goto out;
        }
    }

    i = 0;
    if (ccom() == 0)
        goto notc;
    while (i < in && buf[i] == '#') {
        j = i;
        while (i < in && buf[i++] != '\n') {
            if (i - j > 255) {
                printf("data\n");
                goto out;
            }
        }
        if (i >= in)
            goto notc;
        if (ccom() == 0)
            goto notc;
    }
check:
    if (lookup(c) == 1) {
        while ((ch = buf[i++]) != ';' && ch != '{')
            if (i >= in)
                goto notc;
        printf("c program text");
        goto outa;
    }
    nl = 0;
    while (i < in && buf[i] != '(') {
        // v7 wrote `buf[i] <= 0', which with a SIGNED char meant "a high byte or a NUL".
        // char is unsigned here, and a high byte is text now, so this is the NUL alone.
        if (buf[i] == '\0')
            goto notas;
        if (buf[i] == ';') {
            i++;
            goto check;
        }
        if (buf[i++] == '\n')
            if (nl++ > 6)
                goto notc;
        if (i >= in)
            goto notc;
    }
    if (i >= in)
        goto notc;
    while (i < in && buf[i] != ')') {
        if (buf[i++] == '\n')
            if (nl++ > 6)
                goto notc;
        if (i >= in)
            goto notc;
    }
    while (i < in && buf[i] != '{') {
        if (buf[i++] == '\n')
            if (nl++ > 6)
                goto notc;
        if (i >= in)
            goto notc;
    }
    if (i >= in)
        goto notc;
    printf("c program text");
    goto outa;
notc:
    i = 0;
    while (i < in && (buf[i] == 'c' || buf[i] == '#')) {
        while (i < in && buf[i++] != '\n')
            ;
        if (i >= in)
            goto notfort;
    }
    if (lookup(fort) == 1) {
        printf("fortran program text");
        goto outa;
    }
notfort:
    i = 0;
    if (ascom() == 0)
        goto notas;
    j = i - 1;
    if (i < in && buf[i] == '.') {
        i++;
        if (lookup(as) == 1) {
            printf("assembler program text");
            goto outa;
        } else if (j >= 0 && buf[j] == '\n' && j + 2 < in && isascii(buf[j + 2]) &&
                   isalpha(buf[j + 2])) {
            printf("roff, nroff, or eqn input text");
            goto outa;
        }
    }
    while (lookup(asc) == 0) {
        if (ascom() == 0)
            goto notas;
        while (i < in && buf[i] != '\n' && buf[i++] != ':')
            ;
        if (i >= in)
            goto notas;
        while (i < in && (buf[i] == '\n' || buf[i] == ' ' || buf[i] == '\t'))
            i++;
        if (i >= in)
            goto notas;
        j = i - 1;
        if (buf[i] == '.') {
            i++;
            if (lookup(as) == 1) {
                printf("assembler program text");
                goto outa;
            } else if (j >= 0 && buf[j] == '\n' && j + 2 < in && isascii(buf[j + 2]) &&
                       isalpha(buf[j + 2])) {
                printf("roff, nroff, or eqn input text");
                goto outa;
            }
        }
    }
    printf("assembler program text");
    goto outa;
notas:
    // THE DIVERGENCE.  v7: `for (i=0; i<in; i++) if (buf[i] & 0200) ... printf("data\n")',
    // which makes every Cyrillic text file on this image binary.  See the head of this file.
    if (!utf8ok(buf, in)) {
        printf("data\n");
        goto out;
    }
    if (mbuf.st_mode & ((S_IEXEC) | (S_IEXEC >> 3) | (S_IEXEC >> 6)))
        printf("commands text");
    else if (english(buf, in))
        printf("English text");
    else if (allascii(buf, in))
        printf("ascii text");
    else
        printf("text"); // valid UTF-8 with something above 0177 in it
outa:
    // v7 rescanned the tail for a byte above 127 and called it " with garbage".  Same
    // divergence: what makes it garbage is being malformed, not being eighth-bit.
    if (!utf8ok(buf, in)) {
        printf(" with garbage\n");
        goto out;
    }
    printf("\n");
out:;
}

static int lookup(const char *const *tab)
{
    char r;
    int k, j, l;

    while (i < in && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n'))
        i++;
    for (j = 0; tab[j] != 0; j++) {
        l = 0;
        for (k = i; k < in && (r = tab[j][l++]) == buf[k] && r != '\0'; k++)
            ;
        if (k >= in)
            continue;
        r = tab[j][l - 1];
        if (r == '\0')
            if (buf[k] == ' ' || buf[k] == '\n' || buf[k] == '\t' || buf[k] == '{' ||
                buf[k] == '/') {
                i = k;
                return 1;
            }
    }
    return 0;
}

static int ccom(void)
{
    char cc;

    while (i < in && ((cc = buf[i]) == ' ' || cc == '\t' || cc == '\n'))
        i++;
    if (i >= in)
        return 0;
    if (i + 1 < in && buf[i] == '/' && buf[i + 1] == '*') {
        i += 2;
        while (i + 1 < in && (buf[i] != '*' || buf[i + 1] != '/')) {
            if (buf[i] == '\\')
                i += 2;
            else
                i++;
        }
        if ((i += 2) >= in)
            return 0;
    }
    if (buf[i] == '\n')
        if (ccom() == 0)
            return 0;
    return 1;
}

static int ascom(void)
{
    while (i < in && buf[i] == '/') {
        i++;
        while (i < in && buf[i++] != '\n')
            ;
        if (i >= in)
            return 0;
        while (i < in && buf[i] == '\n')
            i++;
        if (i >= in)
            return 0;
    }
    return i < in;
}

//
// Is b[0..n) well-formed UTF-8?  A sequence cut off by the end of the 512-byte window is
// accepted -- what is being decided is whether the bytes are TEXT, and half a letter at the
// end of a buffer is not evidence of anything.
//
static int utf8ok(const char *b, int n)
{
    int k, len;
    unsigned lead, c1;
    int p;

    for (p = 0; p < n;) {
        lead = (unsigned char)b[p];
        if (lead < 0200) {
            p++;
            continue;
        }
        if (lead >= 0302 && lead <= 0337)
            len = 2;
        else if (lead >= 0340 && lead <= 0357)
            len = 3;
        else if (lead >= 0360 && lead <= 0364)
            len = 4;
        else
            return 0; // 0200-0301 never lead, 0365-0377 never appear at all
        if (p + len > n)
            return 1; // cut by the window
        for (k = 1; k < len; k++)
            if (((unsigned char)b[p + k] & 0300) != 0200)
                return 0;
        c1 = (unsigned char)b[p + 1];
        if (len == 3 && lead == 0340 && c1 < 0240)
            return 0; // over-long
        if (len == 3 && lead == 0355 && c1 >= 0240)
            return 0; // a surrogate, D800-DFFF
        if (len == 4 && lead == 0360 && c1 < 0220)
            return 0; // over-long
        if (len == 4 && lead == 0364 && c1 >= 0220)
            return 0; // past U+10FFFF
        p += len;
    }
    return 1;
}

static int allascii(const char *b, int n)
{
    int p;

    for (p = 0; p < n; p++)
        if ((unsigned char)b[p] >= 0200)
            return 0;
    return 1;
}

static int english(const char *bp, int n)
{
    int ct[NASC], j, vow, freq, rare;
    int badpun = 0, punct = 0;

    if (n < 50)
        return 0; // no point in statistics on squibs
    for (j = 0; j < NASC; j++)
        ct[j] = 0;
    for (j = 0; j < n; j++) {
        // char is unsigned here, so a byte above 0177 fails this test and is simply not
        // counted.  On a PDP-11 it was NEGATIVE, passed the test, and wrote ct[] below zero.
        if ((unsigned char)bp[j] < NASC)
            ct[(unsigned char)bp[j] | 040]++;
        switch (bp[j]) {
        case '.':
        case ',':
        case ')':
        case '%':
        case ';':
        case ':':
        case '?':
            punct++;
            if (j < n - 1 && bp[j + 1] != ' ' && bp[j + 1] != '\n')
                badpun++;
        }
    }
    if (badpun * 5 > punct)
        return 0;
    vow  = ct['a'] + ct['e'] + ct['i'] + ct['o'] + ct['u'];
    freq = ct['e'] + ct['t'] + ct['a'] + ct['i'] + ct['o'] + ct['n'];
    rare = ct['v'] + ct['j'] + ct['k'] + ct['q'] + ct['x'] + ct['z'];
    if (2 * ct[';'] > ct['e'])
        return 0;
    if ((ct['>'] + ct['<'] + ct['/']) > ct['e'])
        return 0; // shell file test
    return vow * 5 >= n - ct[' '] && freq >= 10 * rare;
}
