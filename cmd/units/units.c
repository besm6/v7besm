/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define NDIM 10
#define NTAB 601

// The largest power of ten this machine holds, and the table of them below it.  Every scale
// factor is applied from here rather than built by repeated multiplication: see getflt().
#define PTEN_MAX DBL_MAX_10_EXP

static char *dfile = "/usr/lib/units";
static char *unames[NDIM];

struct unit {
    double factor;
    char dim[NDIM];
};

struct table {
    double factor;
    char dim[NDIM];
    char *name;
};

static struct table table[NTAB];
static char names[NTAB * 10];

struct prefix {
    double factor;
    char *pname;
};

// Explicit inner braces: b6lower fills an aggregate initializer positionally and says nothing
// about the elision (../README.md).
static struct prefix prefix[] = {
    { 1e-18, "atto" }, { 1e-15, "femto" }, { 1e-12, "pico" }, { 1e-9, "nano" },
    { 1e-6, "micro" }, { 1e-3, "milli" },  { 1e-2, "centi" }, { 1e-1, "deci" },
    { 1e1, "deka" },   { 1e2, "hecta" },   { 1e2, "hecto" },  { 1e3, "kilo" },
    { 1e6, "mega" },   { 1e6, "meg" },     { 1e9, "giga" },   { 1e12, "tera" },
    { 0.0, 0 },
};

static const double pten[PTEN_MAX + 1] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18,
};

static FILE *inp;
static int fperrc;
static int peekc;
static int dumpflg;

static void units(struct unit *up);
static int pu(int u, int i, int f);
static int convr(struct unit *up);
static int lookup(char *name, struct unit *up, int den, int c);
static int equal(char *s1, char *s2);
static void init(void);
static double getflt(void);
static int get(void);
static struct table *hash(char *name);
static void fperr(int sig);

// a*b and a/b, gated: an out-of-range result is a machine fault and not a signal, so the test
// goes BEFORE the operation.  README.md, "The gates".
static double fmul(double a, double b)
{
    double x = fabs(a), y = fabs(b);

    if (x == 0. || y == 0.)
        return 0.;
    if (y >= 1. ? x > DBL_MAX / y : x < DBL_MIN / y) {
        fperrc++;
        return 0.;
    }
    return a * b;
}

static double fdiv(double a, double b)
{
    double x = fabs(a), y = fabs(b);

    if (y == 0.) { // the machine faults on a zero -- or denormal -- divisor
        fperrc++;
        return 0.;
    }
    if (x == 0.)
        return 0.;
    if (y >= 1. ? x < DBL_MIN * y : x > DBL_MAX * y) {
        fperrc++;
        return 0.;
    }
    return a / b;
}

int main(int argc, char **argv)
{
    int i;
    char *file;
    struct unit u1, u2;
    double f;

    if (argc > 1 && *argv[1] == '-') {
        argc--;
        argv++;
        dumpflg++;
    }
    file = dfile;
    if (argc > 1)
        file = argv[1];
    if ((inp = fopen(file, "r")) == NULL) {
        printf("no table\n");
        exit(1);
    }
    // The gates should leave nothing to catch; kept because the kernel raises SIGFPE now
    // (kernel/trap.c) and a diagnosis beats a dead process.
    signal(SIGFPE, fperr);
    init();

loop:
    fperrc = 0;
    printf("you have: ");
    if (convr(&u1))
        goto loop;
    if (fperrc)
        goto fp;
loop1:
    printf("you want: ");
    if (convr(&u2))
        goto loop1;
    for (i = 0; i < NDIM; i++)
        if (u1.dim[i] != u2.dim[i])
            goto conform;
    f = fdiv(u1.factor, u2.factor);
    if (fperrc)
        goto fp;
    printf("\t* %e\n", f);
    printf("\t/ %e\n", fdiv(1., f));
    if (fperrc)
        goto fp;
    goto loop;

conform:
    if (fperrc)
        goto fp;
    printf("conformability\n");
    units(&u1);
    units(&u2);
    goto loop;

fp:
    printf("underflow or overflow\n");
    goto loop;
}

