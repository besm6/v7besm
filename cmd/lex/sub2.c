#include "ldefs.h"

// v7 gated three of these blocks on PP, PC and PS, and defined all three
// unconditionally in ldefs.c.  The conditionals are gone, the live arm kept.

// acompute()'s action list.  A state cannot have more positions than this.
#define MAXPOSPERSTATE 300

static void add(int **array, int n);
static void padd(int **array, int n);
static void follow(int v);
static void first(int v);
static void nextstate(int s, int c);
static int notin(int n);
static void packtrans(int st, const unsigned char *tch, const int *tst, int cnt, int tryit);
static int member(int d, const unsigned char *t);

// follow() climbs the parents and stays shallow, so it keeps its recursion.
// README.md, "Walking the tree".
static int depth;

static void enter(void)
{
    if (++depth > MAXDEPTH)
        error("Regular expression nested deeper than %d", MAXDEPTH);
}

// Defer one subtree of a tree walk.  LIFO: the walk must reach a node in the
// order the recursion would have.  README.md, "Walking the tree".
static void defer(int *stack, int *top, int v)
{
    if (*top >= MAXDEFER)
        error("Parse tree deeper than %d rules", MAXDEFER);
    stack[(*top)++] = v;
}

// The RCCL/RNCCL arm of cfoll(), split out: its temporaries were half of cfoll's
// frame.  Does not recurse.
static void compress_ccl(int v, int isccl)
{
    register int j, k;
    unsigned char *p;
    const unsigned char *q;

    for (j = 1; j < NCH; j++)
        symbol[j] = !isccl;
    q = ccl + left[v];
    while (*q)
        symbol[*q++] = isccl;
    p = pcptr;
    for (j = 1; j < NCH; j++)
        if (symbol[j]) {
            for (k = 0; p + k < pcptr; k++)
                if (cindex[j] == *(p + k))
                    break;
            if (p + k >= pcptr)
                *pcptr++ = cindex[j];
        }
    *pcptr++ = 0;
    if (pcptr > pchar + pchlen)
        error("Too many packed character classes %s",
              (pchlen == TOKENSIZE ? "\nTry using %k num" : ""));
    // left[] stops indexing ccl[] here and starts indexing pchar[]
    left[v] = p - pchar;
    name[v] = RCCL; // RNCCL eliminated
}

// The position sets of the leaves under v.  Split out for compress_ccl()'s reason.
static void leaffoll(int v)
{
    register int j;

    for (j = 0; j < tptr; j++)
        tmpstat[j] = FALSE;
    count = 0;
    follow(v);
    padd(foll, v);
}

// v7 recursed here; this walk is iterative, the tree being as deep as the file
// has rules.  main() is the one caller, so the worklist can be static.
void cfoll(int v)
{
    static int stack[MAXDEFER];
    int top = 0;

    for (;;) {
        register int i = name[v];
        if (i < NCH)
            i = 1; // character
        switch (i) {
        case 1:
        case RSTR:
        case RCCL:
        case RNCCL:
        case RNULLS:
            leaffoll(v);
            if (i == RSTR) {
                v = left[v];
                continue;
            }
            if (i == RCCL || i == RNCCL)
                compress_ccl(v, i == RCCL);
            break;
        case CARAT:
        case STAR:
        case PLUS:
        case QUEST:
        case RSCON:
            v = left[v];
            continue;
        case BAR:
        case RCAT:
        case DIV:
        case RNEWE:
            defer(stack, &top, right[v]);
            v = left[v];
            continue;
        case FINAL:
        case S1FINAL:
        case S2FINAL:
            break;
        default:
            warning("bad switch cfoll %d", v);
            break;
        }
        if (top == 0)
            return;
        v = stack[--top];
    }
}

static void add(int **array, int n)
{
    register int i, *temp;
    register const char *ctemp;

    temp     = nxtpos;
    ctemp    = tmpstat;
    array[n] = nxtpos; // note no packing is done in positions
    *temp++  = count;
    for (i = 0; i < tptr; i++)
        if (ctemp[i] == TRUE)
            *temp++ = i;
    nxtpos = temp;
    if (nxtpos >= positions + maxpos)
        error("Too many positions %s", (maxpos == MAXPOS ? "\nTry using %p num" : ""));
}

