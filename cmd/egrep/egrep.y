/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * egrep -- print lines containing (or not containing) a regular expression
 *
 *	status returns:
 *		0 - ok, and some matches
 *		1 - ok, but no matches
 *		2 - some error
 *
 * Task C26.  Five changes, all argued in README.md beside this file:
 *
 *	the alphabet is 256 symbols, closing an out-of-bounds read of a
 *		transition row and an out-of-bounds store into a stack frame;
 *	-b is a byte offset of the start of the line, as grep's and fgrep's are;
 *	-f works at all, a char here being unsigned;
 *	cfoll(), cstate() and follow() are iterative, their depth being the
 *		length of the pattern;
 *	two bound tests were off by one.
 */
%token CHAR DOT CCL NCCL OR CAT STAR PLUS QUEST
%left OR
%left CHAR DOT CCL NCCL '('
%left CAT
%left STAR PLUS QUEST

%{
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAXLIN  350
#define MAXPOS  4000

// 256, not v7's 128; NSTATES stays 128, gotofn costing NSTATES*NCHARS bytes.
#define NCHARS  256
#define NSTATES 128
#define FINAL   -1

// cgotofn() tells a literal byte in name[] from a grammar token by `c < NCHARS', and
// b6yacc numbers tokens from 0401 up, so 256 is the widest alphabet that can work.
_Static_assert(NCHARS <= DOT && NCHARS <= CCL && NCHARS <= NCCL,
               "NCHARS has reached b6yacc's token numbers");
_Static_assert(NSTATES <= 256, "a state number is stored in a char");

static char gotofn[NSTATES][NCHARS];
static int state[NSTATES];
static char out[NSTATES];
static int line = 1;
static int name[MAXLIN];
static int left[MAXLIN];
static int right[MAXLIN];
static int parent[MAXLIN];
static int foll[MAXLIN];
static int positions[MAXPOS];

// Each class is a count followed by that many members.  int, not v7's char, whose
// count truncated at 256 where MAXLIN admits 350.
static int chars[MAXLIN];
static int nxtpos;
static int nxtchar = 0;
static int tmpstat[MAXLIN];
static int initstat[MAXLIN];
static int xstate;
static int count;
static int icount;
static char *input;

static int lnum;
static int bflag;
static int cflag;
static int fflag;
static int lflag;
static int nflag;
static int hflag = 1;
static int sflag;
static int vflag;
static int nfile;
static int tln;
static int nsucc;

// The byte offset of *p, and of the line nlp starts -- what -b prints.  v7 kept
// blkno and printed (blkno-ccount-1)/512, a block number of the line's END.
static int coff;
static int loff;

static int f;
static char *fname;

static int nextch(void);
static void synerror(void);
static int enter(int x);
static int cclenter(int x);
static int node(int x, int l, int r);
static int unary(int x, int d);
static void overflo(void);
static void cfoll(int v);
static void cgotofn(void);
static int cstate(int v);
static int member(int symb, int set, int torf);
static int notin(int n);
static void add(int *array, int n);
static void follow(int v);
static void execute(char *file);
%}

%%
s:	t
		={ unary(FINAL, $1);
		  line--;
		}
	;
t:	b r
		={ $$ = node(CAT, $1, $2); }
	| OR b r OR
		={ $$ = node(CAT, $2, $3); }
	| OR b r
		={ $$ = node(CAT, $2, $3); }
	| b r OR
		={ $$ = node(CAT, $1, $2); }
	;
b:
		={ $$ = enter(DOT);
		   $$ = unary(STAR, $$); }
	;
r:	CHAR
		={ $$ = enter($1); }
	| DOT
		={ $$ = enter(DOT); }
	| CCL
		={ $$ = cclenter(CCL); }
	| NCCL
		={ $$ = cclenter(NCCL); }
	;

