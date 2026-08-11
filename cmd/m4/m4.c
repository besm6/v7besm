/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * m4 -- the macro processor.  Task C13; README.md beside this file is the account.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EOS     0
#define LPAR    '('
#define RPAR    ')'
#define COMMA   ','
#define GRAVE   '`'
#define ACUTE   '\''
#define COMMENT '#'
#define ALPH    1
#define DIG     2

#define HSHSIZ 199 /* prime */
#define STACKS 50
// Three argstk slots per call frame at least -- definition, name, first argument -- so
// v7's STACKS+10 made `call stack overflow' unreachable: the arg stack ran out first.
#define ARGS  (3 * STACKS + 10)
#define SAVS  4096
#define TOKS  128
#define NINCL 10 /* nested includes, and diversion streams */

char lquote = GRAVE;
char rquote = ACUTE;

//
// Character classification, indexed by getc(3)'s result.  256 entries, not v7's 128:
// getc() masks with 0377, so every byte above 0177 landed past the table.  Those bytes
// are ALPH -- a byte above 0177 is part of a multi-byte letter and never punctuation,
// so a macro name may be Russian.  README.md, "A table indexed by getc(3)'s result".
//
static const char type[256] = {
    // clang-format off
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    DIG,  DIG,  DIG,  DIG,  DIG,  DIG,  DIG,  DIG,
    DIG,  DIG,  0,    0,    0,    0,    0,    0,
    0,    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, 0,    0,    0,    0,    ALPH,
    0,    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, 0,    0,    0,    0,    0,
    // 0200-0377: part of a multi-byte letter, so never punctuation.
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH, ALPH,
    // clang-format on
};

//
// A builtin is an integer code in the name table.  v7 identified one by the ADDRESS of
// the one-byte block install() returned and kept twenty-one such pointers in globals;
// free() puts that block back and the next copy() takes it.  README.md.
//
enum {
    B_NONE = 0,
    B_DEFINE,
    B_UNDEFINE,
    B_IFDEF,
    B_CHANGEQUOTE,
    B_DIVERT,
    B_UNDIVERT,
    B_DIVNUM,
    B_DNL,
    B_IFELSE,
    B_INCR,
    B_EVAL,
    B_LEN,
    B_INDEX,
    B_SUBSTR,
    B_TRANSLIT,
    B_INCLUDE,
    B_SINCLUDE,
    B_SYSCMD,
    B_MAKETEMP,
    B_ERRPRINT,
    B_DUMPDEF,
    B_SHIFT
};

struct nlist {
    char *name;
    char *def;
    int bltin;
    struct nlist *next;
};

struct call {
    char **argp;
    int plev;
    int bltin;
};

static char token[TOKS];
static struct nlist *hshtab[HSHSIZ];
static char ibuf[SAVS + TOKS];
static char obuf[SAVS + TOKS];

// Two cursors, each with a shadow count so that no relational between two char * runs
// once per byte (../README.md SS2).  ip == ibuf + curx + nback, op == obuf + opx.
static char *op = obuf;
static char *ip = ibuf;
static int opx;
static int curx;             // pushback floor of the current input level
static int nback;            // characters pushed back and not yet re-read
static int nback_stk[NINCL]; // the outer levels' nback

static struct call *cp = NULL;

// At file scope, not in main's frame: 160 words off a 4,096-word stack (../README.md SS6).
static char *argstk[ARGS];
static struct call callst[STACKS];

// mktemp(3) writes its argument, and the tempname[7] stores below patch it again: an
// array, where v7 handed it a string literal.
static char tempname[] = "/tmp/m4aXXXXX";

static int hshval;
static FILE *olist[NINCL + 1] = { stdout };
static int okret;
static int curout          = 0;
static FILE *curfile       = stdout;
static FILE *infile[NINCL] = { stdin };
static int infptr          = 0;

static int nfiles; // command-line files not yet read
static char **filev;

// Shared with m4y.y: the value channel in and out of yyparse(), and the cursor it reads.
int evalval;
char *pe;
char *evalerr;
int yyparse(void);