static void padd(int **array, int n)
{
    register int i, k;

    array[n] = nxtpos;
    if (count == 0) {
        *nxtpos++ = 0;
        return;
    }
    for (i = tptr - 1; i >= 0; i--) {
        const int *j = array[i];
        if (j && *j++ == count) {
            for (k = 0; k < count; k++)
                if (!tmpstat[*j++])
                    break;
            if (k >= count) {
                array[n] = array[i];
                return;
            }
        }
    }
    add(array, n);
}

static void follow(int v)
{
    register int p;

    if (v >= tptr - 1)
        return;
    p = parent[v];
    if (p == 0)
        return;
    enter();
    switch (name[p]) {
        // will not be CHAR RNULLS FINAL S1FINAL S2FINAL RCCL RNCCL
    case RSTR:
        if (tmpstat[p] == FALSE) {
            count++;
            tmpstat[p] = TRUE;
        }
        break;
    case STAR:
    case PLUS:
        first(v);
        follow(p);
        break;
    case BAR:
    case QUEST:
    case RNEWE:
        follow(p);
        break;
    case RCAT:
    case DIV:
        if (v == left[p]) {
            if (nullstr[right[p]])
                follow(p);
            first(right[p]);
        } else
            follow(p);
        break;
    case RSCON:
    case CARAT:
        follow(p);
        break;
    default:
        warning("bad switch follow %d", p);
        break;
    }
    depth--;
}

// Is the current start condition among v's?  Split out of first()'s RSCON arm.
static int scon_active(int v)
{
    register const unsigned char *p;
    register int i;

    i = stnum / 2 + 1;
    p = slist + right[v];
    while (*p)
        if (*p++ == i)
            return TRUE;
    return FALSE;
}

// Set of positions with v as root which can be active initially.  Iterative for
// cfoll()'s reason; follow() never re-enters it, so this worklist is static too.
static void first(int v)
{
    static int stack[MAXDEFER];
    int top = 0;

    for (;;) {
        register int i = name[v];
        if (i < NCH)
            i = 1;
        switch (i) {
        case 1:
        case RCCL:
        case RNCCL:
        case RNULLS:
        case FINAL:
        case S1FINAL:
        case S2FINAL:
            if (tmpstat[v] == FALSE) {
                count++;
                tmpstat[v] = TRUE;
            }
            break;
        case BAR:
        case RNEWE:
            defer(stack, &top, right[v]);
            v = left[v];
            continue;
        case CARAT:
            if (stnum % 2 == 1) {
                v = left[v];
                continue;
            }
            break;
        case RSCON:
            if (scon_active(v)) {
                v = left[v];
                continue;
            }
            break;
        case STAR:
        case QUEST:
        case PLUS:
        case RSTR:
            v = left[v];
            continue;
        case RCAT:
        case DIV:
            if (nullstr[left[v]])
                defer(stack, &top, right[v]);
            v = left[v];
            continue;
        default:
            warning("bad switch first %d", v);
            break;
        }
        if (top == 0)
            return;
        v = stack[--top];
    }
}

