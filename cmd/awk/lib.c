// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "awk.h"
#include "y.tab.h"

static FILE *infile = NULL;
static char *file;

// Part of the size profile b.c heads: v7 had 2,560 bytes of record and as much again of
// fields, 854 words between them, and every word is heap this program does not get.
#define RECSIZE 768
char record[RECSIZE];
static char fields[RECSIZE];

#define MAXFLD 40
int donefld; // 1 = implies rec broken into fields
int donerec; // 1 = record is valid (no flds have changed)
int mustfld; // 1 = NF seen, so always break

// v7 initialized this statically.  A string literal cannot initialize a char * inside a
// struct initializer here, so it is built at startup.
static cell fldtab[MAXFLD];

void fldinit(void)
{
    int i;

    fldtab[0].nval = "$record";
    fldtab[0].sval = record;
    fldtab[0].tval = STR | FLD;
    for (i = 1; i < MAXFLD; i++)
        fldtab[i].tval = FLD | STR;
}

static int maxfld = 0; // last used field

int getrec(void)
{
    register char *rr;
    register int c, sep;

    donefld   = 0;
    donerec   = 1;
    record[0] = 0;
    while (svargc > 0) {
        if (infile == NULL) {           // have to open a new file
            if (member('=', *svargv)) { // it's a var=value argument
                setclvar(*svargv);
                svargv++;
                svargc--;
                continue;
            }
            *FILENAME = file = *svargv;
            if (*file == '-')
                infile = stdin;
            else if ((infile = fopen(file, "r")) == NULL)
                error(FATAL, "can't open %s", file);
        }
        if ((sep = **RS) == 0)
            sep = '\n';
        for (rr = record;;) {
            for (; (c = getc(infile)) != sep && c != EOF; *rr++ = c)
                if (rr >= record + RECSIZE - 2)
                    error(FATAL, "record `%.20s...' too long", record);
            if (**RS == sep || c == EOF)
                break;
            if ((c = getc(infile)) == '\n' || c == EOF) // 2 in a row
                break;
            if (rr >= record + RECSIZE - 3)
                error(FATAL, "record `%.20s...' too long", record);
            *rr++ = '\n';
            *rr++ = c;
        }
        *rr = 0;
        if (mustfld)
            fldbld();
        if (c != EOF || rr > record) { // normal record
            recloc->tval &= ~NUM;
            recloc->tval |= STR;
            ++nrloc->fval;
            nrloc->tval &= ~STR;
            nrloc->tval |= NUM;
            return 1;
        }
        // EOF arrived on this file; set up next
        if (infile != stdin)
            fclose(infile);
        infile = NULL;
        svargc--;
        svargv++;
    }
    return 0; // true end of file
}

void setclvar(char *s) // set var=value from s
{
    char *p;
    cell *q;

    for (p = s; *p != '='; p++)
        ;
    *p++ = 0;
    q    = setsymtab(s, tostring(p), 0.0, STR, symtab);
    setsval(q, p);
}

