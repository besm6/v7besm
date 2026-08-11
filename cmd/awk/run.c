// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "awk.h"
#include "y.tab.h"

#define RECSIZE 512

#define FILENUM 10
static struct {
    FILE *fp;
    char *fname;
} files[FILENUM];

#define PA2NUM 29
int pairstack[PA2NUM], paircnt;
node *winner = (node *)NULL;
#define MAXTMP 20
static cell tmps[MAXTMP];
static cell nullval = { 0, 0, 0.0, NUM, 0 };
obj awktrue         = { OBOOL, BTRUE, 0 };
obj awkfalse        = { OBOOL, BFALSE, 0 };

void run(void)
{
    execute(winner);
}

// execute() recurses as deep as the awk program nests, and the stack is 4,096 words --
// measured at ~100 words a level here, plus the operator's own frame.  README.md.
#define MAXDEPTH 15
static int depth;

obj execute(node *u)
{
    register procfn proc;
    obj x;
    node *a;

    if (u == (node *)NULL)
        return awktrue;
    if (++depth > MAXDEPTH)
        error(FATAL, "expression nested too deeply");
    for (a = u;; a = a->nnext) {
        if (cantexec(a)) {
            x = nodetoobj(a);
            break;
        }
        if (a->ntype == NPA2)
            proc = dopa2;
        else {
            if (notlegal(a->nobj))
                error(FATAL, "illegal statement, token %d", a->nobj);
            proc = proctab[a->nobj - FIRSTTOKEN];
        }
        x = (*proc)(a->narg, a->nobj);
        if (isfld(x))
            fldbld();
        if (isexpr(a))
            break;
        // a statement, goto next statement
        if (isjump(x))
            break;
        if (a->nnext == (node *)NULL)
            break;
        tempfree(x);
    }
    depth--;
    return x;
}

obj program(node **a, int n)
{
    obj x;

    x = awktrue; // v7 read x uninitialised when there was no BEGIN and no record
    if (a[0] != NULL) {
        x = execute(a[0]);
        if (isexit(x))
            return awktrue;
        if (isjump(x))
            error(FATAL, "unexpected break, continue or next");
        tempfree(x);
    }
    while (getrec()) {
        x = execute(a[1]);
        if (isexit(x))
            break;
        tempfree(x);
    }
    tempfree(x);
    if (a[2] != NULL) {
        x = execute(a[2]);
        if (isbreak(x) || isnext(x) || iscont(x))
            error(FATAL, "unexpected break, continue or next");
        tempfree(x);
    }
    return awktrue;
}

obj awkgetline(node **a, int n)
{
    obj x;

    x = gettemp();
    setfval(x.optr, (awkfloat)getrec());
    return x;
}

obj array(node **a, int n)
{
    obj x, y;

    x = execute(a[1]);
    y = arrayel(a[0], x);
    tempfree(x);
    return y;
}

obj arrayel(node *a, obj b)
{
    char *s;
    cell *x;
    obj y;

    s = getsval(b.optr);
    x = (cell *)a;
    if (!(x->tval & ARR)) {
        xfree(x->sval);
        x->tval &= ~STR;
        x->tval |= ARR;
        x->sval = (char *)makesymtab();
    }
    y.optr  = setsymtab(s, tostring(""), 0.0, STR | NUM, (cell **)x->sval);
    y.otype = OCELL;
    y.osub  = CVAR;
    return y;
}

obj matchop(node **a, int n)
{
    obj x;
    char *s;
    int i;

    x = execute(a[0]);
    if (isstr(x))
        s = x.optr->sval;
    else
        s = getsval(x.optr);
    tempfree(x);
    i = match((struct fa *)a[1], s);
    if ((n == MATCH && i == 1) || (n == NOTMATCH && i == 0))
        return awktrue;
    else
        return awkfalse;
}

obj boolop(node **a, int n)
{
    obj x, y;
    int i;

    x = execute(a[0]);
    i = istrue(x);
    tempfree(x);
    switch (n) {
    default:
        error(FATAL, "unknown boolean operator %d", n);
    case BOR:
        if (i)
            return awktrue;
        y = execute(a[1]);
        i = istrue(y);
        tempfree(y);
        if (i)
            return awktrue;
        else
            return awkfalse;
    case AND:
        if (!i)
            return awkfalse;
        y = execute(a[1]);
        i = istrue(y);
        tempfree(y);
        if (i)
            return awktrue;
        else
            return awkfalse;
    case NOT:
        if (i)
            return awkfalse;
        else
            return awktrue;
    }
}