r:	r OR r
		={ $$ = node(OR, $1, $3); }
	| r r %prec CAT
		={ $$ = node(CAT, $1, $2); }
	| r STAR
		={ $$ = unary(STAR, $1); }
	| r PLUS
		={ $$ = unary(PLUS, $1); }
	| r QUEST
		={ $$ = unary(QUEST, $1); }
	| '(' r ')'
		={ $$ = $2; }
	| error
	;

%%
void yyerror(char *s)
{
    fprintf(stderr, "egrep: %s\n", s);
    exit(2);
}

int yylex(void)
{
    int cclcnt, x;
    int c, d; // int, not v7's char: nextch() answers EOF, and a byte above 0177 must
              // reach name[] positive.  README.md.

    switch (c = nextch()) {
    case '$':
    case '^':
        c = '\n';
        goto defchar;
    case '|':
        return (OR);
    case '*':
        return (STAR);
    case '+':
        return (PLUS);
    case '?':
        return (QUEST);
    case '(':
        return (c);
    case ')':
        return (c);
    case '.':
        return (DOT);
    case '\0':
        return (0);
    case '\n':
        return (OR);
    case '[':
        x     = CCL;
        cclcnt = 0;
        count  = nxtchar++;
        if ((c = nextch()) == '^') {
            x = NCCL;
            c = nextch();
        }
        do {
            if (c == '\0')
                synerror();
            if (c == '-' && cclcnt > 0 && chars[nxtchar - 1] != 0) {
                if ((d = nextch()) != 0) {
                    c = chars[nxtchar - 1];
                    while (c < d) {
                        if (nxtchar >= MAXLIN)
                            overflo();
                        chars[nxtchar++] = ++c;
                        cclcnt++;
                    }
                    continue;
                }
            }
            if (nxtchar >= MAXLIN)
                overflo();
            chars[nxtchar++] = c;
            cclcnt++;
        } while ((c = nextch()) != ']');
        chars[count] = cclcnt;
        return (x);
    case '\\':
        if ((c = nextch()) == '\0')
            synerror();
    defchar:
    default:
        yylval = c;
        return (CHAR);
    }
}

static int nextch(void)
{
    int c; // v7's char is unsigned here, so `c == EOF' was never true and -f
           // read for ever.  README.md.

    if (fflag) {
        if ((c = getc(stdin)) == EOF)
            return (0);
    } else
        c = *(unsigned char *)input++; // a pattern byte is 0..255 on both machines
    return (c);
}

static void synerror(void)
{
    fprintf(stderr, "egrep: syntax error\n");
    exit(2);
}

static int enter(int x)
{
    if (line >= MAXLIN)
        overflo();
    name[line]  = x;
    left[line]  = 0;
    right[line] = 0;
    return (line++);
}

static int cclenter(int x)
{
    int linno;

    linno        = enter(x);
    right[linno] = count;
    return (linno);
}

static int node(int x, int l, int r)
{
    if (line >= MAXLIN)
        overflo();
    name[line]  = x;
    left[line]  = l;
    right[line] = r;
    parent[l]   = line;
    parent[r]   = line;
    return (line++);
}

static int unary(int x, int d)
{
    if (line >= MAXLIN)
        overflo();
    name[line]  = x;
    left[line]  = d;
    right[line] = 0;
    parent[d]   = line;
    return (line++);
}

static void overflo(void)
{
    fprintf(stderr, "egrep: regular expression too long\n");
    exit(2);
}

// A node is the right child of at most one parent, so at most one entry per node is
// ever stacked and MAXLIN needs no bound test.
static int cstack[MAXLIN];

static void cfoll(int v)
{
    int i, sp;

    sp           = 0;
    cstack[sp++] = v;
    while (sp > 0) {
        v = cstack[--sp];
        while (left[v] != 0) { // descend left, keeping v7's left-then-right order
            if (right[v] != 0)
                cstack[sp++] = right[v];
            v = left[v];
        }
        count = 0;
        for (i = 1; i <= line; i++)
            tmpstat[i] = 0;
        follow(v);
        add(foll, v);
    }
}

