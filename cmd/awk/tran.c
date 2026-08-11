// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>
#include <string.h>

#include "awk.h"
#include "y.tab.h"

cell *symtab[MAXSYM]; // symbol table pointers

char **FS;       // initial field sep
char **RS;       // initial record sep
char **OFS;      // output field sep
char **ORS;      // output record sep
char **OFMT;     // output format for numbers
awkfloat *NF;    // number of fields in current record
awkfloat *NR;    // number of current record
char **FILENAME; // current filename argument

cell *recloc; // location of record
cell *nrloc;  // NR
cell *nfloc;  // NF

void syminit(void)
{
    setsymtab("0", tostring("0"), 0.0, NUM | STR | CON | FLD, symtab);
    // this one is used for if(x)... tests:
    setsymtab("$zero&null", tostring(""), 0.0, NUM | STR | CON | FLD, symtab);
    recloc   = setsymtab("$record", record, 0.0, STR | FLD, symtab);
    FS       = &setsymtab("FS", tostring(" "), 0.0, STR | FLD, symtab)->sval;
    RS       = &setsymtab("RS", tostring("\n"), 0.0, STR | FLD, symtab)->sval;
    OFS      = &setsymtab("OFS", tostring(" "), 0.0, STR | FLD, symtab)->sval;
    ORS      = &setsymtab("ORS", tostring("\n"), 0.0, STR | FLD, symtab)->sval;
    OFMT     = &setsymtab("OFMT", tostring("%.6g"), 0.0, STR | FLD, symtab)->sval;
    FILENAME = &setsymtab("FILENAME", NULL, 0.0, STR | FLD, symtab)->sval;
    nfloc    = setsymtab("NF", NULL, 0.0, NUM, symtab);
    NF       = &nfloc->fval;
    nrloc    = setsymtab("NR", NULL, 0.0, NUM, symtab);
    NR       = &nrloc->fval;
}

cell **makesymtab(void)
{
    int i;
    cell **cp;

    cp = (cell **)malloc(MAXSYM * sizeof(cell *));
    if (cp == NULL)
        error(FATAL, "out of space in makesymtab");
    for (i = 0; i < MAXSYM; i++)
        cp[i] = 0;
    return cp;
}

void freesymtab(cell *ap) // free symbol table
{
    cell *cp, *next, **tp;
    int i;

    if (!(ap->tval & ARR))
        return;
    tp = (cell **)ap->sval;
    for (i = 0; i < MAXSYM; i++) {
        for (cp = tp[i]; cp != NULL; cp = next) {
            next = cp->nextval; // v7 read cp->nextval after free(cp)
            xfree(cp->nval);
            xfree(cp->sval);
            free(cp);
        }
        tp[i] = NULL;
    }
    xfree(tp);
}

cell *setsymtab(const char *n, char *s, awkfloat f, unsigned t, cell **tab)
{
    register int h;
    register cell *p;

    if (n != NULL && (p = lookup(n, tab, 0)) != NULL) {
        xfree(s);
        return p;
    }
    p = (cell *)malloc(sizeof(cell));
    if (p == NULL)
        error(FATAL, "symbol table overflow at %s", n);
    p->nval    = tostring(n);
    p->sval    = s;
    p->fval    = f;
    p->tval    = t;
    h          = hash(n);
    p->nextval = tab[h];
    tab[h]     = p;
    return p;
}

int hash(const char *s) // form hash value for string s
{
    register int hashval;

    for (hashval = 0; *s != '\0';)
        hashval += *s++;
    return hashval % MAXSYM;
}

cell *lookup(const char *s, cell **tab, unsigned flag) // look for s in tab, flag must match
{
    register cell *p;

    for (p = tab[hash(s)]; p != NULL; p = p->nextval)
        if (strcmp(s, p->nval) == 0 && (flag == 0 || flag == p->tval))
            return p; // found it
    return NULL;      // not found
}

awkfloat setfval(cell *vp, awkfloat f)
{
    checkval(vp);
    if (vp == recloc)
        error(FATAL, "can't set $0");
    vp->tval &= ~STR; // mark string invalid
    vp->tval |= NUM;  // mark number ok
    if ((vp->tval & FLD) && vp->nval == 0)
        donerec = 0;
    return vp->fval = f;
}

char *setsval(cell *vp, char *s)
{
    checkval(vp);
    if (vp == recloc)
        error(FATAL, "can't set $0");
    vp->tval &= ~NUM;
    vp->tval |= STR;
    if ((vp->tval & FLD) && vp->nval == 0)
        donerec = 0;
    if (!(vp->tval & FLD))
        xfree(vp->sval);
    vp->tval &= ~FLD;
    return vp->sval = tostring(s);
}

awkfloat getfval(cell *vp)
{
    if (vp->sval == record && donerec == 0)
        recbld();
    checkval(vp);
    if ((vp->tval & NUM) == 0) {
        // The problem is to make non-numeric things have unlikely numeric values, so that
        // $1 == $2 sort of makes sense when one or the other is numeric.
        if (isnumstr(vp->sval)) {
            vp->fval = atof(vp->sval);
            if (!(vp->tval & CON)) // don't change type of a constant
                vp->tval |= NUM;
        } else
            vp->fval = 0.0; // not a very good idea
    }
    return vp->fval;
}

char *getsval(cell *vp)
{
    char s[100];

    if (vp->sval == record && donerec == 0)
        recbld();
    checkval(vp);
    if ((vp->tval & STR) == 0) {
        if (!(vp->tval & FLD))
            xfree(vp->sval);
        // v7 printed an integral value with %.20g.  doprnt clamps a float to 12
        // significant digits, and (long) is undefined past 2^40.
        if (vp->fval > -TWO40 && vp->fval < TWO40 && (long)vp->fval == vp->fval)
            snprintf(s, sizeof s, "%d", (long)vp->fval);
        else
            snprintf(s, sizeof s, *OFMT, vp->fval); // OFMT is the user's
        vp->sval = tostring(s);
        vp->tval &= ~FLD;
        vp->tval |= STR;
    }
    return vp->sval;
}

void checkval(cell *vp)
{
    if (vp->tval & ARR)
        error(FATAL, "illegal reference to array %s", vp->nval);
    if ((vp->tval & (NUM | STR)) == 0)
        error(FATAL, "funny variable %s: type %o", vp->nval ? vp->nval : "(temp)", vp->tval);
}

char *tostring(const char *s)
{
    register char *p;

    p = malloc(strlen(s) + 1);
    if (p == NULL)
        error(FATAL, "out of space in tostring on %s", s);
    strcpy(p, s);
    return p;
}