obj relop(node **a, int n)
{
    int i;
    obj x, y;
    awkfloat j;

    x = execute(a[0]);
    y = execute(a[1]);
    if (x.optr->tval & NUM && y.optr->tval & NUM) {
        j = x.optr->fval - y.optr->fval;
        i = j < 0 ? -1 : (j > 0 ? 1 : 0);
    } else {
        i = strcmp(getsval(x.optr), getsval(y.optr));
    }
    tempfree(x);
    tempfree(y);
    switch (n) {
    default:
        error(FATAL, "unknown relational operator %d", n);
    case LT:
        return i < 0 ? awktrue : awkfalse;
    case LE:
        return i <= 0 ? awktrue : awkfalse;
    case NE:
        return i != 0 ? awktrue : awkfalse;
    case EQ:
        return i == 0 ? awktrue : awkfalse;
    case GE:
        return i >= 0 ? awktrue : awkfalse;
    case GT:
        return i > 0 ? awktrue : awkfalse;
    }
}

void tempfree(obj a)
{
    if (!istemp(a))
        return;
    xfree(a.optr->sval);
    a.optr->tval = 0;
}

obj gettemp(void)
{
    int i;
    obj x;

    for (i = 0; i < MAXTMP; i++)
        if (tmps[i].tval == 0)
            break;
    if (i == MAXTMP)
        error(FATAL, "out of temporaries in gettemp");
    x.optr  = &tmps[i];
    tmps[i] = nullval;
    x.otype = OCELL;
    x.osub  = CTEMP;
    return x;
}

obj indirect(node **a, int n)
{
    obj x;
    int m;

    x = execute(a[0]);
    m = getfval(x.optr);
    tempfree(x);
    x.optr  = fieldadr(m);
    x.otype = OCELL;
    x.osub  = CFLD;
    return x;
}

obj substr(node **a, int nnn)
{
    char *s, temp;
    obj x;
    int k, m, n;

    x = execute(a[0]);
    s = getsval(x.optr);
    k = strlen(s) + 1;
    tempfree(x);
    x = execute(a[1]);
    m = getfval(x.optr);
    if (m <= 0)
        m = 1;
    else if (m > k)
        m = k;
    tempfree(x);
    if (a[2] != nullstat) {
        x = execute(a[2]);
        n = getfval(x.optr);
        tempfree(x);
    } else
        n = k - 1;
    if (n < 0)
        n = 0;
    else if (n > k - m)
        n = k - m;
    x            = gettemp();
    temp         = s[n + m - 1]; // with thanks to John Linderman
    s[n + m - 1] = '\0';
    setsval(x.optr, s + m - 1);
    s[n + m - 1] = temp;
    return x;
}

obj sindex(node **a, int nnn)
{
    obj x;
    char *s1, *s2, *p1, *p2, *q;

    x  = execute(a[0]);
    s1 = getsval(x.optr);
    tempfree(x);
    x  = execute(a[1]);
    s2 = getsval(x.optr);
    tempfree(x);

    x = gettemp();
    for (p1 = s1; *p1 != '\0'; p1++) {
        for (q = p1, p2 = s2; *p2 != '\0' && *q == *p2; q++, p2++)
            ;
        if (*p2 == '\0') {
            setfval(x.optr, (awkfloat)(p1 - s1 + 1)); // origin 1
            return x;
        }
    }
    setfval(x.optr, 0.0);
    return x;
}