static void cgotofn(void)
{
    int c, i, k;
    int n, s;
    char symbol[NCHARS];
    int j, nc, pc, pos;
    int curpos, num;
    int number, newpos;

    count = 0;
    for (n = 3; n <= line; n++)
        tmpstat[n] = 0;
    if (cstate(line - 1) == 0) {
        tmpstat[line] = 1;
        count++;
        out[0] = 1;
    }
    for (n = 3; n <= line; n++)
        initstat[n] = tmpstat[n];
    count--; // leave out position 1
    icount     = count;
    tmpstat[1] = 0;
    add(state, 0);
    n = 0;
    for (s = 0; s <= n; s++) {
        if (out[s] == 1)
            continue;
        for (i = 0; i < NCHARS; i++)
            symbol[i] = 0;
        num   = positions[state[s]];
        count = icount;
        for (i = 3; i <= line; i++)
            tmpstat[i] = initstat[i];
        pos = state[s] + 1;
        for (i = 0; i < num; i++) {
            curpos = positions[pos];
            if ((c = name[curpos]) >= 0) {
                if (c < NCHARS)
                    symbol[c] = 1;
                else if (c == DOT) {
                    for (k = 0; k < NCHARS; k++)
                        if (k != '\n')
                            symbol[k] = 1;
                } else if (c == CCL) {
                    nc = chars[right[curpos]];
                    pc = right[curpos] + 1;
                    for (k = 0; k < nc; k++)
                        symbol[chars[pc++]] = 1;
                } else if (c == NCCL) {
                    nc = chars[right[curpos]];
                    for (j = 0; j < NCHARS; j++) {
                        pc = right[curpos] + 1;
                        for (k = 0; k < nc; k++)
                            if (j == chars[pc++])
                                goto cont;
                        if (j != '\n')
                            symbol[j] = 1;
                    cont:;
                    }
                } else {
                    // unreachable; v7 said so on stdout and carried on
                    fprintf(stderr, "egrep: RE botch\n");
                    exit(2);
                }
            }
            pos++;
        }
        for (c = 0; c < NCHARS; c++) {
            if (symbol[c] == 1) { // nextstate(s,c)
                count = icount;
                for (i = 3; i <= line; i++)
                    tmpstat[i] = initstat[i];
                pos = state[s] + 1;
                for (i = 0; i < num; i++) {
                    curpos = positions[pos];
                    if ((k = name[curpos]) >= 0)
                        if ((k == c) | (k == DOT) | (k == CCL && member(c, right[curpos], 1)) |
                            (k == NCCL && member(c, right[curpos], 0))) {
                            number = positions[foll[curpos]];
                            newpos = foll[curpos] + 1;
                            for (k = 0; k < number; k++) {
                                if (tmpstat[positions[newpos]] != 1) {
                                    tmpstat[positions[newpos]] = 1;
                                    count++;
                                }
                                newpos++;
                            }
                        }
                    pos++;
                } // end nextstate
                if (notin(n)) {
                    // v7 tested n, then stored at ++n: state[NSTATES] and
                    // out[NSTATES] at n == NSTATES-1.
                    if (n >= NSTATES - 1)
                        overflo();
                    add(state, ++n);
                    if (tmpstat[line] == 1)
                        out[n] = 1;
                    gotofn[s][c] = n;
                } else {
                    gotofn[s][c] = xstate;
                }
            }
        }
    }
}

// The node being visited and how far it has got: 0 = not entered, 1 = the first child
// answered, 2 = the second did.  csval carries OR's first answer, v7's `b'.
static int csnode[MAXLIN];
static int csphase[MAXLIN];
static int csval[MAXLIN];

