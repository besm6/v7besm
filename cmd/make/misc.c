/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdlib.h>
#include <string.h>

#include "defs.h"

static struct nameblock *hashtab[HASHSIZE];
static int nhashed = 0;

// simple linear hash.  hash function is sum of characters mod hash table size.
static int hashloc(char *s)
{
    int i;
    int hashval;
    char *t;

    hashval = 0;

    for (t = s; *t != '\0'; ++t)
        hashval += *t;

    hashval %= HASHSIZE;

    for (i = hashval; hashtab[i] != 0 && unequal(s, hashtab[i]->namep); i = (i + 1) % HASHSIZE)
        ;

    return i;
}

struct nameblock *srchname(char *s)
{
    return hashtab[hashloc(s)];
}

static int hasslash(char *s)
{
    for (; *s; ++s)
        if (*s == '/')
            return YES;
    return NO;
}

struct nameblock *makename(char *s)
{
    struct nameblock *p;

    if (nhashed++ > HASHSIZE - 3)
        fatal("Hash table overflow");

    p               = ALLOC(nameblock);
    p->nxtnameblock = firstname;
    p->namep        = copys(s);
    p->linep        = 0;
    p->done         = 0;
    p->septype      = 0;
    p->modtime      = 0;

    firstname = p;
    if (mainname == NULL)
        if (s[0] != '.' || hasslash(s))
            mainname = p;

    hashtab[hashloc(s)] = p;

    return p;
}

// make a fresh copy of the string s
char *copys(char *s)
{
    char *t, *t0;

    if ((t = t0 = calloc(strlen(s) + 1, sizeof(char))) == NULL)
        fatal("out of memory");
    while ((*t++ = *s++))
        ;
    return t0;
}

// c = concatenation of a and b
char *concat(char *a, char *b, char *c)
{
    char *t;
    t = c;

    while ((*t = *a++))
        t++;
    while ((*t++ = *b++))
        ;
    return c;
}

// is b the suffix of a?  if so, set p = prefix
int suffix(char *a, char *b, char *p)
{
    char *a0, *b0;
    a0 = a;
    b0 = b;

    while (*a++)
        ;
    while (*b++)
        ;

    if ((a - a0) < (b - b0))
        return 0;

    while (b > b0)
        if (*--a != *--b)
            return 0;

    while (a0 < a)
        *p++ = *a0++;
    *p = '\0';

    return 1;
}

void *ckalloc(int n)
{
    void *p;

    if ((p = calloc(1, n)))
        return p;

    fatal("out of memory");
    return NULL; // fatal() exits
}

// copy string a into b, substituting for arguments.  bend is the last byte b may
// write; v7 had no bound here at all and no bound on vname either.
char *subst(char *a, char *b, char *bend)
{
    static int depth = 0;
    char *s;
    char vname[100];
    struct varblock *vbp;
    char closer;

    if (++depth > MAXSUBST)
        fatal("infinitely recursive macro?");
    if (a != 0)
        while (*a) {
            if (b >= bend)
                fatal("macro expansion too long");
            if (*a != '$')
                *b++ = *a++;
            else if (*++a == '\0' || *a == '$')
                *b++ = *a++;
            else {
                s = vname;
                if (*a == '(' || *a == '{') {
                    closer = (*a == '(' ? ')' : '}');
                    ++a;
                    while (*a == ' ')
                        ++a;
                    while (*a != ' ' && *a != closer && *a != '\0') {
                        if (s >= &vname[sizeof(vname) - 1])
                            fatal("macro name too long");
                        *s++ = *a++;
                    }
                    while (*a != closer && *a != '\0')
                        ++a;
                    if (*a == closer)
                        ++a;
                } else
                    *s++ = *a++;

                *s = '\0';
                if ((vbp = varptr(vname))->varval != 0) {
                    b         = subst(vbp->varval, b, bend);
                    vbp->used = YES;
                }
            }
        }

    *b = '\0';
    --depth;
    return b;
}

void setvar(char *v, char *s)
{
    struct varblock *p;

    p = varptr(v);
    if (p->noreset == 0) {
        p->varval  = s;
        p->noreset = inarglist;
        if (p->used && unequal(v, "@") && unequal(v, "*") && unequal(v, "<") && unequal(v, "?"))
            fprintf(stderr, "Warning: %s changed after being used\n", v);
    }
}

// look for arguments with equal signs but not colons
int eqsign(char *a)
{
    char *s, *t;

    while (*a == ' ')
        ++a;
    for (s = a; *s != '\0' && *s != ':'; ++s)
        if (*s == '=') {
            for (t = a; *t != '=' && *t != ' ' && *t != '\t'; ++t)
                ;
            *t = '\0';

            for (++s; *s == ' ' || *s == '\t'; ++s)
                ;
            setvar(a, copys(s));
            return YES;
        }

    return NO;
}

struct varblock *varptr(char *v)
{
    struct varblock *vp;

    for (vp = firstvar; vp; vp = vp->nxtvarblock)
        if (!unequal(v, vp->varname))
            return vp;

    vp              = ALLOC(varblock);
    vp->nxtvarblock = firstvar;
    firstvar        = vp;
    vp->varname     = copys(v);
    vp->varval      = 0;
    return vp;
}

// v7 passed sprintf()'s value straight to fatal(); C11's returns an int.
void fatal1(char *s, char *t)
{
    char buf[100];

    sprintf(buf, s, t);
    fatal(buf);
}

void fatal(char *s)
{
    if (s)
        fprintf(stderr, "Make: %s.  Stop.\n", s);
    else
        fprintf(stderr, "\nStop.\n");
    exit(1);
}

void yyerror(char *s)
{
    char buf[50];

    sprintf(buf, "line %d: %s", yylineno, s);
    fatal(buf);
}

struct chain *appendq(struct chain *head, char *tail)
{
    struct chain *p, *q;

    p        = ALLOC(chain);
    p->datap = tail;

    if (head) {
        for (q = head; q->nextp; q = q->nextp)
            ;
        q->nextp = p;
        return head;
    } else
        return p;
}

char *mkqlist(struct chain *p)
{
    char *qbufp, *s;
    static char qbuf[QBUFMAX];

    if (p == NULL) {
        qbuf[0] = '\0';
        return qbuf; // v7 returned nothing at all here
    }

    qbufp = qbuf;

    for (; p; p = p->nextp) {
        s = p->datap;
        if (qbufp + strlen(s) > &qbuf[QBUFMAX - 3]) {
            fprintf(stderr, "$? list too long\n");
            break;
        }
        while (*s)
            *qbufp++ = *s++;
        *qbufp++ = ' ';
    }
    // v7 wrote *--qbufp unconditionally, forming a pointer below the array when
    // the first name already overflowed.
    if (qbufp > qbuf)
        --qbufp;
    *qbufp = '\0';
    return qbuf;
}