void fldbld(void)
{
    register char *r, *fr;
    register int sep;
    int i, j;

    r  = record;
    fr = fields;
    i  = 0; // number of fields accumulated here
    if ((sep = **FS) == ' ')
        for (i = 0;;) {
            while (*r == ' ' || *r == '\t' || *r == '\n')
                r++;
            if (*r == 0)
                break;
            i++;
            if (i >= MAXFLD)
                error(FATAL, "record `%.20s...' has too many fields", record);
            if (!(fldtab[i].tval & FLD))
                xfree(fldtab[i].sval);
            fldtab[i].sval = fr;
            fldtab[i].tval = FLD | STR;
            do
                *fr++ = *r++;
            while (*r != ' ' && *r != '\t' && *r != '\n' && *r != '\0');
            *fr++ = 0;
        }
    else if (*r != 0) // if 0, it's a null field
        for (;;) {
            i++;
            if (i >= MAXFLD)
                error(FATAL, "record `%.20s...' has too many fields", record);
            if (!(fldtab[i].tval & FLD))
                xfree(fldtab[i].sval);
            fldtab[i].sval = fr;
            fldtab[i].tval = FLD | STR;
            while (*r != sep && *r != '\n' && *r != '\0') // \n always a separator
                *fr++ = *r++;
            *fr++ = 0;
            if (*r++ == 0)
                break;
        }
    *fr = 0;
    for (j = MAXFLD - 1; j > i; j--) { // clean out junk from previous record
        if (!(fldtab[j].tval & FLD))
            xfree(fldtab[j].sval);
        fldtab[j].tval = STR | FLD;
        fldtab[j].sval = NULL;
    }
    maxfld  = i;
    donefld = 1;
    for (i = 1; i <= maxfld; i++)
        if (isnumstr(fldtab[i].sval)) {
            fldtab[i].fval = atof(fldtab[i].sval);
            fldtab[i].tval |= NUM;
        }
    setfval(lookup("NF", symtab, 0), (awkfloat)maxfld);
}

void recbld(void)
{
    int i;
    register char *r, *p;

    if (donefld == 0 || donerec == 1)
        return;
    r = record;
    for (i = 1; i <= *NF; i++) {
        p = getsval(&fldtab[i]);
        while (*p) {
            if (r >= record + RECSIZE - 1)
                error(FATAL, "built giant record `%.20s...'", record);
            *r++ = *p++;
        }
        *r++ = **OFS;
    }
    if (r > record) // v7 wrote record[-1] for an empty record
        r--;
    *r           = '\0';
    recloc->tval = STR | FLD;
}

cell *fieldadr(int n)
{
    if (n >= MAXFLD)
        error(FATAL, "trying to access field %d", n);
    return &fldtab[n];
}

int errorflag = 0;

void yyerror(char *s)
{
    fprintf(stderr, "awk: %s near line %d\n", s, lineno);
    errorflag = 2;
}

void error(int f, const char *s, ...)
{
    va_list ap;

    fprintf(stderr, "awk: ");
    va_start(ap, s);
    vfprintf(stderr, s, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    if (*NR > 0)
        fprintf(stderr, " record number %g\n", *NR);
    if (f)
        exit(2);
}

// The largest decimal exponent this machine's float reaches.  v7's 38 was the PDP-11's,
// and it is not cosmetic: libc's atof() has no overflow gate and FAULTS past DBL_MAX, so
// this test is what stands between an awk script and a dead interpreter.
#define MAXEXPON LOGHUGE

// v7 called this isnumber(), a name libc reserves.
int isnumstr(char *s)
{
    register int d1, d2;
    int point, nd, expval, expneg;

    d1 = d2 = point = expval = expneg = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '\0')
        return 0; // empty stuff isn't number
    if (*s == '+' || *s == '-')
        s++;
    if (!isdigit(*s) && *s != '.')
        return 0;
    if (isdigit(*s)) {
        do {
            d1++;
            s++;
        } while (isdigit(*s));
    }
    if (*s == '.') {
        point++;
        s++;
    }
    if (isdigit(*s)) {
        d2++;
        do {
            s++;
        } while (isdigit(*s));
    }
    if (!(d1 || (point && d2)))
        return 0;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+')
            s++;
        else if (*s == '-')
            expneg = 1, s++;
        if (!isdigit(*s))
            return 0;
        for (nd = 0; isdigit(*s); s++) {
            if (++nd > 2) // v7 allowed at most two exponent digits
                return 0;
            expval = expval * 10 + *s - '0';
        }
        if (expval >= MAXEXPON)
            return 0;
        if (expneg)
            expval = -expval;
    }
    // v7 tested the digit count and the exponent but never their sum, so 99e18 passed
    // both and then faulted in atof().
    if (d1 + expval >= MAXEXPON)
        return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '\0')
        return 1;
    else
        return 0;
}