static _Noreturn void delexit(void);
static _Noreturn void ovf(char *what);
static void catchsig(int sig);
static int nextfile(void);
static void puttok(void);
static void pbstr(char *str);
static void expand(char **a1, int c, int bltin);
static struct nlist *lookup(char *str);
static char *install(char *nam, char *val, int bltin);
static char *copy(char *s);
static void putnum(int num);
static int ctoi(char *str);
static int strindex(char *p1, char *p2);
static void undivert(int i);
static void dodef(char **ap, int c);
static void doifdef(char **ap, int c);
static void dolen(char **ap, int c);
static void docq(char **ap, int c);
static void doshift(char **ap, int c);
static void dodump(char **ap, int c);
static void doerrp(char **ap, int c);
static void doeval(char **ap, int c);
static void doincl(char **ap, int c, int noisy);
static void dosyscmd(char **ap, int c);
static void domake(char **ap, int c);
static void doincr(char **ap, int c);
static void dosubstr(char **ap, int c);
static void doindex(char **ap, int c);
static void dotransl(char **ap, int c);
static void doif(char **ap, int c);
static void dodiv(char **ap, int c);
static void doundiv(char **ap, int c);
static void dodivnum(char **ap, int c);
static void dodnl(char **ap, int c);
static void doundef(char **ap, int c);

#define getchr() (nback ? (nback--, *--ip) : getc(infile[infptr]))

#define putbak(c)                 \
    do {                          \
        if (curx + nback >= SAVS) \
            ovf("pushback");      \
        nback++;                  \
        *ip++ = (c);              \
    } while (0)

#define outc(c)              \
    do {                     \
        if (opx >= SAVS)     \
            ovf("argument"); \
        opx++;               \
        *op++ = (c);         \
    } while (0)

#define putchr(c)                   \
    do {                            \
        if (cp == NULL) {           \
            if (curfile)            \
                putc((c), curfile); \
        } else                      \
            outc(c);                \
    } while (0)

// Install the builtins.  Split out of main: a resident function's frame is permanent
// and every temporary in it costs a word (../README.md SS6).
static void instbltins(void)
{
    install("unix", "", B_NONE);

    install("define", "", B_DEFINE);
    install("undefine", "", B_UNDEFINE);
    install("ifdef", "", B_IFDEF);
    install("changequote", "", B_CHANGEQUOTE);
    install("divert", "", B_DIVERT);
    install("undivert", "", B_UNDIVERT);
    install("divnum", "", B_DIVNUM);
    install("dnl", "", B_DNL);
    install("ifelse", "", B_IFELSE);
    install("incr", "", B_INCR);
    install("eval", "", B_EVAL);
    install("len", "", B_LEN);
    install("index", "", B_INDEX);
    install("substr", "", B_SUBSTR);
    install("translit", "", B_TRANSLIT);
    install("include", "", B_INCLUDE);
    install("sinclude", "", B_SINCLUDE);
    install("syscmd", "", B_SYSCMD);
    install("maketemp", "", B_MAKETEMP);
    install("errprint", "", B_ERRPRINT);
    install("dumpdef", "", B_DUMPDEF);
    install("shift", "", B_SHIFT);
}

