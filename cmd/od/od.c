/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// od -- octal (also hex, decimal, and character) dump.
//
//      od [ -bcdhowx ] [ file ] [ [ + ] offset[.][b] ]
//
// One of task C5b's seven (../README.md), and the one that had to be DESIGNED rather than
// ported: od's whole job is printing machine words, and a machine word here is 48 bits where
// v7's was 16.  README.md beside it is the account; the short form is here.
//
// THE FLAGS DID NOT CHANGE MEANING.  THE MACHINE WORD DID.  -o, -d and -x meant `the machine
// word, in this radix' on a PDP-11 and mean exactly that here, so what moves is only how wide
// the field has to be:
//
//      flag        v7 (16 bits)        BESM-6 (48 bits)
//      -o -w       6 octal digits      16 octal digits
//      -d          5 decimal           15 decimal
//      -x -h       4 hex               12 hex
//      -b          3 octal per byte    unchanged
//      -c          character           unchanged
//
// -w IS A NEW NAME FOR -o AND NOT A NEW FORMAT, which is what ../README.md asked for when it
// said this machine wants `a -w word dump in 16 octal digits, beside the byte formats'.  It
// is a synonym because the alternative -- making -o mean something byte-sized and -w the word
// -- would have been the one change that really did redefine a v7 flag.  The default stays
// -o, and it has a better reason here than v7 had: octal words are what b6as, b6ld and
// b6disasm speak, so od now agrees with the rest of the toolchain about what a word looks
// like.
//
// A LINE IS 12 BYTES, TWO WORDS, where v7's was 16 bytes and eight words.  16 is not a whole
// number of 6-byte words and a word view of a 16-byte line would have had to split one.
//
// THE BYTES PACK MOST SIGNIFICANT FIRST, which is not a free choice: it is how six chars
// occupy a word on this machine (doc/Besm6_Data_Representation.md) and what
// lib/libc/stdio/getw.c and putw.c already do.  So `od -c' and `od -w' of the same file agree
// about which byte is which, and a file written by putw() reads back as its own words.  A
// short final group is padded with zero bytes on the RIGHT, which is where the unused chars
// of a partial word are.
//
// EVERYTHING v7 BAKED THE PDP-11 WORD INTO, and there was more of it than the ten `long's:
//
//   * `unsigned short word[8]' is 8 WORDS here and sizeof it is 48, not 16, so the fread()
//     that filled it by sizeof and the loops that read it by item disagreed by six times.
//     The line buffer is bytes now and the word views assemble from it.
//   * `*(char *)&sn' and `*((char *)&sn + 1)' split an item into exactly TWO bytes in
//     PDP-11 order.  Both the count and the order are wrong here; the split is explicit now.
//   * putn() RECURSED EXACTLY AS DEEP AS THE FIELD WAS WIDE and discarded every digit past
//     it, so a value too big for its format came out silently truncated rather than wrong-
//     looking.  That is the trap this whole file is: a 48-bit word through v7's `putn(n,8,6)'
//     would have printed its low six octal digits and looked perfectly plausible.  It prints
//     AT LEAST the field width now and never fewer digits than the value needs.
//   * The address column was 7 octal digits -- 21 bits, about 2 MB, a PDP-11's file -- and it
//     truncated through the same putn().  It is 8 digits and cannot truncate.
//   * `for(c=1; c; c<<=1)' terminated after 16 iterations on a 16-bit int and would take 41
//     here.  Bounded by the highest format bit.
//
// THE `b' SUFFIX ON AN OFFSET IS BSIZE (§4), the same decision tail(1) took in this task and
// dd(1) before it: 512 named a PDP-11 disk block and names nothing on this machine.
//
// WHAT IS LEFT ALONE: v7's escape set for -c (\0 \b \f \n \r \t and octal for the rest, with
// no \a or \v -- v7 had none and adding them would change what a dump means); the radix of
// the ADDRESS column being set as a side effect of parsing the offset argument, which the
// manual page has never mentioned and which od.1.umm now does; and the `*' line that stands for
// a run of identical lines.
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#define WPL  2            // words per output line
#define LINE (WPL * NBPW) // ... and bytes, which is 12