static int cstate(int v)
{
    int sp, res;

    sp         = 0;
    csnode[0]  = v;
    csphase[0] = 0;
    res        = 0;
    for (;;) {
        v = csnode[sp];
        switch (csphase[sp]) {
        case 0:
            if (left[v] == 0) { // a leaf is an initial position
                if (tmpstat[v] != 1) {
                    tmpstat[v] = 1;
                    count++;
                }
                res = 1;
                break;
            }
            // OR visits its right child first, as v7 did; everything else the left
            csphase[sp] = 1;
            sp++;
            csnode[sp]  = (right[v] != 0 && name[v] != CAT) ? right[v] : left[v];
            csphase[sp] = 0;
            continue;

        case 1:
            if (right[v] == 0) { // STAR, PLUS, QUEST
                res = (res != 0 && name[v] == PLUS);
                break;
            }
            if (name[v] == CAT) {
                // v7's `&&': a left child that is not nullable answers for the node
                // and the right child is never visited
                if (res != 0) {
                    res = 1;
                    break;
                }
                csphase[sp] = 2;
                sp++;
                csnode[sp]  = right[v];
                csphase[sp] = 0;
                continue;
            }
            csval[sp]   = res; // OR: both children are always visited
            csphase[sp] = 2;
            sp++;
            csnode[sp]  = left[v];
            csphase[sp] = 0;
            continue;

        default:
            if (name[v] == CAT)
                res = (res != 0);
            else
                res = !(res == 0 || csval[sp] == 0);
            break;
        }
        if (sp == 0)
            return (res);
        sp--;
    }
}

static int member(int symb, int set, int torf)
{
    int i, num, pos;

    num = chars[set];
    pos = set + 1;
    for (i = 0; i < num; i++)
        if (symb == chars[pos++])
            return (torf);
    return (!torf);
}

static int notin(int n)
{
    int i, j, pos;

    for (i = 0; i <= n; i++) {
        if (positions[state[i]] == count) {
            pos = state[i] + 1;
            for (j = 0; j < count; j++)
                if (tmpstat[positions[pos++]] != 1)
                    goto nxt;
            xstate = i;
            return (0);
        }
    nxt:;
    }
    return (1);
}

static void add(int *array, int n)
{
    int i;

    // count + 1 words are written, so the highest index touched is nxtpos + count;
    // v7 tested `>'.  Conservative for foll[1], which nothing reads -- README.md.
    if (nxtpos + count >= MAXPOS)
        overflo();
    array[n]            = nxtpos;
    positions[nxtpos++] = count;
    for (i = 3; i <= line; i++) {
        if (tmpstat[i] == 1) {
            positions[nxtpos++] = i;
        }
    }
}

// Pure tail recursion in v7, in every arm.
static void follow(int v)
{
    int p;

    for (;;) {
        if (v == line)
            return;
        p = parent[v];
        switch (name[p]) {
        case STAR:
        case PLUS:
            cstate(v);
            break;

        case OR:
        case QUEST:
            break;

        case CAT:
            // v7's `&&' again: cstate() is not called when v is the right child
            if (v == left[p] && cstate(right[p]) != 0)
                return;
            break;

        case FINAL:
            if (tmpstat[line] != 1) {
                tmpstat[line] = 1;
                count++;
            }
            return;

        default: // a parent is always an operator; v7 fell out of the switch
            return;
        }
        v = p;
    }
}

int main(int argc, char **argv)
{
    while (--argc > 0 && (++argv)[0][0] == '-')
        switch (argv[0][1]) {

        case 's':
            sflag++;
            continue;

        case 'h':
            hflag = 0;
            continue;

        case 'b':
            bflag++;
            continue;

        case 'c':
            cflag++;
            continue;

        case 'e':
            argc--;
            argv++;
            goto out;

        case 'f':
            fflag++;
            continue;

        case 'l':
            lflag++;
            continue;

        case 'n':
            nflag++;
            continue;

        case 'v':
            vflag++;
            continue;

        default:
            // v7 printed this and went round the loop without consuming the
            // argument, so `egrep --' could not terminate.  grep and fgrep exit.
            fprintf(stderr, "egrep: unknown flag\n");
            exit(2);
        }
out:
    if (argc <= 0) {
        // v7 exited 2 in silence.  The status is unchanged.
        fprintf(stderr, "usage: egrep [-bchlnsv] [-e] expression [file] ...\n");
        fprintf(stderr, "       egrep [-bchlnsv] -f exprfile [file] ...\n");
        exit(2);
    }
    if (fflag) {
        if (freopen(fname = *argv, "r", stdin) == NULL) {
            fprintf(stderr, "egrep: can't open %s\n", fname);
            exit(2);
        }
    } else
        input = *argv;
    argc--;
    argv++;

    yyparse();

    cfoll(line - 1);
    cgotofn();
    nfile = argc;
    if (argc <= 0) {
        if (lflag)
            exit(1);
        execute(0);
    } else
        while (--argc >= 0) {
            execute(*argv);
            argv++;
        }
    exit(nsucc == 0);
}