int main(int argc, char **argv)
{
    struct nlist *np;
    char **ap;
    int t, n, i, long_tok;

    instbltins();

    ap = argstk;
    if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
        signal(SIGHUP, catchsig);
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        signal(SIGINT, catchsig);
    if (mktemp(tempname) != tempname) {
        fprintf(stderr, "m4: cannot create temp file\n");
        exit(1);
    }
    close(creat(tempname, 0));

    nfiles = argc - 1;
    filev  = argv + 1;
    if (nfiles > 0)
        nextfile();

    for (;;) {
        t = getchr();
        if (t == EOF) {
            if (infptr > 0) {
                fclose(infile[infptr]);
                infptr--;
                nback = nback_stk[infptr];
                curx -= nback;
                continue;
            }
            if (!nextfile())
                break;
            continue;
        }
        token[0] = t;
        token[1] = EOS;
        if (type[t] == ALPH) {
            n        = 1;
            long_tok = 0;
            for (;;) {
                t = getchr();
                if (t == EOF || (type[t] != ALPH && type[t] != DIG))
                    break;
                // Longer than TOKS can hold is longer than any name we could match,
                // so emit what we have and go on reading it as text.  v7 ran off the
                // end of token[] instead.
                if (n >= TOKS - 1) {
                    token[n] = EOS;
                    puttok();
                    n        = 0;
                    long_tok = 1;
                }
                token[n++] = t;
            }
            if (t != EOF)
                putbak(t);
            token[n] = EOS;
            if (long_tok) {
                puttok();
                continue;
            }
            np = lookup(token);
            if ((*ap = np->def) != NULL) {
                if (++ap >= &argstk[ARGS])
                    ovf("arg stack");
                if (cp == NULL)
                    cp = callst;
                else if (++cp >= &callst[STACKS])
                    ovf("call stack");
                cp->argp  = ap;
                cp->bltin = np->bltin;
                *ap++     = op;
                puttok();
                outc('\0');
                t = getchr();
                if (t != EOF)
                    putbak(t);
                if (t != LPAR) {
                    putbak(')');
                    putbak('(');
                } else /* try to fix arg count */
                    *ap++ = op;
                cp->plev = 0;
            } else
                puttok();
        } else if (t == lquote) {
            i = 1;
            for (;;) {
                t = getchr();
                if (t == rquote) {
                    i--;
                    if (i == 0)
                        break;
                } else if (t == lquote)
                    i++;
                else if (t == EOF) {
                    fprintf(stderr, "m4: EOF in string\n");
                    delexit();
                }
                putchr(t);
            }
        } else if (t == COMMENT) {
            putbak(t);
            while ((t = getchr()) != '\n' && t != EOF)
                if (cp == NULL)
                    putchr(t);
            if (t != EOF)
                putbak(t);
        } else if (cp == NULL) {
            // The byte, not token: a NUL is ordinary here and puttok() prints a string.
            if (curfile)
                putc(t, curfile);
        } else if (t == LPAR) {
            if (cp->plev)
                outc(t);
            cp->plev++;
            /* skip leading white space during arg collection */
            while ((t = getchr()) == ' ' || t == '\t' || t == '\n')
                ;
            if (t != EOF)
                putbak(t);
        } else if (t == RPAR) {
            cp->plev--;
            if (cp->plev == 0) {
                outc('\0');
                expand(cp->argp, ap - cp->argp - 1, cp->bltin);
                op  = *cp->argp;
                opx = op - obuf;
                ap  = cp->argp - 1;
                cp--;
                if (cp < callst)
                    cp = NULL;
            } else
                outc(t);
        } else if (t == COMMA && cp->plev <= 1) {
            outc('\0');
            if (ap >= &argstk[ARGS])
                ovf("arg stack");
            *ap++ = op;
            /* skip leading white space during arg collection */
            while ((t = getchr()) == ' ' || t == '\t' || t == '\n')
                ;
            if (t != EOF)
                putbak(t);
        } else
            outc(t);
    }
    if (cp != NULL) {
        fprintf(stderr, "m4: unexpected EOF\n");
        delexit();
    }
    okret = 1;
    delexit();
}

// Open the next command-line file; 0 when they are all read.  A file of its own so that
// NUL is an ordinary byte: v7 primed the input with putbak(0) and tested EOF as `<= 0'.
static int nextfile(void)
{
    if (nfiles <= 0)
        return (0);
    nfiles--;
    if (infile[infptr] != stdin)
        fclose(infile[infptr]);
    if (strcmp(*filev, "-") == 0)
        infile[infptr] = stdin;
    else if ((infile[infptr] = fopen(*filev, "r")) == NULL) {
        fprintf(stderr, "m4: file not found: %s\n", *filev);
        delexit();
    }
    filev++;
    return (1);
}

static void catchsig(int sig)
{
    okret = 0;
    delexit();
}

static _Noreturn void ovf(char *what)
{
    fprintf(stderr, "m4: %s overflow\n", what);
    delexit();
}

static _Noreturn void delexit(void)
{
    FILE *fp;
    int i, c;

    if (!okret) {
        signal(SIGHUP, SIG_IGN);
        signal(SIGINT, SIG_IGN);
    }
    for (i = 1; i < NINCL; i++) {
        if (olist[i] == NULL)
            continue;
        fclose(olist[i]);
        tempname[7] = 'a' + i;
        if (okret && (fp = fopen(tempname, "r")) != NULL) {
            while ((c = getc(fp)) != EOF)
                putchar(c);
            fclose(fp);
        }
        unlink(tempname);
    }
    tempname[7] = 'a';
    unlink(tempname);
    exit(1 - okret);
}