// Field widths.  Each is exactly what the whole range of its type needs, so no value can
// overflow its column: 48 bits is 16 octal digits, 15 decimal and 12 hex, and a byte is 3
// octal digits.  A byte group is NBPW of those with a space between each pair.
#define W_OCT  16
#define W_DEC  15
#define W_HEX  12
#define W_BYTE (NBPW * 3 + NBPW - 1)

// The format bits, as v7 assigned them.  004 was v7's unused hole and stays unused: -w sets
// FMT_OCT, being a second spelling of it rather than a format of its own.
#define FMT_OCT  001
#define FMT_DEC  002
#define FMT_HEX  010
#define FMT_CHR  020
#define FMT_BYTE 040
#define FMT_MAX  040

static char buf[LINE];
static char lastbuf[LINE];
static int conv;
static int base = 010;
static int width;
static int addr;

static void dumpline(int a, const char *b, int nw);
static void putitem(const char *b, int i, int c);
static void cput(int c);
static void putn(unsigned n, int b, int c);
static void pre(int n);
static void offset(const char *s);

// The 48-bit word made of b[i*NBPW .. i*NBPW+NBPW-1], most significant byte first -- getw(3)'s
// order, and the order six chars occupy a word.  `unsigned' and not `int': a signed int is 41
// bits here (§3) and would lose the top seven.
static unsigned wordat(const char *b, int i)
{
    unsigned w;
    int k;

    w = 0;
    for (k = 0; k < NBPW; k++)
        w = (w << 8) | (unsigned)(b[i * NBPW + k] & 0377);
    return w;
}

int main(int argc, char **argv)
{
    const char *p;
    int i, n, f, same;

    argv++;
    f = 0;
    if (argc > 1) {
        p = *argv;
        if (*p == '-') {
            while (*p != '\0') {
                switch (*p++) {
                case 'o':
                case 'w': // the word in octal, under the name that says so
                    conv |= FMT_OCT;
                    f = W_OCT;
                    break;
                case 'd':
                    conv |= FMT_DEC;
                    f = W_DEC;
                    break;
                case 'x':
                case 'h':
                    conv |= FMT_HEX;
                    f = W_HEX;
                    break;
                case 'c':
                    conv |= FMT_CHR;
                    f = W_BYTE;
                    break;
                case 'b':
                    conv |= FMT_BYTE;
                    f = W_BYTE;
                    break;
                }
                if (f > width)
                    width = f;
            }
            argc--;
            argv++;
        }
    }
    if (!conv) {
        width = W_OCT;
        conv  = FMT_OCT;
    }
    if (argc > 1)
        if (**argv != '+') {
            if (freopen(*argv, "r", stdin) == NULL) {
                printf("cannot open %s\n", *argv);
                exit(1);
            }
            argv++;
            argc--;
        }
    if (argc > 1)
        offset(*argv);

    same = -1;
    for (;;) {
        // Zeroed before every read, so a short final group is padded on the right rather
        // than carrying the tail of the line before it.  v7 zeroed only on the path that
        // printed, which left the suppressed case able to pad with stale bytes.
        for (i = 0; i < LINE; i++)
            buf[i] = 0;
        n = fread(buf, 1, LINE, stdin);
        if (n <= 0)
            break;
        if (same >= 0) {
            for (i = 0; i < LINE; i++)
                if (lastbuf[i] != buf[i])
                    goto notsame;
            if (same == 0) {
                printf("*\n");
                same = 1;
            }
            addr += n;
            continue;
        }
    notsame:
        dumpline(addr, buf, (n + NBPW - 1) / NBPW);
        same = 0;
        for (i = 0; i < LINE; i++)
            lastbuf[i] = buf[i];
        addr += n;
    }
    putn((unsigned)addr, base, 8);
    putchar('\n');
    return 0;
}

