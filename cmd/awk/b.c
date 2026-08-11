// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

#include <stdio.h>

#include "awk.h"
#include "y.tab.h"

// A size profile, as cmd/cpp, cmd/as and cmd/lex carry: v7 had 256 leaves and 256 states,
// and each word of them is a word of heap this program does not get.  README.md, "The
// heap is the ceiling".
#define MAXLIN  112
#define NCHARS  257 // 256 byte values, plus HAT
#define NSTATES 112

// Tree nodes, which MAXLIN does not bound: it counts leaves, and `a****' is one leaf under
// a chain of STARs.  The walks below are iterative and this sizes their stacks.
#define MAXNODE 168

#define type(v)   v->nobj
#define left(v)   v->narg[0]
#define right(v)  v->narg[1]
#define parent(v) v->nnext

// Encoding in tree nodes:
//   leaf (CCL, NCCL, CHAR, DOT): left is index, right holds the value or a pointer to it
//   unary (FINAL, STAR, PLUS, QUEST): left is child, right is null
//   binary (CAT, OR): left and right are children
//   parent contains pointer to parent

struct fa {
    int cch;
    struct fa *st;
};

static int *state[NSTATES];
static int *foll[MAXLIN];
static char follown[MAXLIN]; // 1 where foll[i] is this leaf's own, 0 where it is shared
static char chars[MAXLIN];
static int setvec[MAXLIN];
static node *point[MAXLIN];

static int setcnt;
static int line;

// A character class is the one value in this program that is a fat char *, and neither an
// int YYSTYPE nor a node * can carry one.  The tree holds an index into this instead.
// README.md, "One fat pointer".
#define MAXCCL 16
static char *cclstr[MAXCCL];
static int ncclstr;

int cclstash(char *s) // takes ownership of s
{
    if (ncclstr >= MAXCCL)
        error(FATAL, "too many character classes");
    cclstr[ncclstr] = s;
    return ncclstr++;
}

char *cclget(int h)
{
    return cclstr[h];
}

// penter(), freetr() and cfoll() run one after another and never overlap, so they share
// one worklist.  first() runs inside cfoll() and has its own.
static node *walk[MAXNODE];

static void push(node *p, int *sp)
{
    if (*sp >= MAXNODE)
        overflo();
    walk[(*sp)++] = p;
}

struct fa *makedfa(node *p) // returns dfa for tree pointed to by p
{
    node *p1;
    struct fa *fap;
    int i;

    // put DOT STAR in front of the regular expression
    p1 = op2(CAT, op2(STAR, op2(DOT, (node *)0, (node *)0), (node *)0), p);
    p1 = op2(FINAL, p1, (node *)0); // install FINAL node

    line = 0;
    penter(p1);       // enter parent pointers and leaf indices
    point[line] = p1; // FINAL node
    setvec[0]   = 1;  // for initial DOT STAR
    cfoll(p1);        // set up follow sets
    fap = cgotofn();
    freetr(p1);
    // The follow sets, once.  v7 freed them from freetr() one leaf at a time, which frees
    // a shared set as many times as it is shared -- and cgotofn() has already freed
    // state[0], which is foll[0].
    for (i = 0; i < line; i++) {
        if (follown[i])
            free(foll[i]);
        foll[i]    = NULL;
        follown[i] = 0;
    }
    return fap;
}

// Left to right, so the leaf indices come out in v7's order.
void penter(node *p) // set up parent pointers and leaf indices
{
    int sp = 0;

    push(p, &sp);
    while (sp > 0) {
        p = walk[--sp];
        switch (type(p)) {
        case CCL:
        case NCCL:
        case CHAR:
        case DOT:
            if (line >= MAXLIN - 1) // v7 had no bound here at all
                overflo();
            left(p)       = (node *)line;
            point[line++] = p;
            break;
        case FINAL:
        case STAR:
        case PLUS:
        case QUEST:
            parent(left(p)) = p;
            push(left(p), &sp);
            break;
        case CAT:
        case OR:
            parent(left(p))  = p;
            parent(right(p)) = p;
            push(right(p), &sp);
            push(left(p), &sp);
            break;
        default:
            error(FATAL, "unknown type %d in penter", type(p));
            break;
        }
    }
}

void freetr(node *p) // free the parse tree
{
    node *l, *r;
    int sp = 0;

    push(p, &sp);
    while (sp > 0) {
        p = walk[--sp];
        switch (type(p)) {
        case CCL:
        case NCCL:
        case CHAR:
        case DOT:
            xfree(p);
            break;
        case FINAL:
        case STAR:
        case PLUS:
        case QUEST:
            l = left(p);
            xfree(p);
            push(l, &sp);
            break;
        case CAT:
        case OR:
            l = left(p);
            r = right(p);
            xfree(p);
            push(r, &sp);
            push(l, &sp);
            break;
        default:
            error(FATAL, "unknown type %d in freetr", type(p));
            break;
        }
    }
}