static void units(struct unit *up)
{
    struct unit *p;
    int f, i;

    p = up;
    printf("\t%e ", p->factor);
    f = 0;
    for (i = 0; i < NDIM; i++)
        f |= pu(p->dim[i], i, f);
    if (f & 1) {
        putchar('/');
        f = 0;
        for (i = 0; i < NDIM; i++)
            f |= pu(-p->dim[i], i, f);
    }
    putchar('\n');
}

static int pu(int u, int i, int f)
{
    if (u > 0) {
        if (f & 2)
            putchar('-');
        if (unames[i])
            printf("%s", unames[i]);
        else
            printf("*%c*", i + 'a');
        if (u > 1)
            putchar(u + '0');
        return 2;
    }
    if (u < 0)
        return 1;
    return 0;
}

static int convr(struct unit *up)
{
    struct unit *p;
    int c;
    char *cp;
    char name[20];
    int den, err;

    p = up;
    for (c = 0; c < NDIM; c++)
        p->dim[c] = 0;
    p->factor = getflt();
    if (p->factor == 0.)
        p->factor = 1.0;
    err = 0;
    den = 0;
    cp  = name;

loop:
    switch (c = get()) {

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
    case '/':
    case ' ':
    case '\t':
    case '\n':
        if (cp != name) {
            *cp++ = 0;
            cp    = name;
            err |= lookup(cp, p, den, c);
        }
        if (c == '/')
            den++;
        if (c == '\n')
            return err;
        goto loop;
    }
    *cp++ = c;
    goto loop;
}

static int lookup(char *name, struct unit *up, int den, int c)
{
    struct unit *p;
    struct table *q;
    int i;
    char *cp1, *cp2;
    double e;

    p = up;
    e = 1.0;

loop:
    q = hash(name);
    if (q->name) {
    l1:
        if (den) {
            p->factor = fdiv(p->factor, fmul(q->factor, e));
            for (i = 0; i < NDIM; i++)
                p->dim[i] -= q->dim[i];
        } else {
            p->factor = fmul(p->factor, fmul(q->factor, e));
            for (i = 0; i < NDIM; i++)
                p->dim[i] += q->dim[i];
        }
        if (c >= '2' && c <= '9') {
            c--;
            goto l1;
        }
        return 0;
    }
    for (i = 0; (cp1 = prefix[i].pname) != 0; i++) {
        cp2 = name;
        while (*cp1 == *cp2++)
            if (*cp1++ == 0) {
                cp1--;
                break;
            }
        if (*cp1 == 0) {
            e    = fmul(e, prefix[i].factor);
            name = cp2 - 1;
            goto loop;
        }
    }
    for (cp1 = name; *cp1; cp1++)
        ;
    if (cp1 > name + 1 && *--cp1 == 's') {
        *cp1 = 0;
        goto loop;
    }
    printf("cannot recognize %s\n", name);
    return 1;
}

static int equal(char *s1, char *s2)
{
    char *c1, *c2;

    c1 = s1;
    c2 = s2;
    while (*c1++ == *c2)
        if (*c2++ == 0)
            return 1;
    return 0;
}