// One input line, once per selected format: the address on the first, a tab on the rest.
static void dumpline(int a, const char *b, int nw)
{
    int i, c, first;

    first = 1;
    for (c = 1; c <= FMT_MAX; c <<= 1) {
        if ((c & conv) == 0)
            continue;
        if (first) {
            putn((unsigned)a, base, 8);
            putchar(' ');
            first = 0;
        } else
            putchar('\t');
        for (i = 0; i < nw; i++) {
            putitem(b, i, c);
            putchar(i == nw - 1 ? '\n' : ' ');
        }
    }
}

// One word of the line in format c -- as a number for the word formats, as its NBPW bytes for
// the byte formats.  The byte groups are padded to the same field, so a -b or -c column still
// lines up under the -o column above it, which is what v7's pre() was for.
static void putitem(const char *b, int i, int c)
{
    int k;

    switch (c) {
    case FMT_OCT:
        pre(W_OCT);
        putn(wordat(b, i), 8, W_OCT);
        break;
    case FMT_DEC:
        pre(W_DEC);
        putn(wordat(b, i), 10, W_DEC);
        break;
    case FMT_HEX:
        pre(W_HEX);
        putn(wordat(b, i), 16, W_HEX);
        break;
    case FMT_CHR:
        pre(W_BYTE);
        for (k = 0; k < NBPW; k++) {
            cput(b[i * NBPW + k]);
            if (k != NBPW - 1)
                putchar(' ');
        }
        break;
    case FMT_BYTE:
        pre(W_BYTE);
        for (k = 0; k < NBPW; k++) {
            putn((unsigned)(b[i * NBPW + k] & 0377), 8, 3);
            if (k != NBPW - 1)
                putchar(' ');
        }
        break;
    }
}

// One byte as a character, in three columns.  A byte above 0177 has no character here and
// falls to the octal default, which is what a dump wants -- od is the one filter in this task
// whose job is to show the bytes rather than to carry them.
static void cput(int c)
{
    c &= 0377;
    if (c > 037 && c < 0177) {
        printf("  ");
        putchar(c);
        return;
    }
    switch (c) {
    case '\0':
        printf(" \\0");
        break;
    case '\b':
        printf(" \\b");
        break;
    case '\f':
        printf(" \\f");
        break;
    case '\n':
        printf(" \\n");
        break;
    case '\r':
        printf(" \\r");
        break;
    case '\t':
        printf(" \\t");
        break;
    default:
        putn((unsigned)c, 8, 3);
    }
}

// n in radix b, at least c digits wide, zero filled -- and NEVER fewer digits than n needs.
// v7 recursed exactly c deep and dropped everything above, which is the silent truncation
// this file's header is about.
static void putn(unsigned n, int b, int c)
{
    int d;

    if (n >= (unsigned)b || c > 1)
        putn(n / (unsigned)b, b, c - 1);
    d = (int)(n % (unsigned)b);
    if (d > 9)
        putchar(d - 10 + 'a');
    else
        putchar(d + '0');
}

static void pre(int n)
{
    int i;

    for (i = n; i < width; i++)
        putchar(' ');
}

// The `+offset' argument, which also sets the radix of the ADDRESS column as a side effect:
// x or 0x makes it hex, a leading 0 octal, a `.' anywhere decimal.
static void offset(const char *s)
{
    const char *p;
    int a, d;

    if (*s == '+')
        s++;
    if (*s == 'x') {
        s++;
        base = 16;
    } else if (*s == '0' && s[1] == 'x') {
        s += 2;
        base = 16;
    } else if (*s == '0')
        base = 8;
    p = s;
    while (*p) {
        if (*p++ == '.')
            base = 10;
    }
    for (a = 0; *s; s++) {
        d = *s;
        if (d >= '0' && d <= '9')
            a = a * base + d - '0';
        else if (d >= 'a' && d <= 'f' && base == 16)
            a = a * base + d + 10 - 'a';
        else
            break;
    }
    if (*s == '.')
        s++;
    if (*s == 'b' || *s == 'B')
        a *= BSIZE; // one FILESYSTEM block, not the PDP-11's 512 (§4)
    fseek(stdin, a, 0);
    addr = a;
}