static void puttok(void)
{
    char *tp;

    tp = token;
    if (cp) {
        while (*tp)
            outc(*tp++);
    } else if (curfile)
        while (*tp)
            putc(*tp++, curfile);
}

static void pbstr(char *str)
{
    int k;

    for (k = strlen(str); k > 0;)
        putbak(str[--k]);
}

static void expand(char **a1, int c, int bltin)
{
    char *bp, *dp;
    int n, k;

    switch (bltin) {
    case B_DEFINE:
        dodef(a1, c);
        return;
    case B_UNDEFINE:
        doundef(a1, c);
        return;
    case B_IFDEF:
        doifdef(a1, c);
        return;
    case B_CHANGEQUOTE:
        docq(a1, c);
        return;
    case B_DIVERT:
        dodiv(a1, c);
        return;
    case B_UNDIVERT:
        doundiv(a1, c);
        return;
    case B_DIVNUM:
        dodivnum(a1, c);
        return;
    case B_DNL:
        dodnl(a1, c);
        return;
    case B_IFELSE:
        doif(a1, c);
        return;
    case B_INCR:
        doincr(a1, c);
        return;
    case B_EVAL:
        doeval(a1, c);
        return;
    case B_LEN:
        dolen(a1, c);
        return;
    case B_INDEX:
        doindex(a1, c);
        return;
    case B_SUBSTR:
        dosubstr(a1, c);
        return;
    case B_TRANSLIT:
        dotransl(a1, c);
        return;
    case B_INCLUDE:
        doincl(a1, c, 1);
        return;
    case B_SINCLUDE:
        doincl(a1, c, 0);
        return;
    case B_SYSCMD:
        dosyscmd(a1, c);
        return;
    case B_MAKETEMP:
        domake(a1, c);
        return;
    case B_ERRPRINT:
        doerrp(a1, c);
        return;
    case B_DUMPDEF:
        dodump(a1, c);
        return;
    case B_SHIFT:
        doshift(a1, c);
        return;
    }

    // A user macro: push the body back, right to left, with $n replaced.  Counted
    // rather than walked with `dp > a1[-1]', which was two b$pdiff calls per byte.
    bp = a1[-1];
    k  = strlen(bp);
    dp = bp + k;
    while (k > 0) {
        --dp;
        --k;
        if (k > 0 && dp[-1] == '$') {
            n = *dp - '0';
            if (n >= 0 && n <= 9) {
                if (n <= c)
                    pbstr(a1[n]);
                --dp;
                --k;
            } else
                putbak(*dp);
        } else
            putbak(*dp);
    }
}

static struct nlist *lookup(char *str)
{
    char *s1, *s2;
    struct nlist *np;
    static struct nlist nodef;

    s1 = str;
    for (hshval = 0; *s1;)
        hshval += *s1++;
    hshval %= HSHSIZ;
    for (np = hshtab[hshval]; np != NULL; np = np->next) {
        s1 = str;
        s2 = np->name;
        while (*s1++ == *s2)
            if (*s2++ == EOS)
                return (np);
    }
    return (&nodef);
}

static char *install(char *nam, char *val, int bltin)
{
    struct nlist *np;

    if ((np = lookup(nam))->name == NULL) {
        np = (struct nlist *)malloc(sizeof(*np));
        if (np == NULL) {
            fprintf(stderr, "m4: no space for alloc\n");
            exit(1);
        }
        np->name       = copy(nam);
        np->def        = copy(val);
        np->bltin      = bltin;
        np->next       = hshtab[hshval];
        hshtab[hshval] = np;
        return (np->def);
    }
    free(np->def);
    np->def   = copy(val);
    np->bltin = bltin;
    return (np->def);
}

static void doundef(char **ap, int c)
{
    struct nlist *np, *tnp;

    if (c < 1 || (np = lookup(ap[1]))->name == NULL)
        return;
    tnp = hshtab[hshval]; /* lookup sets hshval */
    if (tnp == np)        /* it's in first place */
        hshtab[hshval] = np->next;
    else {
        for (; tnp->next != np; tnp = tnp->next)
            ;
        tnp->next = np->next;
    }
    free(np->name);
    free(np->def);
    free((char *)np);
}