char *format(char *s, node *a)
{
    char *buf, *p, fmt[200], *t, *os;
    obj x;
    int flag = 0;
    int nfmt;
    awkfloat xf;

    os = s;
    p = buf = (char *)malloc(RECSIZE);
    if (buf == NULL)
        error(FATAL, "out of space in format");
    while (*s) {
        if (*s != '%') {
            *p++ = *s++;
            continue;
        }
        if (*(s + 1) == '%') {
            *p++ = '%';
            s += 2;
            continue;
        }
        // v7 tested the bound after the copy had already run off fmt[]
        for (t = fmt, nfmt = 0; (*t++ = *s) != '\0'; s++) {
            if (++nfmt >= (int)sizeof(fmt) - 2)
                error(FATAL, "format item %.20s... too long", os);
            if (*s >= 'a' && *s <= 'z' && *s != 'l')
                break;
        }
        *t = '\0';
        switch (*s) {
        case 'f':
        case 'e':
        case 'g':
            flag = 1;
            break;
        case 'd':
            flag = 2;
            break;
        case 'o':
        case 'x':
            flag = *(s - 1) == 'l' ? 2 : 3;
            break;
        case 'c':
            flag = 3;
            break;
        case 's':
            flag = 4;
            break;
        default:
            flag = 0;
            break;
        }
        if (flag == 0) {
            sprintf(p, "%s", fmt);
            p += strlen(p);
            continue;
        }
        if (a == NULL)
            error(FATAL, "not enough arguments in printf(%s)", os);
        x = execute(a);
        a = a->nnext;
        if (flag != 4) // watch out for converting to numbers!
            xf = getfval(x.optr);
        if (flag == 1)
            sprintf(p, fmt, xf);
        else if (flag == 2) {
            if (xf <= -TWO40 || xf >= TWO40)
                error(FATAL, "%d format: %g is out of range", xf);
            sprintf(p, fmt, (long)xf);
        } else if (flag == 3)
            sprintf(p, fmt, (int)xf);
        else if (flag == 4)
            sprintf(p, fmt, x.optr->sval == NULL ? "" : getsval(x.optr));
        tempfree(x);
        p += strlen(p);
        s++;
    }
    *p = '\0';
    return buf;
}

obj asprintf(node **a, int n)
{
    obj x;
    node *y;
    char *s;

    y = a[0]->nnext;
    x = execute(a[0]);
    s = format(getsval(x.optr), y);
    tempfree(x);
    x            = gettemp();
    x.optr->sval = s;
    x.optr->tval = STR;
    return x;
}

obj arith(node **a, int n)
{
    awkfloat i, j;
    obj x, y, z;

    j = 0.0;
    x = execute(a[0]);
    i = getfval(x.optr);
    tempfree(x);
    if (n != UMINUS) {
        y = execute(a[1]);
        j = getfval(y.optr);
        tempfree(y);
    }
    z = gettemp();
    switch (n) {
    default:
        error(FATAL, "illegal arithmetic operator %d", n);
    case ADD:
        i += j;
        break;
    case MINUS:
        i -= j;
        break;
    case MULT:
        i *= j;
        break;
    case DIVIDE:
        if (j == 0)
            error(FATAL, "division by zero");
        i /= j;
        break;
    case MOD:
        if (j == 0)
            error(FATAL, "division by zero");
        i = fmod(i, j); // x - y*trunc(x/y) loses every bit above 2^40
        break;
    case UMINUS:
        i = -i;
        break;
    }
    setfval(z.optr, i);
    return z;
}

obj incrdecr(node **a, int n)
{
    obj x, z;
    int k;
    awkfloat xf;

    x  = execute(a[0]);
    xf = getfval(x.optr);
    k  = (n == PREINCR || n == POSTINCR) ? 1 : -1;
    if (n == PREINCR || n == PREDECR) {
        setfval(x.optr, xf + k);
        return x;
    }
    z = gettemp();
    setfval(z.optr, xf);
    setfval(x.optr, xf + k);
    tempfree(x);
    return z;
}

obj assign(node **a, int n)
{
    obj x, y;
    awkfloat xf, yf;

    x = execute(a[0]);
    y = execute(a[1]);
    if (n == ASSIGN) { // ordinary assignment
        if ((y.optr->tval & (STR | NUM)) == (STR | NUM)) {
            setsval(x.optr, y.optr->sval);
            x.optr->fval = y.optr->fval;
            x.optr->tval |= NUM;
        } else if (y.optr->tval & STR)
            setsval(x.optr, y.optr->sval);
        else if (y.optr->tval & NUM)
            setfval(x.optr, y.optr->fval);
        tempfree(y);
        return x;
    }
    xf = getfval(x.optr);
    yf = getfval(y.optr);
    switch (n) {
    case ADDEQ:
        xf += yf;
        break;
    case SUBEQ:
        xf -= yf;
        break;
    case MULTEQ:
        xf *= yf;
        break;
    case DIVEQ:
        if (yf == 0)
            error(FATAL, "division by zero");
        xf /= yf;
        break;
    case MODEQ:
        if (yf == 0)
            error(FATAL, "division by zero");
        xf = fmod(xf, yf);
        break;
    default:
        error(FATAL, "illegal assignment operator %d", n);
        break;
    }
    tempfree(y);
    setfval(x.optr, xf);
    return x;
}

