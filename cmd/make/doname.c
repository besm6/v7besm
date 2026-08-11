/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

#include <stdlib.h>
#include <string.h>

#include "defs.h"

//  BASIC PROCEDURE.  RECURSIVE.
//
// p->done = 0   don't know what to do yet
// p->done = 1   file in process of being updated
// p->done = 2   file already exists in current state
// p->done = 3   file make failed

static int docom1(char *comstring, int nohalt, int noprint);

// Find a suffix rule that makes p.  Returns the source file's name block and
// sets *rulep to the rule's, or NULL if there is none; prefix and sourcename are
// the caller's buffers.  Split out of doname() so that its frame -- and
// srchdir()'s under it -- is gone before doname() recurses.  README.md.
static struct nameblock *implicit(struct nameblock *p, char *prefix, char *sourcename,
                                  struct nameblock **rulep)
{
    struct lineblock *lp, *lp1;
    struct depblock *suffp, *suffp1;
    struct nameblock *p1, *p2;
    char *pnamep, *p1namep;
    char temp[100], concsuff[20];

    for (lp = sufflist; lp; lp = lp->nxtlineblock)
        for (suffp = lp->depp; suffp; suffp = suffp->nxtdepblock) {
            pnamep = suffp->depname->namep;
            if (suffix(p->namep, pnamep, prefix)) {
                srchdir(concat(prefix, "*", temp), NO, (struct depblock *)NULL);
                for (lp1 = sufflist; lp1; lp1 = lp1->nxtlineblock)
                    for (suffp1 = lp1->depp; suffp1; suffp1 = suffp1->nxtdepblock) {
                        p1namep = suffp1->depname->namep;
                        if ((p1 = srchname(concat(p1namep, pnamep, concsuff))) &&
                            (p2 = srchname(concat(prefix, p1namep, sourcename)))) {
                            *rulep = p1;
                            return p2;
                        }
                    }
            }
        }
    return NULL;
}

// Run the commands that bring p up to date and return its new time.  Split out
// of doname() for the same reason implicit() is: nothing here recurses, so these
// words need not be on the stack once per level of the graph.
static TIMETYPE makeit(struct nameblock *p, TIMETYPE tdep, struct shblock *explcom,
                       struct shblock *implcom, struct chain *qchain, int *errstatp)
{
    struct nameblock *p1;
    struct lineblock *lp2;
    TIMETYPE ptime;

    ptime = (tdep > 0 ? tdep : prestime());
    setvar("@", p->namep);
    setvar("?", mkqlist(qchain));
    if (explcom)
        *errstatp += docom(explcom);
    else if (implcom)
        *errstatp += docom(implcom);
    else if (p->septype == 0) {
        if ((p1 = srchname(".DEFAULT"))) {
            setvar("<", p->namep);
            for (lp2 = p1->linep; lp2; lp2 = lp2->nxtlineblock)
                if ((implcom = lp2->shp)) {
                    *errstatp += docom(implcom);
                    break;
                }
        } else if (keepgoing) {
            printf("Don't know how to make %s\n", p->namep);
            ++*errstatp;
        } else
            fatal1(" Don't know how to make %s", p->namep);
    }

    setvar("@", (char *)NULL);
    if (noexflag || (ptime = exists(p->namep)) == 0)
        ptime = prestime();
    return ptime;
}