int cclenter(int h)
{
    register int i, c;
    register char *p = cclstr[h];
    char *op;

    op = p;
    i  = 0;
    while ((c = *p++) != 0) {
        if (c == '-' && i > 0 && chars[i - 1] != 0) {
            if (*p != 0) {
                c = chars[i - 1];
                while (c < *p) {
                    if (i >= MAXLIN - 1)
                        overflo();
                    chars[i++] = ++c;
                }
                p++;
                continue;
            }
        }
        if (i >= MAXLIN - 1) // v7 left no room for the terminator below
            overflo();
        chars[i++] = c;
    }
    chars[i++] = '\0';
    xfree(op);
    cclstr[h] = tostring(chars);
    return h;
}

void overflo(void)
{
    error(FATAL, "regular expression too long");
}

// enter the follow set of each leaf of vertex v into foll[leaf]; left to right, because
// notin() only looks at the leaves with lower indices
void cfoll(node *v)
{
    register int i;
    int prev, sp = 0;

    push(v, &sp);
    while (sp > 0) {
        v = walk[--sp];
        switch (type(v)) {
        case CCL:
        case NCCL:
        case CHAR:
        case DOT:
            setcnt = 0;
            for (i = 1; i <= line; i++)
                setvec[i] = 0;
            follow(v);
            if (notin(foll, ((int)left(v)) - 1, &prev)) {
                foll[(int)left(v)]    = add(setcnt);
                follown[(int)left(v)] = 1;
            } else
                foll[(int)left(v)] = foll[prev];
            break;
        case FINAL:
        case STAR:
        case PLUS:
        case QUEST:
            push(left(v), &sp);
            break;
        case CAT:
        case OR:
            push(right(v), &sp);
            push(left(v), &sp);
            break;
        default:
            error(FATAL, "unknown type %d in cfoll", type(v));
        }
    }
}

// The node being visited and how far it has got: 0 = not entered, 1 = the first child
// answered, 2 = the second did.  fsval carries OR's first answer, v7's `b'.
static node *fsnode[MAXNODE];
static char fsphase[MAXNODE];
static char fsval[MAXNODE];

// collects initially active leaves of p into setvec; returns 0 or 1 depending on
// whether p matches the empty string
int first(node *p)
{
    int sp = 0, res = 0;

    fsnode[0]  = p;
    fsphase[0] = 0;
    for (;;) {
        p = fsnode[sp];
        switch (fsphase[sp]) {
        case 0:
            switch (type(p)) {
            case CCL:
            case NCCL:
            case CHAR:
            case DOT:
                if (setvec[(int)left(p)] != 1) {
                    setvec[(int)left(p)] = 1;
                    setcnt++;
                }
                // an empty CCL matches the empty string
                res = (type(p) == CCL && *cclget((int)right(p)) == '\0') ? 0 : 1;
                break;
            case FINAL:
            case PLUS:
            case STAR:
            case QUEST:
            case CAT:
            case OR:
                fsphase[sp] = 1;
                if (++sp >= MAXNODE)
                    overflo();
                // OR visits its right child first, as v7 did; everything else the left
                fsnode[sp] =
                    type(fsnode[sp - 1]) == OR ? right(fsnode[sp - 1]) : left(fsnode[sp - 1]);
                fsphase[sp] = 0;
                continue;
            default:
                error(FATAL, "unknown type %d in first", type(p));
            }
            break;

        case 1:
            switch (type(p)) {
            case FINAL:
            case PLUS:
                res = res != 0;
                break;
            case STAR:
            case QUEST:
                res = 0;
                break;
            case CAT:
                // v7's `&&': a left child that is not nullable answers for the node and
                // the right child is never visited
                if (res != 0) {
                    res = 1;
                    break;
                }
                fsphase[sp] = 2;
                sp++;
                fsnode[sp]  = right(p);
                fsphase[sp] = 0;
                continue;
            default: // OR, whose children are both always visited
                fsval[sp]   = res;
                fsphase[sp] = 2;
                sp++;
                fsnode[sp]  = left(p);
                fsphase[sp] = 0;
                continue;
            }
            break;

        default: // phase 2
            if (type(p) == CAT)
                res = res != 0;
            else
                res = !(res == 0 || fsval[sp] == 0);
            break;
        }
        if (sp == 0)
            return res;
        sp--;
    }
}