void cgoto(void)
{
    register int i, j, s;
    // static, not automatic: 300 words of frame the target's 4,096-word stack
    // would rather not carry, and cgoto() does not recurse.
    static unsigned char tch[NCH];
    static int tst[NCH];

    // generate initial state, for each start condition
    fprintf(fout, "int yyvstop[] ={\n0,\n");
    while (stnum < 2 || stnum / 2 < sptr) {
        for (i = 0; i < tptr; i++)
            tmpstat[i] = 0;
        count = 0;
        if (tptr > 0)
            first(tptr - 1);
        add(state, stnum);
        stnum++;
    }
    stnum--;
    // even stnum = might not be at line begin
    // odd stnum  = must be at line begin
    for (s = 0; s <= stnum; s++) {
        int npos, n;
        int tryit   = FALSE;
        cpackflg[s] = FALSE;
        sfall[s]    = -1;
        acompute(s);
        for (i = 0; i < NCH; i++)
            symbol[i] = 0;
        npos = *state[s];
        for (i = 1; i <= npos; i++) {
            int curpos = *(state[s] + i);
            if (name[curpos] < NCH)
                symbol[name[curpos]] = TRUE;
            else
                switch (name[curpos]) {
                case RCCL: {
                    const unsigned char *q = pchar + left[curpos];
                    tryit                  = TRUE;
                    while (*q) {
                        for (j = 1; j < NCH; j++)
                            if (cindex[j] == *q)
                                symbol[j] = TRUE;
                        q++;
                    }
                    break;
                }
                case RSTR:
                    symbol[right[curpos]] = TRUE;
                    break;
                case RNULLS:
                case FINAL:
                case S1FINAL:
                case S2FINAL:
                    break;
                default:
                    warning("bad switch cgoto %d state %d", curpos, s);
                    break;
                }
        }
        // for each char, calculate next state
        n = 0;
        for (i = 1; i < NCH; i++) {
            if (symbol[i]) {
                nextstate(s, i); // once per state, transition pair
                xstate = notin(stnum);
                if (xstate == -2)
                    warning("bad state  %d %o", s, i);
                else if (xstate == -1) {
                    if (stnum >= nstates)
                        error("Too many states %s",
                              (nstates == NSTATES ? "\nTry using %n num" : ""));
                    add(state, ++stnum);
                    tch[n]   = i;
                    tst[n++] = stnum;
                } else { // xstate >= 0 ==> state exists
                    tch[n]   = i;
                    tst[n++] = xstate;
                }
            }
        }
        tch[n] = 0;
        tst[n] = -1;
        // pack transitions into permanent array
        if (n > 0)
            packtrans(s, tch, tst, n, tryit);
        else
            gotof[s] = -1;
    }
    fprintf(fout, "0};\n");
}

// Beware -- 70% of total CPU time is spent in this subroutine.
static void nextstate(int s, int c)
{
    register int j;
    register char *temp;
    register const char *tz;
    const int *pos;
    int i, *f, num;

    // state to goto from state s on char c
    num  = *state[s];
    temp = tmpstat;
    pos  = state[s] + 1;
    for (i = 0; i < num; i++) {
        int curpos = *pos++;
        j          = name[curpos];
        if ((j < NCH && j == c) || (j == RSTR && c == right[curpos]) ||
            (j == RCCL && member(c, pchar + left[curpos]))) {
            const int *newpos;
            int number;
            f      = foll[curpos];
            number = *f;
            newpos = f + 1;
            for (j = 0; j < number; j++)
                temp[*newpos++] = 2;
        }
    }
    j  = 0;
    tz = temp + tptr;
    while (temp < tz) {
        if (*temp == 2) {
            j++;
            *temp++ = 1;
        } else
            *temp++ = 0;
    }
    count = j;
}

static int notin(int n) // see if tmpstat occurs previously
{
    register int k;
    register const char *temp;
    int i;

    if (count == 0)
        return (-2);
    temp = tmpstat;
    for (i = n; i >= 0; i--) { // for each state
        const int *j = state[i];
        if (count == *j++) {
            for (k = 0; k < count; k++)
                if (!temp[*j++])
                    break;
            if (k >= count)
                return (i);
        }
    }
    return (-1);
}