static void init(void)
{
    char *cp;
    struct table *tp, *lp;
    int c, i, f, t;
    char *np;

    cp = names;
    for (i = 0; i < NDIM; i++) {
        np    = cp;
        *cp++ = '*';
        *cp++ = i + 'a';
        *cp++ = '*';
        *cp++ = 0;
        lp    = hash(np);
        lp->name   = np;
        lp->factor = 1.0;
        lp->dim[i] = 1;
    }
    lp         = hash("");
    lp->name   = cp - 1;
    lp->factor = 1.0;

l0:
    c = get();
    if (c == 0) {
        printf("%d units; %d bytes\n\n", i, (int)(cp - names));
        if (dumpflg)
            for (tp = &table[0]; tp < &table[NTAB]; tp++) {
                if (tp->name == 0)
                    continue;
                printf("%s", tp->name);
                units((struct unit *)tp);
            }
        fclose(inp);
        inp = stdin;
        return;
    }
    if (c == '/') {
        while (c != '\n' && c != 0)
            c = get();
        goto l0;
    }
    if (c == '\n')
        goto l0;
    np = cp;
    while (c != ' ' && c != '\t') {
        *cp++ = c;
        c     = get();
        if (c == 0)
            goto l0;
        if (c == '\n') {
            *cp++ = 0;
            tp    = hash(np);
            if (tp->name)
                goto redef;
            tp->name   = np;
            tp->factor = lp->factor;
            for (c = 0; c < NDIM; c++)
                tp->dim[c] = lp->dim[c];
            i++;
            goto l0;
        }
    }
    *cp++ = 0;
    lp    = hash(np);
    if (lp->name)
        goto redef;
    fperrc = 0;
    convr((struct unit *)lp);
    // A definition the machine cannot hold is DROPPED, not kept as a zero: kept, it would fault
    // the first time somebody divided by it.  ../units has six commented out, so only a table
    // named on the command line reaches this.
    if (fperrc) {
        fperrc = 0;
        printf("out of range %s\n", np);
        cp = np;
        goto l0;
    }
    lp->name = np;
    f        = 0;
    i++;
    if (lp->factor != 1.0)
        goto l0;
    for (c = 0; c < NDIM; c++) {
        t = lp->dim[c];
        if (t > 1 || (f > 0 && t != 0))
            goto l0;
        if (f == 0 && t == 1) {
            if (unames[c])
                goto l0;
            f = c + 1;
        }
    }
    if (f > 0)
        unames[f - 1] = np;
    goto l0;

redef:
    printf("redefinition %s\n", np);
    goto l0;
}

static double getflt(void)
{
    int c, i, dp;
    double d;
    int f;

    d  = 0.;
    dp = 0;
    do
        c = get();
    while (c == ' ' || c == '\t');

l1:
    if (c >= '0' && c <= '9') {
        // Stop at more digits than the machine can keep: v7 accumulated the whole string, and
        // ../units's own 21-digit `pi' overflows on its own.  README.md, "The scale factor".
        if (d < 1e12) {
            d = d * 10. + c - '0';
            if (dp)
                dp++;
        } else if (!dp)
            dp--;
        c = get();
        goto l1;
    }
    if (c == '.') {
        dp++;
        c = get();
        goto l1;
    }
    if (dp)
        dp--;
    if (c == '+' || c == '-') {
        f = 0;
        if (c == '-')
            f++;
        i = 0;
        c = get();
        while (c >= '0' && c <= '9') {
            if (i < 10000) // no exponent that large is representable; do not wrap the int
                i = i * 10 + c - '0';
            c = get();
        }
        if (f)
            i = -i;
        dp -= i;
    }

    // Gated bites of at most 10^PTEN_MAX.  v7 built 10^|dp| first, which faults for |dp| >= 19
    // even when the VALUE is in range.  README.md, "The scale factor".
    i = dp < 0 ? -dp : dp;
    while (i > 0) {
        int step = i > PTEN_MAX ? PTEN_MAX : i;

        d = dp < 0 ? fmul(d, pten[step]) : fdiv(d, pten[step]);
        i -= step;
    }

    if (c == '|')
        return fdiv(d, getflt());
    peekc = c;
    return d;
}

static int get(void)
{
    int c;

    if ((c = peekc) != 0) {
        peekc = 0;
        return c;
    }
    c = getc(inp);
    if (c == EOF) {
        if (inp == stdin) {
            printf("\n");
            exit(0);
        }
        return 0;
    }
    return c;
}

static struct table *hash(char *name)
{
    struct table *tp;
    char *np;
    unsigned h;

    h  = 0;
    np = name;
    while (*np)
        h = h * 57 + *np++ - '0';
    h %= NTAB;
    tp = &table[h];
l0:
    if (tp->name == 0)
        return tp;
    if (equal(name, tp->name))
        return tp;
    tp++;
    if (tp >= &table[NTAB])
        tp = table;
    goto l0;
}

static void fperr(int sig)
{
    signal(sig, fperr);
    fperrc++;
}