// collects leaves that can follow v into setvec.  Every arm of v7's recursion was a tail
// call up the parent chain, so this is a loop and needs no stack.
void follow(node *v)
{
    node *p;

    for (;;) {
        if (type(v) == FINAL)
            return;
        p = parent(v);
        switch (type(p)) {
        case STAR:
        case PLUS:
            first(v);
            v = p;
            continue;

        case OR:
        case QUEST:
            v = p;
            continue;

        case CAT:
            if (v == left(p)) { // v is left child of p
                if (first(right(p)) != 0)
                    return;
            }
            v = p;
            continue;

        case FINAL:
            if (setvec[line] != 1) {
                setvec[line] = 1;
                setcnt++;
            }
            return;

        default:
            return;
        }
    }
}

int member(int c, char *s) // is c in s?
{
    while (*s)
        if (c == *s++)
            return 1;
    return 0;
}

int notin(int **arr, int n, int *prev) // is setvec in arr[0] thru arr[n]?
{
    register int i, j;
    int *ptr;

    for (i = 0; i <= n; i++) {
        ptr = arr[i];
        if (*ptr == setcnt) {
            for (j = 0; j < setcnt; j++)
                if (setvec[*(++ptr)] != 1)
                    goto nxt;
            *prev = i;
            return 0;
        }
    nxt:;
    }
    return 1;
}

int *add(int n) // remember setvec
{
    int *ptr, *p;
    register int i;

    if ((p = ptr = (int *)malloc((n + 1) * sizeof(int))) == NULL)
        overflo();
    *ptr = n;
    for (i = 1; i <= line; i++)
        if (setvec[i] == 1)
            *(++ptr) = i;
    return p;
}

// cgotofn()'s tables.  v7 made them automatic, which is a 1,488-word frame in a 4,096-word
// stack; it does not recurse, so file scope costs nothing but bss.
static struct fa *where[NSTATES];
static int fatab[2 * NCHARS + 1];
static char inset[MAXLIN]; // v7 called this index, which is a libc name
static char iposns[MAXLIN];
static int sposns[MAXLIN];
static char symbol[NCHARS];
static char isyms[NCHARS];
static int ssyms[NCHARS];

struct fa *cgotofn(void)
{
    register int i, k;
    register int *ptr;
    int c;
    char *p;
    node *cp;
    int j, n, s, ind, numtrans;
    int finflg;
    int curpos, num, prev;
    struct fa *pfa;
    int spmax, spinit;
    int ssmax, ssinit;