// pack transitions into nchar, nexts.  nchar is terminated by '\0', nexts uses
// cnt followed by that many elements; gotof[st] indexes both for state st.
// sfall[st] == t means t is the fall back state for st, -1 that there is none.
static void packtrans(int st, const unsigned char *tch, const int *tst, int cnt, int tryit)
{
    int cmin, cval, tcnt, diff, p;
    const int *ast;
    register int i, j, k;
    const unsigned char *ach;
    int c;
    int upper;
    // static for cgoto()'s reason: some 800 words of frame, and no recursion.
    static int go[NCH], temp[NCH], swork[NCH];
    static unsigned char cwork[NCH];

    rcount += cnt;
    cmin = -1;
    cval = NCH;
    ast  = tst;
    ach  = tch;
    // try to pack transitions using ccl's
    if (!optim)
        goto nopack; // skip all compaction
    if (tryit) {     // ccl's used
        for (i = 1; i < NCH; i++) {
            go[i] = temp[i] = -1;
            symbol[i]       = 1;
        }
        for (i = 0; i < cnt; i++) {
            go[tch[i]]     = tst[i];
            symbol[tch[i]] = 0;
        }
        for (i = 0; i < cnt; i++) {
            c = match[tch[i]];
            if (go[c] != tst[i] || c == tch[i])
                temp[tch[i]] = tst[i];
        }
        // fill in error entries
        for (i = 1; i < NCH; i++)
            if (symbol[i])
                temp[i] = -2; // error trans
        // count them
        k = 0;
        for (i = 1; i < NCH; i++)
            if (temp[i] != -1)
                k++;
        if (k < cnt) { // compress by char
            k = 0;
            for (i = 1; i < NCH; i++)
                if (temp[i] != -1) {
                    cwork[k]   = i;
                    swork[k++] = (temp[i] == -2 ? -1 : temp[i]);
                }
            cwork[k]     = 0;
            ach          = cwork;
            ast          = swork;
            cnt          = k;
            cpackflg[st] = TRUE;
        }
    }
    for (i = 0; i < st; i++) { // get most similar state
        // reject a state with more transitions, one already represented by a
        // third state, and one compressed by char if ours is not to be
        if (sfall[i] != -1)
            continue;
        if (cpackflg[st] == 1)
            if (!(cpackflg[i] == 1))
                continue;
        p = gotof[i];
        if (p == -1) // no transitions
            continue;
        tcnt = nexts[p];
        if (tcnt > cnt)
            continue;
        diff  = 0;
        k     = 0;
        j     = 0;
        upper = p + tcnt;
        while (ach[j] && p < upper) {
            while (ach[j] < nchar[p] && ach[j]) {
                diff++;
                j++;
            }
            if (ach[j] == 0)
                break;
            if (ach[j] > nchar[p]) {
                diff = NCH;
                break;
            }
            // ach[j] == nchar[p]
            if (ast[j] != nexts[++p] || ast[j] == -1 ||
                (cpackflg[st] && ach[j] != match[ach[j]]))
                diff++;
            j++;
        }
        while (ach[j]) {
            diff++;
            j++;
        }
        if (p < upper)
            diff = NCH;
        if (diff < cval && diff < tcnt) {
            cval = diff;
            cmin = i;
            if (cval == 0)
                break;
        }
    }
    // cmin = state "most like" state st.  Reserve before writing: v7 tested
    // nptr against ntrans only after the whole state had gone in.
    if (nptr + cnt + 1 > ntrans)
        error("Too many transitions %s", (ntrans == NTRANS ? "\nTry using %a num" : ""));
    if (cmin != -1) { // if we can use st cmin
        gotof[st] = nptr;
        k         = 0;
        sfall[st] = cmin;
        p         = gotof[cmin] + 1;
        j         = 0;
        while (ach[j]) {
            // if cmin has a transition on c, then so will st -- st may be
            // "larger" than cmin, however
            while (ach[j] < nchar[p - 1] && ach[j]) {
                k++;
                nchar[nptr]  = ach[j];
                nexts[++nptr] = ast[j];
                j++;
            }
            if (nchar[p - 1] == 0)
                break;
            if (ach[j] > nchar[p - 1]) {
                warning("bad transition %d %d", st, cmin);
                goto nopack;
            }
            // ach[j] == nchar[p-1]
            if (ast[j] != nexts[p] || ast[j] == -1 ||
                (cpackflg[st] && ach[j] != match[ach[j]])) {
                k++;
                nchar[nptr]   = ach[j];
                nexts[++nptr] = ast[j];
            }
            p++;
            j++;
        }
        while (ach[j]) {
            nchar[nptr]   = ach[j];
            nexts[++nptr] = ast[j++];
            k++;
        }
        nexts[gotof[st]] = cnt = k;
        nchar[nptr++]          = 0;
    } else {
    nopack:
        if (nptr + cnt + 1 > ntrans)
            error("Too many transitions %s", (ntrans == NTRANS ? "\nTry using %a num" : ""));
        // stick it in
        gotof[st] = nptr;
        nexts[nptr] = cnt;
        for (i = 0; i < cnt; i++) {
            nchar[nptr]   = ach[i];
            nexts[++nptr] = ast[i];
        }
        nchar[nptr++] = 0;
    }
    if (cnt < 1) {
        gotof[st] = -1;
        nptr--;
    }
}