static char *copy(char *s)
{
    char *p, *s1;

    p = s1 = malloc((unsigned)strlen(s) + 1);
    if (p == NULL) {
        fprintf(stderr, "m4: no space for alloc\n");
        exit(1);
    }
    while ((*s1++ = *s++) != 0)
        ;
    return (p);
}

static void dodef(char **ap, int c)
{
    if (c >= 2) {
        if (strcmp(ap[1], ap[2]) == 0) {
            fprintf(stderr, "m4: %s defined as itself\n", ap[1]);
            delexit();
        }
        install(ap[1], ap[2], B_NONE);
    } else if (c == 1)
        install(ap[1], "", B_NONE);
}

static void doifdef(char **ap, int c)
{
    if (c < 2)
        return;
    if (lookup(ap[1])->name != NULL)
        pbstr(ap[2]);
    else if (c >= 3)
        pbstr(ap[3]);
}

static void dolen(char **ap, int c)
{
    putnum(c < 1 ? 0 : (int)strlen(ap[1]));
}

// An empty argument restores that side's default: with NUL an ordinary byte, v7's
// `lquote = *ap[1]' would have made every NUL in the input a quote mark.
static void docq(char **ap, int c)
{
    if (c > 1) {
        lquote = *ap[1] ? *ap[1] : GRAVE;
        rquote = *ap[2] ? *ap[2] : ACUTE;
    } else if (c == 1 && *ap[1]) {
        lquote = rquote = *ap[1];
    } else {
        lquote = GRAVE;
        rquote = ACUTE;
    }
}

static void doshift(char **ap, int c)
{
    fprintf(stderr, "m4: shift not yet implemented\n");
}

static void dodump(char **ap, int c)
{
    int i;
    struct nlist *np;

    if (c > 0)
        while (c--) {
            if ((np = lookup(*++ap))->name != NULL)
                fprintf(stderr, "`%s'\t`%s'\n", np->name, np->def);
        }
    else
        for (i = 0; i < HSHSIZ; i++)
            for (np = hshtab[i]; np != NULL; np = np->next)
                fprintf(stderr, "`%s'\t`%s'\n", np->name, np->def);
}

// Literally, not as a printf format: the argument is the user's text, and v7 also
// passed five argstk slots it had not checked were there.
static void doerrp(char **ap, int c)
{
    if (c > 0)
        fprintf(stderr, "%s\n", ap[1]);
}

static void doeval(char **ap, int c)
{
    if (c > 0) {
        pe      = ap[1];
        evalerr = NULL;
        if (yyparse() == 0)
            putnum(evalval);
        else if (evalerr)
            fprintf(stderr, "m4: %s in eval: %s\n", evalerr, ap[1]);
        else
            fprintf(stderr, "m4: invalid expression in eval: %s\n", ap[1]);
    }
}

static void doincl(char **ap, int c, int noisy)
{
    FILE *fp;

    if (c < 1 || *ap[1] == EOS)
        return;
    if (infptr >= NINCL - 1) {
        fprintf(stderr, "m4: include nesting too deep\n");
        delexit();
    }
    if ((fp = fopen(ap[1], "r")) == NULL) {
        if (noisy) {
            fprintf(stderr, "m4: file not found: %s\n", ap[1]);
            delexit();
        }
        return;
    }
    // Push only once the open has succeeded.  v7 moved the pushback floor first and a
    // failed sinclude never moved it back, losing whatever was pending.
    nback_stk[infptr] = nback;
    infptr++;
    curx += nback;
    nback          = 0;
    infile[infptr] = fp;
}

static void dosyscmd(char **ap, int c)
{
    if (c > 0)
        system(ap[1]);
}

static void domake(char **ap, int c)
{
    // mktemp(3) walks back from the NUL, so an empty argument reads below the array.
    if (c > 0 && *ap[1] != EOS)
        pbstr(mktemp(ap[1]));
}

static void doincr(char **ap, int c)
{
    if (c >= 1)
        putnum(ctoi(ap[1]) + 1);
}

static void putnum(int num)
{
    int sign;

    sign = (num < 0) ? '-' : '\0';
    if (num < 0)
        num = -num;
    do {
        putbak(num % 10 + '0');
        num = num / 10;
    } while (num != 0);
    if (sign == '-')
        putbak('-');
}