int doname(struct nameblock *p, int reclevel, TIMETYPE *tval)
{
    int errstat;
    int okdel1;
    int didwork;
    TIMETYPE td, td1, tdep, ptime, ptime1;
    struct depblock *q;
    struct depblock *qtemp;
    struct nameblock *p1, *p2;
    struct shblock *implcom, *explcom;
    struct lineblock *lp;
    struct lineblock *lp2;
    char sourcename[100], prefix[100];
    struct chain *qchain;

    if (p == 0) {
        *tval = 0;
        return 0;
    }

    // The graph decides how deep this goes, and the stack is 4,096 words.
    // README.md, "The stack".
    if (reclevel > MAXLEVEL)
        fatal1("Dependency graph too deep at `%s'", p->namep);

    if (dbgflag) {
        printf("doname(%s,%d)\n", p->namep, reclevel);
        fflush(stdout);
    }

    if (p->done > 0) {
        *tval = p->modtime;
        return p->done == 3;
    }

    errstat = 0;
    tdep    = 0;
    implcom = 0;
    explcom = 0;
    ptime   = exists(p->namep);
    ptime1  = 0;
    didwork = NO;
    p->done = 1; // avoid infinite loops

    qchain = NULL;

    // Expand any names that have embedded metacharaters
    for (lp = p->linep; lp; lp = lp->nxtlineblock)
        for (q = lp->depp; q; q = qtemp) {
            qtemp = q->nxtdepblock;
            expand(q);
        }

    // make sure all dependents are up to date
    for (lp = p->linep; lp; lp = lp->nxtlineblock) {
        td = 0;
        for (q = lp->depp; q; q = q->nxtdepblock) {
            errstat += doname(q->depname, reclevel + 1, &td1);
            if (dbgflag)
                printf("TIME(%s)=%ld\n", q->depname->namep, td1);
            if (td1 > td)
                td = td1;
            if (ptime < td1)
                qchain = appendq(qchain, q->depname->namep);
        }
        if (p->septype == SOMEDEPS) {
            if (lp->shp != 0)
                if (ptime < td || (ptime == 0 && td == 0) || lp->depp == 0) {
                    okdel1 = okdel;
                    okdel  = NO;
                    setvar("@", p->namep);
                    setvar("?", mkqlist(qchain));
                    qchain = NULL;
                    if (!questflag)
                        errstat += docom(lp->shp);
                    setvar("@", (char *)NULL);
                    okdel   = okdel1;
                    ptime1  = prestime();
                    didwork = YES;
                }
        }

        else {
            if (lp->shp != 0) {
                if (explcom)
                    fprintf(stderr, "Too many command lines for `%s'\n", p->namep);
                else
                    explcom = lp->shp;
            }

            if (td > tdep)
                tdep = td;
        }
    }

    // Look for implicit dependents, using suffix rules
    p1 = NULL;
    if ((p2 = implicit(p, prefix, sourcename, &p1)) != NULL) {
        errstat += doname(p2, reclevel + 1, &td);
        if (ptime < td)
            qchain = appendq(qchain, p2->namep);
        if (dbgflag)
            printf("TIME(%s)=%ld\n", p2->namep, td);
        if (td > tdep)
            tdep = td;
        setvar("*", prefix);
        setvar("<", copys(sourcename));
        for (lp2 = p1->linep; lp2; lp2 = lp2->nxtlineblock)
            if ((implcom = lp2->shp))
                break;
    }

    if (errstat == 0 && (ptime < tdep || (ptime == 0 && tdep == 0)))
        ptime = makeit(p, tdep, explcom, implcom, qchain, &errstat);

    else if (errstat != 0 && reclevel == 0)
        printf("`%s' not remade because of errors\n", p->namep);

    else if (!questflag && reclevel == 0 && didwork == NO)
        printf("`%s' is up to date.\n", p->namep);

    if (questflag && reclevel == 0)
        exit(ndocoms > 0 ? -1 : 0);

    p->done = (errstat ? 3 : 2);
    if (ptime1 > ptime)
        ptime = ptime1;
    p->modtime = ptime;
    *tval      = ptime;
    return errstat;
}

int docom(struct shblock *q)
{
    char *s;
    int ign, nopr;
    // At file scope, not on the stack: OUTMAX is 417 words and only one docom()
    // is ever live -- doname()'s recursion is done before it runs a command.
    static char string[OUTMAX];

    ++ndocoms;
    if (questflag)
        return NO;

    if (touchflag) {
        s = varptr("@")->varval;
        if (!silflag)
            printf("touch(%s)\n", s);
        if (!noexflag)
            touch(YES, s);
    }

    else
        for (; q; q = q->nxtshblock) {
            subst(q->shbp, string, string + OUTMAX - 1);

            ign  = ignerr;
            nopr = NO;
            for (s = string; *s == '-' || *s == '@'; ++s)
                if (*s == '-')
                    ign = YES;
                else
                    nopr = YES;

            if (docom1(s, ign, nopr) && !ign) {
                if (keepgoing)
                    return YES;
                else
                    fatal((char *)NULL);
            }
        }
    return NO;
}

static int docom1(char *comstring, int nohalt, int noprint)
{
    int status;

    if (comstring[0] == '\0')
        return 0;

    if (!silflag && (!noprint || noexflag)) {
        printf("%s%s\n", (noexflag ? "" : prompt), comstring);
        fflush(stdout);
    }

    if (noexflag)
        return 0;

    if ((status = dosys(comstring, nohalt))) {
        if (status >> 8)
            printf("*** Error code %d", status >> 8);
        else
            printf("*** Termination code %d", status);

        if (nohalt)
            printf(" (ignored)\n");
        else
            printf("\n");
        fflush(stdout);
    }

    return status;
}

// If there are any Shell meta characters in the name,
// expand into a list, after searching directory
void expand(struct depblock *q)
{
    char *s;
    char *s1;
    struct depblock *p;

    s1 = q->depname->namep;
    for (s = s1;;)
        switch (*s++) {
        case '\0':
            return;

        case '*':
        case '?':
        case '[':
            if ((p = srchdir(s1, YES, q->nxtdepblock))) {
                q->nxtdepblock = p;
                q->depname     = 0;
            }
            return;
        }
}