static int member(int d, const unsigned char *t)
{
    register int c;
    register const unsigned char *s;

    c = d;
    s = t;
    c = cindex[c];
    while (*s)
        if (*s++ == c)
            return (1);
    return (0);
}

void acompute(int s) // compute action list = set of poss. actions
{
    register const int *p;
    register int i, j;
    int cnt;
    int k, n;
    // static for cgoto()'s reason: 600 words of frame, and no recursion.
    static int temp[MAXPOSPERSTATE], neg[MAXPOSPERSTATE];

    k   = 0;
    n   = 0;
    p   = state[s];
    cnt = *p++;
    if (cnt > MAXPOSPERSTATE)
        error("Too many positions for one state - acompute");
    for (i = 0; i < cnt; i++) {
        if (name[*p] == FINAL)
            temp[k++] = left[*p];
        else if (name[*p] == S1FINAL) {
            temp[k++] = left[*p];
            if (left[*p] >= NACTIONS)
                error("Too many right contexts");
            extra[left[*p]] = 1;
        } else if (name[*p] == S2FINAL)
            neg[n++] = left[*p];
        p++;
    }
    atable[s] = -1;
    if (k < 1 && n < 1)
        return;
    // sort action list
    for (i = 0; i < k; i++)
        for (j = i + 1; j < k; j++)
            if (temp[j] < temp[i]) {
                int m   = temp[j];
                temp[j] = temp[i];
                temp[i] = m;
            }
    // remove dups
    for (i = 0; i < k - 1; i++)
        if (temp[i] == temp[i + 1])
            temp[i] = 0;
    // copy to permanent quarters
    atable[s] = aptr;
    putc('\n', fout);
    for (i = 0; i < k; i++)
        if (temp[i] != 0) {
            fprintf(fout, "%d,\n", temp[i]);
            aptr++;
        }
    for (i = 0; i < n; i++) { // copy fall back actions - all neg
        fprintf(fout, "%d,\n", neg[i]);
        aptr++;
    }
    fprintf(fout, "0,\n");
    aptr++;
}

void mkmatch(void)
{
    register int i;
    unsigned char tab[NCH];

    for (i = 0; i < ccount; i++)
        tab[i] = 0;
    for (i = 1; i < NCH; i++)
        if (tab[cindex[i]] == 0)
            tab[cindex[i]] = i;
    // tab[i] = principal char for new ccl i
    for (i = 1; i < NCH; i++)
        match[i] = tab[cindex[i]];
}