static void dosubstr(char **ap, int c)
{
    int nc, start, avail, len;
    char *fc;

    if (c < 2)
        return;
    len   = strlen(ap[1]);
    start = ctoi(ap[2]);
    if (start < 0)
        start = 0;
    if (start > len)
        start = len;
    fc    = ap[1] + start;
    avail = len - start;
    // A missing length reaches the end of the string.  v7 used TOKS, which is the
    // token bound and truncated anything longer than 128 bytes.
    nc = (c < 3) ? avail : ctoi(ap[3]);
    if (nc > avail)
        nc = avail;
    while (nc > 0)
        putbak(fc[--nc]);
}

static void doindex(char **ap, int c)
{
    if (c >= 2)
        putnum(strindex(ap[1], ap[2]));
}

// v7 kept comparing after a mismatch, walking s past p1's NUL into whatever followed.
static int strindex(char *p1, char *p2)
{
    char *s, *t, *p;

    for (p = p1; *p; p++) {
        for (s = p, t = p2; *t && *t == *s; t++, s++)
            ;
        if (*t == EOS)
            return (p - p1);
    }
    return (-1);
}

static void dotransl(char **ap, int c)
{
    char *s, *fr, *to;
    int i;

    if (c <= 1)
        return;

    if (c == 2) {
        to = ap[1];
        for (s = ap[1]; *s; s++) {
            i = 0;
            for (fr = ap[2]; *fr; fr++)
                if (*s == *fr) {
                    i++;
                    break;
                }
            if (i == 0)
                *to++ = *s;
        }
        *to = '\0';
    }

    if (c >= 3) {
        for (s = ap[1]; *s; s++)
            for (fr = ap[2], to = ap[3]; *fr && *to; fr++, to++)
                if (*s == *fr)
                    *s = *to;
    }

    pbstr(ap[1]);
}

static void doif(char **ap, int c)
{
    if (c < 3)
        return;
    while (c >= 3) {
        if (strcmp(ap[1], ap[2]) == 0) {
            pbstr(ap[3]);
            return;
        }
        c -= 3;
        ap += 3;
    }
    if (c > 0)
        pbstr(ap[1]);
}

static void dodiv(char **ap, int c)
{
    int f;

    if (c < 1)
        f = 0;
    else
        f = ctoi(ap[1]);
    if (f >= NINCL || f < 0) {
        curfile = NULL;
        return;
    }
    tempname[7] = 'a' + f;
    if (olist[f] || (olist[f] = fopen(tempname, "w"))) {
        curout  = f;
        curfile = olist[f];
    }
}

// Copy diversion i into the current stream and discard it.
static void undivert(int i)
{
    FILE *fp;
    int ch;

    fclose(olist[i]);
    tempname[7] = 'a' + i;
    if ((fp = fopen(tempname, "r")) != NULL) {
        if (curfile != NULL)
            while ((ch = getc(fp)) != EOF)
                putc(ch, curfile);
        fclose(fp);
    }
    unlink(tempname);
    olist[i] = NULL;
}

static void doundiv(char **ap, int c)
{
    int i, j;

    if (c == 0) {
        for (i = 1; i < NINCL; i++)
            if (i != curout && olist[i] != NULL)
                undivert(i);
        return;
    }
    for (j = 1; j <= c; j++) {
        i = ctoi(*++ap);
        if (i < 1 || i >= NINCL || i == curout || olist[i] == NULL)
            continue;
        undivert(i);
    }
}

static void dodivnum(char **ap, int c)
{
    putnum(curout);
}

static void dodnl(char **ap, int c)
{
    int t;

    while ((t = getchr()) != '\n' && t != EOF)
        ;
}

//
// A leading digit string as a number.  v7 had this as ctol() returning long with an
// int wrapper called ctoi(); a long is an int is one word here, so the wrapper was
// an identity function and is gone.
//
static int ctoi(char *str)
{
    int sign;
    int num;

    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;
    num = 0;
    if (*str == '-') {
        sign = -1;
        str++;
    } else
        sign = 1;
    while (*str >= '0' && *str <= '9')
        num = num * 10 + *str++ - '0';
    return (sign * num);
}