obj cat(node **a, int q)
{
    obj x, y, z;
    int n1, n2;
    char *s;

    x = execute(a[0]);
    y = execute(a[1]);
    getsval(x.optr);
    getsval(y.optr);
    n1 = strlen(x.optr->sval);
    n2 = strlen(y.optr->sval);
    s  = (char *)malloc(n1 + n2 + 1);
    if (s == NULL)
        error(FATAL, "out of space concatenating strings");
    strcpy(s, x.optr->sval);
    strcpy(s + n1, y.optr->sval);
    tempfree(y);
    z            = gettemp();
    z.optr->sval = s;
    z.optr->tval = STR;
    tempfree(x);
    return z;
}

obj pastat(node **a, int n)
{
    obj x;

    if (a[0] == nullstat)
        x = awktrue;
    else
        x = execute(a[0]);
    if (istrue(x)) {
        tempfree(x);
        x = execute(a[1]);
    }
    return x;
}

obj dopa2(node **a, int n)
{
    obj x;

    if (pairstack[n] == 0) {
        x = execute(a[0]);
        if (istrue(x))
            pairstack[n] = 1;
        tempfree(x);
    }
    if (pairstack[n] == 1) {
        x = execute(a[1]);
        if (istrue(x))
            pairstack[n] = 0;
        tempfree(x);
        x = execute(a[2]);
        return x;
    }
    return awkfalse;
}

obj aprintf(node **a, int n)
{
    obj x;

    x = asprintf(a, n);
    if (a[1] == NULL) {
        printf("%s", x.optr->sval);
        tempfree(x);
        return awktrue;
    }
    redirprint(x.optr->sval, (int)a[1], a[2]);
    return x;
}