void layout(void) // format and output final program's tables
{
    register int i, j, k;
    int top, bot, startup, omin;

    startup = 0;
    for (i = 0; i < outsize; i++)
        verify[i] = advance[i] = 0;
    omin  = 0;
    yytop = 0;
    for (i = 0; i <= stnum; i++) { // for each state
        j = gotof[i];
        if (j == -1) {
            stoff[i] = 0;
            continue;
        }
        bot = j;
        while (nchar[j])
            j++;
        top = j - 1;
        while (omin + NCH < outsize && verify[omin + NCH])
            omin++;
        startup = omin;
        if (chset) {
            do {
                ++startup;
                if (startup + NCH >= outsize)
                    error("output table overflow %s",
                          (outsize == NOUTPUT ? "\nTry using %o num" : ""));
                for (j = bot; j <= top; j++) {
                    k = startup + ctable[nchar[j]];
                    if (verify[k])
                        break;
                }
            } while (j <= top);
            // have found place
            for (j = bot; j <= top; j++) {
                k         = startup + ctable[nchar[j]];
                verify[k] = i + 1;             // state number + 1
                advance[k] = nexts[j + 1] + 1; // state number + 1
                if (yytop < k)
                    yytop = k;
            }
        } else {
            do {
                ++startup;
                if (startup + NCH >= outsize)
                    error("output table overflow %s",
                          (outsize == NOUTPUT ? "\nTry using %o num" : ""));
                for (j = bot; j <= top; j++) {
                    k = startup + nchar[j];
                    if (verify[k])
                        break;
                }
            } while (j <= top);
            // have found place
            for (j = bot; j <= top; j++) {
                k          = startup + nchar[j];
                verify[k]  = i + 1;           // state number + 1
                advance[k] = nexts[j + 1] + 1; // state number + 1
                if (yytop < k)
                    yytop = k;
            }
        }
        stoff[i] = startup;
    }

    // stoff[i] = offset into verify, advance for trans for state i
    // put out yywork.  verify and advance hold a state number plus one, so
    // they are never negative and one byte reaches 255 rather than 127.
    fprintf(fout, "# define YYTYPE %s\n", stnum + 1 > MAXCCLASS ? "int" : "unsigned char");
    // Braced per element.  v7 emitted a flat list into an array of structs,
    // which is legal and which -Wmissing-braces diagnoses.
    fprintf(fout, "struct yywork { YYTYPE verify, advance; } yycrank[] ={\n");
    for (i = 0; i <= yytop; i += 4) {
        for (j = 0; j < 4; j++) {
            k = i + j;
            if (verify[k])
                fprintf(fout, "{%d,%d},\t", verify[k], advance[k]);
            else
                fprintf(fout, "{0,0},\t");
        }
        putc('\n', fout);
    }
    fprintf(fout, "{0,0}};\n");

    // put out yysvec.  yystoff is a SIGNED OFFSET into yycrank[], negative for
    // a char-compressed state; v7 emitted `yycrank+-5', a pointer below the
    // base of its own array, and the skeleton reflected it back.  README.md.
    fprintf(fout, "struct yysvf yysvec[] ={\n");
    fprintf(fout, "{0,\t0,\t0},\n");
    for (i = 0; i <= stnum; i++) { // for each state
        if (cpackflg[i])
            stoff[i] = -stoff[i];
        fprintf(fout, "{%d,\t", stoff[i]);
        if (sfall[i] != -1)
            fprintf(fout, "yysvec+%d,\t", sfall[i] + 1); // state + 1
        else
            fprintf(fout, "0,\t\t");
        if (atable[i] != -1)
            fprintf(fout, "yyvstop+%d},", atable[i]);
        else
            fprintf(fout, "0},\t");
        putc('\n', fout);
    }
    fprintf(fout, "{0,\t0,\t0}};\n");

    // put out yymatch
    fprintf(fout, "int yytop = %d;\n", yytop);
    fprintf(fout, "struct yysvf *yybgin = yysvec+1;\n");
    if (optim) {
        fprintf(fout, "unsigned char yymatch[] ={\n");
        if (chset == 0) { // no chset, put out in normal order
            for (i = 0; i < NCH; i += 8) {
                for (j = 0; j < 8; j++) {
                    int fbch;
                    fbch = match[i + j];
                    if (printable(fbch) && fbch != '\'' && fbch != '\\')
                        fprintf(fout, "'%c' ,", fbch);
                    else
                        fprintf(fout, "0%-3o,", fbch);
                }
                putc('\n', fout);
            }
        } else {
            int *fbarr = (int *)myalloc(NCH, sizeof(*fbarr));
            for (i = 0; i < NCH; i++)
                fbarr[i] = 0;
            for (i = 0; i < NCH; i++)
                fbarr[ctable[i]] = ctable[match[i]];
            for (i = 0; i < NCH; i += 8) {
                for (j = 0; j < 8; j++)
                    fprintf(fout, "0%-3o,", fbarr[i + j]);
                putc('\n', fout);
            }
            free(fbarr);
        }
        fprintf(fout, "0};\n");
    }
    // put out yyextra
    fprintf(fout, "unsigned char yyextra[] ={\n");
    for (i = 0; i < casecount; i += 8) {
        for (j = 0; j < 8; j++)
            fprintf(fout, "%d,", i + j < NACTIONS ? extra[i + j] : 0);
        putc('\n', fout);
    }
    fprintf(fout, "0};\n");
}