static void execute(char *file)
{
    char *p;
    int cstat;
    int ccount;
    char buf[1024]; // 171 words of the 4,096 the stack has (../README.md SS6)
    char *nlp;
    int istat;

    if (file) {
        if ((f = open(file, 0)) < 0) {
            fprintf(stderr, "egrep: can't open %s\n", file);
            exit(2);
        }
    } else
        f = 0;
    ccount = 0;
    lnum   = 1;
    tln    = 0;
    coff   = 0;
    loff   = 0;
    p      = buf;
    nlp    = p;
    if ((ccount = read(f, p, 512)) <= 0)
        goto done;
    istat = cstat = gotofn[0]['\n'];
    if (out[cstat])
        goto found;
    for (;;) {
        cstat = gotofn[cstat][*p & 0377]; // all input chars made positive
        if (out[cstat]) {
        found:
            for (;;) {
                coff++;
                if (*p++ == '\n') {
                    if (vflag == 0) {
                    succeed:
                        nsucc = 1;
                        if (cflag)
                            tln++;
                        else if (sflag)
                            ; /* ugh */
                        else if (lflag) {
                            printf("%s\n", file);
                            close(f);
                            return;
                        } else {
                            if (nfile > 1 && hflag)
                                printf("%s:", file);
                            if (bflag)
                                printf("%d:", loff);
                            if (nflag)
                                printf("%d:", lnum);
                            if (p <= nlp) {
                                while (nlp < &buf[1024])
                                    putchar(*nlp++);
                                nlp = buf;
                            }
                            while (nlp < p)
                                putchar(*nlp++);
                        }
                    }
                    lnum++;
                    nlp  = p;
                    loff = coff;
                    if ((out[(cstat = istat)]) == 0)
                        goto brk2;
                }
            cfound:
                if (--ccount <= 0) {
                    if (p <= &buf[512]) {
                        if ((ccount = read(f, p, 512)) <= 0)
                            goto done;
                    } else if (p == &buf[1024]) {
                        p = buf;
                        if ((ccount = read(f, p, 512)) <= 0)
                            goto done;
                    } else {
                        if ((ccount = read(f, p, &buf[1024] - p)) <= 0)
                            goto done;
                    }
                    // the read lapped the line: its head is gone, but loff is NOT
                    // moved -- -b reports where the line began.  README.md.
                    if (nlp > p && nlp <= p + ccount)
                        nlp = p + ccount;
                }
            }
        }
        coff++;
        if (*p++ == '\n') {
            if (vflag)
                goto succeed;
            else {
                lnum++;
                nlp  = p;
                loff = coff;
                if (out[(cstat = istat)])
                    goto cfound;
            }
        }
    brk2:
        if (--ccount <= 0) {
            if (p <= &buf[512]) {
                if ((ccount = read(f, p, 512)) <= 0)
                    break;
            } else if (p == &buf[1024]) {
                p = buf;
                if ((ccount = read(f, p, 512)) <= 0)
                    break;
            } else {
                if ((ccount = read(f, p, &buf[1024] - p)) <= 0)
                    break;
            }
            if (nlp > p && nlp <= p + ccount)
                nlp = p + ccount;
        }
    }
done:
    close(f);
    if (cflag) {
        if (nfile > 1 && hflag) // v7 ignored -h here, where the line above asks it
            printf("%s:", file);
        printf("%d\n", tln);
    }
}