obj split(node **a, int nnn)
{
    obj x;
    cell *ap;
    register char *s, *p;
    char *t, temp, num[16];
    register int sep;
    int n;

    x = execute(a[0]);
    s = getsval(x.optr);
    tempfree(x);
    if (a[2] == nullstat)
        sep = **FS;
    else {
        x   = execute(a[2]);
        sep = getsval(x.optr)[0];
        tempfree(x);
    }
    ap = (cell *)a[1];
    freesymtab(ap);
    ap->tval &= ~STR;
    ap->tval |= ARR;
    ap->sval = (char *)makesymtab();

    n = 0;
    if (sep == ' ')
        for (n = 0;;) {
            while (*s == ' ' || *s == '\t' || *s == '\n')
                s++;
            if (*s == 0)
                break;
            n++;
            t = s;
            do
                s++;
            while (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\0');
            temp = *s;
            *s   = '\0';
            snprintf(num, sizeof num, "%d", n);
            if (isnumstr(t))
                setsymtab(num, tostring(t), atof(t), STR | NUM, (cell **)ap->sval);
            else
                setsymtab(num, tostring(t), 0.0, STR, (cell **)ap->sval);
            *s = temp;
            if (*s != 0)
                s++;
        }
    else if (*s != 0)
        for (;;) {
            n++;
            t = s;
            while (*s != sep && *s != '\n' && *s != '\0')
                s++;
            temp = *s;
            *s   = '\0';
            snprintf(num, sizeof num, "%d", n);
            if (isnumstr(t))
                setsymtab(num, tostring(t), atof(t), STR | NUM, (cell **)ap->sval);
            else
                setsymtab(num, tostring(t), 0.0, STR, (cell **)ap->sval);
            *s = temp;
            if (*s++ == 0)
                break;
        }
    x            = gettemp();
    x.optr->tval = NUM;
    x.optr->fval = n;
    return x;
}

obj ifstat(node **a, int n)
{
    obj x;

    x = execute(a[0]);
    if (istrue(x)) {
        tempfree(x);
        x = execute(a[1]);
    } else if (a[2] != nullstat) {
        tempfree(x);
        x = execute(a[2]);
    }
    return x;
}

obj whilestat(node **a, int n)
{
    obj x;

    for (;;) {
        x = execute(a[0]);
        if (!istrue(x))
            return x;
        tempfree(x);
        x = execute(a[1]);
        if (isbreak(x))
            return awktrue;
        if (isnext(x) || isexit(x))
            return x;
        tempfree(x);
    }
}

obj forstat(node **a, int n)
{
    obj x;

    tempfree(execute(a[0]));
    for (;;) {
        if (a[1] != nullstat) {
            x = execute(a[1]);
            if (!istrue(x))
                return x;
            else
                tempfree(x);
        }
        x = execute(a[3]);
        if (isbreak(x)) // turn off break
            return awktrue;
        if (isnext(x) || isexit(x))
            return x;
        tempfree(x);
        tempfree(execute(a[2]));
    }
}

obj instat(node **a, int n)
{
    cell *vp, *arrayp, *cp, **tp;
    obj x;
    int i;

    vp     = (cell *)a[0];
    arrayp = (cell *)a[1];
    if (!(arrayp->tval & ARR))
        error(FATAL, "%s is not an array", arrayp->nval);
    tp = (cell **)arrayp->sval;
    for (i = 0; i < MAXSYM; i++) { // this routine knows too much
        for (cp = tp[i]; cp != NULL; cp = cp->nextval) {
            setsval(vp, cp->nval);
            x = execute(a[2]);
            if (isbreak(x))
                return awktrue;
            if (isnext(x) || isexit(x))
                return x;
            tempfree(x);
        }
    }
    return awktrue; // v7 fell off the end of a struct-returning function here
}

obj jump(node **a, int n)
{
    obj x, y;

    x.otype = OJUMP;
    x.osub  = 0;
    x.optr  = 0;
    switch (n) {
    default:
        error(FATAL, "illegal jump type %d", n);
        break;
    case EXIT:
        if (a[0] != 0) {
            y         = execute(a[0]);
            errorflag = getfval(y.optr);
        }
        x.osub = JEXIT;
        break;
    case NEXT:
        x.osub = JNEXT;
        break;
    case BREAK:
        x.osub = JBREAK;
        break;
    case CONTINUE:
        x.osub = JCONT;
        break;
    }
    return x;
}

obj fncn(node **a, int n)
{
    obj x;
    awkfloat u;
    int t;

    u = 0.0;
    t = (int)a[0];
    x = execute(a[1]);
    if (t == FLENGTH)
        u = (awkfloat)strlen(getsval(x.optr));
    else if (t == FLOG)
        u = log(getfval(x.optr));
    else if (t == FINT)
        u = trunc(getfval(x.optr)); // exact over the whole range, unlike (long)
    else if (t == FEXP)
        u = exp(getfval(x.optr));
    else if (t == FSQRT)
        u = sqrt(getfval(x.optr));
    else
        error(FATAL, "illegal function type %d", t);
    tempfree(x);
    x = gettemp();
    setfval(x.optr, u);
    return x;
}

obj print(node **a, int n)
{
    register node *x;
    obj y;
    char s[RECSIZE];
    char *t;
    int len;

    // v7 strcat'd into s[] and tested the length afterwards
    s[0] = '\0';
    len  = 0;
    for (x = a[0]; x != NULL; x = x->nnext) {
        y = execute(x);
        t = getsval(y.optr);
        while (*t) {
            if (len >= RECSIZE - 1)
                error(FATAL, "string %.20s ... too long to print", s);
            s[len++] = *t++;
        }
        tempfree(y);
        t = x->nnext == NULL ? *ORS : *OFS;
        while (*t) {
            if (len >= RECSIZE - 1)
                error(FATAL, "string %.20s ... too long to print", s);
            s[len++] = *t++;
        }
        s[len] = '\0';
    }
    if (a[1] == nullstat) {
        printf("%s", s);
        return awktrue;
    }
    redirprint(s, (int)a[1], a[2]);
    return awkfalse;
}

obj nullproc(node **a, int n)
{
    return awkfalse; // v7 returned nothing at all from a struct-returning function
}

obj nodetoobj(node *a)
{
    obj x;

    x.optr  = (cell *)a->nobj;
    x.otype = OCELL;
    x.osub  = a->subtype;
    if (isfld(x))
        fldbld();
    return x;
}

void redirprint(char *s, int a, node *b)
{
    register int i;
    obj x;

    x = execute(b);
    getsval(x.optr);
    for (i = 0; i < FILENUM; i++)
        if (files[i].fname && strcmp(x.optr->sval, files[i].fname) == 0)
            goto doit;
    for (i = 0; i < FILENUM; i++)
        if (files[i].fp == 0)
            break;
    if (i >= FILENUM)
        error(FATAL, "too many output files %d", i);
    if (a == '|') // a pipe!
        files[i].fp = popen(x.optr->sval, "w");
    else if (a == APPEND)
        files[i].fp = fopen(x.optr->sval, "a");
    else
        files[i].fp = fopen(x.optr->sval, "w");
    if (files[i].fp == NULL)
        error(FATAL, "can't open file %s", x.optr->sval);
    files[i].fname = tostring(x.optr->sval);
doit:
    fprintf(files[i].fp, "%s", s);
    fflush(files[i].fp); // in case someone is waiting for the output
    tempfree(x);
}