    for (i = 0; i <= line; i++)
        inset[i] = iposns[i] = setvec[i] = 0;
    for (i = 0; i < NCHARS; i++)
        isyms[i] = symbol[i] = 0;
    setcnt = 0;
    // compute initial positions and symbols of state 0
    ssmax = 0;
    ptr = state[0] = foll[0];
    spinit         = *ptr;
    for (i = 0; i < spinit; i++) {
        curpos         = *(++ptr);
        sposns[i]      = curpos;
        iposns[curpos] = 1;
        cp             = point[curpos];
        switch (type(cp)) {
        case CHAR:
            k = (int)right(cp);
            if (isyms[k] != 1) {
                isyms[k]       = 1;
                ssyms[ssmax++] = k;
            }
            break;
        case DOT:
            for (k = 1; k < NCHARS; k++) {
                if (k != HAT) {
                    if (isyms[k] != 1) {
                        isyms[k]       = 1;
                        ssyms[ssmax++] = k;
                    }
                }
            }
            break;
        case CCL:
            for (p = cclget((int)right(cp)); *p; p++) {
                if (*p != HAT) {
                    if (isyms[(int)*p] != 1) {
                        isyms[(int)*p] = 1;
                        ssyms[ssmax++] = *p;
                    }
                }
            }
            break;
        case NCCL:
            for (k = 1; k < NCHARS; k++) {
                if (k != HAT && !member(k, cclget((int)right(cp)))) {
                    if (isyms[k] != 1) {
                        isyms[k]       = 1;
                        ssyms[ssmax++] = k;
                    }
                }
            }
        }
    }
    ssinit = ssmax;
    n      = 0;
    for (s = 0; s <= n; s++) {
        ind      = 0;
        numtrans = 0;
        finflg   = 0;
        if (*(state[s] + *state[s]) == line) { // s final?
            finflg = 1;
            goto tenter;
        }
        spmax = spinit;
        ssmax = ssinit;
        ptr   = state[s];
        num   = *ptr;
        for (i = 0; i < num; i++) {
            curpos = *(++ptr);
            if (iposns[curpos] != 1 && inset[curpos] != 1) {
                inset[curpos]   = 1;
                sposns[spmax++] = curpos;
            }
            cp = point[curpos];
            switch (type(cp)) {
            case CHAR:
                k = (int)right(cp);
                if (isyms[k] == 0 && symbol[k] == 0) {
                    symbol[k]      = 1;
                    ssyms[ssmax++] = k;
                }
                break;
            case DOT:
                for (k = 1; k < NCHARS; k++) {
                    if (k != HAT) {
                        if (isyms[k] == 0 && symbol[k] == 0) {
                            symbol[k]      = 1;
                            ssyms[ssmax++] = k;
                        }
                    }
                }
                break;
            case CCL:
                for (p = cclget((int)right(cp)); *p; p++) {
                    if (*p != HAT) {
                        if (isyms[(int)*p] == 0 && symbol[(int)*p] == 0) {
                            symbol[(int)*p] = 1;
                            ssyms[ssmax++]  = *p;
                        }
                    }
                }
                break;
            case NCCL:
                for (k = 1; k < NCHARS; k++) {
                    if (k != HAT && !member(k, cclget((int)right(cp)))) {
                        if (isyms[k] == 0 && symbol[k] == 0) {
                            symbol[k]      = 1;
                            ssyms[ssmax++] = k;
                        }
                    }
                }
            }
        }
        for (j = 0; j < ssmax; j++) { // nextstate(s, ssyms[j])
            c         = ssyms[j];
            symbol[c] = 0;
            setcnt    = 0;
            for (k = 0; k <= line; k++)
                setvec[k] = 0;
            for (i = 0; i < spmax; i++) {
                inset[sposns[i]] = 0;
                cp               = point[sposns[i]];
                if ((k = type(cp)) != FINAL)
                    if ((k == CHAR && c == (int)right(cp)) || k == DOT ||
                        (k == CCL && member(c, cclget((int)right(cp)))) ||
                        (k == NCCL && !member(c, cclget((int)right(cp))))) {
                        ptr = foll[sposns[i]];
                        num = *ptr;
                        for (k = 0; k < num; k++) {
                            if (setvec[*(++ptr)] != 1 && iposns[*ptr] != 1) {
                                setvec[*ptr] = 1;
                                setcnt++;
                            }
                        }
                    }
            } // end nextstate
            if (notin(state, n, &prev)) {
                if (n >= NSTATES - 1) // v7 wrote state[NSTATES]
                    overflo();
                state[++n]   = add(setcnt);
                fatab[++ind] = c;
                fatab[++ind] = n;
                numtrans++;
            } else {
                if (prev != 0) {
                    fatab[++ind] = c;
                    fatab[++ind] = prev;
                    numtrans++;
                }
            }
        }
    tenter:
        if ((pfa = (struct fa *)malloc((numtrans + 1) * sizeof(struct fa))) == NULL)
            overflo();
        where[s] = pfa;
        if (finflg)
            pfa->cch = -1; // s is a final state
        else
            pfa->cch = numtrans;
        pfa->st = 0;
        for (i = 1, pfa += 1; i <= numtrans; i++, pfa++) {
            pfa->cch = fatab[2 * i - 1];
            pfa->st  = (struct fa *)fatab[2 * i];
        }
    }
    // from 1: state[0] is foll[0], which makedfa() owns
    for (i = 1; i <= n; i++)
        xfree(state[i]);
    for (i = 0; i <= n; i++) {
        pfa     = where[i];
        pfa->st = where[0];
        for (k = 1; k <= pfa->cch; k++)
            (pfa + k)->st = where[(int)(pfa + k)->st];
    }
    pfa = where[0];
    if ((num = pfa->cch) < 0)
        return where[0];
    for (pfa += num; num; num--, pfa--)
        if (pfa->cch == HAT)
            return pfa->st;
    return where[0];
}

int match(struct fa *pfa, char *p)
{
    register int count;
    int c;

    if (p == 0)
        return 0;
    if (pfa->cch == 1) { // fast test for first character, if possible
        c = (++pfa)->cch;
        do
            if (c == *p) {
                p++;
                pfa = pfa->st;
                goto adv;
            }
        while (*p++ != 0);
        return 0;
    }
adv:
    if ((count = pfa->cch) < 0)
        return 1;
    do {
        for (pfa += count; count; count--, pfa--)
            if (pfa->cch == *p)
                break;
        pfa = pfa->st;
        if ((count = pfa->cch) < 0)
            return 1;
    } while (*p++ != 0);
    return 0;
}
