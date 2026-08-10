#include "ldefs.h"

static void newccl(void);

// return next line of input, throw away trailing '\n'; 0 if eof came at once
char *getl(char *p)
{
    register int c;
    register char *s, *t;

    t = s = p;
    while (((c = gch()) != 0) && c != '\n')
        *t++ = c;
    *t = 0;
    if (c == 0 && s == t)
        return (0);
    prev = '\n';
    pres = '\n';
    return (s);
}

int space(int ch)
{
    switch (ch) {
    case ' ':
    case '\t':
    case '\n':
        return (1);
    }
    return (0);
}

int digit(int c)
{
    return (c >= '0' && c <= '9');
}

// v7 declared these with three untyped parameters and handed all three to
// fprintf, whatever the caller passed.  cmd/yacc/y1.c is the model.
_Noreturn void error(char *fmt, ...)
{
    va_list ap;

    fprintf(errorf, "\"%s\", line %d: (Error) ", fptr > 0 ? sargv[fptr] : "<stdin>", yyline);
    va_start(ap, fmt);
    vfprintf(errorf, fmt, ap);
    va_end(ap);
    putc('\n', errorf);
    if (report == 1)
        statistics();
    exit(1);
}

void warning(char *fmt, ...)
{
    va_list ap;

    fprintf(errorf, "\"%s\", line %d: (Warning) ", fptr > 0 ? sargv[fptr] : "<stdin>", yyline);
    va_start(ap, fmt);
    vfprintf(errorf, fmt, ap);
    va_end(ap);
    putc('\n', errorf);
    fflush(errorf);
    if (fout != NULL)
        fflush(fout);
    fflush(stdout);
}

// v7 called this index(), with the arguments the other way round from index(3).
int chpos(int c, const char *s)
{
    register int k;

    for (k = 0; s[k]; k++)
        if (s[k] == c)
            return (k);
    return (-1);
}

int alpha(int c)
{
    return (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z'));
}

int printable(int c)
{
    return (c > 040 && c < 0177);
}

void lgate(void)
{
    if (lgatflg)
        return;
    lgatflg = 1;
    if (fout == NULL) {
        fout = fopen("lex.yy.c", "w");
        if (fout == NULL)
            error("Can't open lex.yy.c");
        shrink_buffer(fout);
    }
    phead1();
}

int siconv(const char *t) // convert string t, return integer value
{
    register int i, sw;
    register const char *s;

    s = t;
    while (!(('0' <= *s && *s <= '9') || *s == '-') && *s)
        s++;
    sw = 0;
    if (*s == '-') { // neg
        sw = 1;
        s++;
    }
    i = 0;
    while ('0' <= *s && *s <= '9')
        i = i * 10 + (*(s++) - '0');
    return (sw ? -i : i);
}

int ctrans(char **ss)
{
    register int c, k;

    if ((c = **ss) != '\\')
        return (c);
    switch (c = *++*ss) {
    case 'n':
        c = '\n';
        break;
    case 't':
        c = '\t';
        break;
    case 'r':
        c = '\r';
        break;
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = 014;
        break;
    case '\\':
        c = '\\';
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
        c -= '0';
        while ((k = *(*ss + 1)) >= '0' && k <= '7') {
            c = c * 8 + k - '0';
            (*ss)++;
        }
        break;
    }
    return (c);
}

void cclinter(int sw) // sw = 1 ==> ccl
{
    register int i, j, k;
    int m;

    if (!sw) { // is NCCL
        for (i = 1; i < NCH; i++)
            symbol[i] ^= 1; // reverse value
    }
    for (i = 1; i < NCH; i++)
        if (symbol[i])
            break;
    if (i >= NCH)
        return;
    i = cindex[i];
    // see if ccl is already in our table
    j = 0;
    if (i) {
        for (j = 1; j < NCH; j++) {
            if ((symbol[j] && cindex[j] != i) || (!symbol[j] && cindex[j] == i))
                break;
        }
    }
    if (j >= NCH)
        return; // already in
    m = 0;
    k = 0;
    for (i = 1; i < NCH; i++)
        if (symbol[i]) {
            if (!cindex[i]) {
                cindex[i] = ccount;
                symbol[i] = 0;
                m         = 1;
            } else
                k = 1;
        }
    // m == 1 implies last value of ccount has been used
    if (m)
        newccl();
    if (k == 0)
        return; // is now in as ccount wholly
    // intersection must be computed
    for (i = 1; i < NCH; i++) {
        if (symbol[i]) {
            m = 0;
            j = cindex[i]; // will be non-zero
            for (k = 1; k < NCH; k++) {
                if (cindex[k] == j) {
                    if (symbol[k])
                        symbol[k] = 0;
                    else {
                        cindex[k] = ccount;
                        m         = 1;
                    }
                }
            }
            if (m)
                newccl();
        }
    }
}

// cindex[], match[] and pchar[] are one byte wide, so the class numbers they
// hold have a ceiling and this is every path that allocates one.
static void newccl(void)
{
    if (++ccount > MAXCCLASS)
        error("Too many character classes (limit %d)", MAXCCLASS);
}

int usescape(int c)
{
    register int d;

    switch (c) {
    case 'n':
        c = '\n';
        break;
    case 'r':
        c = '\r';
        break;
    case 't':
        c = '\t';
        break;
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = 014;
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
        c -= '0';
        while ('0' <= (d = gch()) && d <= '7') {
            c = c * 8 + (d - '0');
            if (!('0' <= peek && peek <= '7'))
                break;
        }
        break;
    }
    return (c);
}

int lookup(const char *s, char **t)
{
    register int i;

    i = 0;
    while (*t) {
        if (strcmp(s, *t) == 0)
            return (i);
        i++;
        t++;
    }
    return (-1);
}

int cpyact(void) // copy C action to the next ; or closing }
{
    register int brac, c, mth;
    int savline, sw;

    brac    = 0;
    sw      = TRUE;
    savline = yyline;

    while (!eof) {
        c = gch();
    swt:
        switch (c) {

        case '|':
            if (brac == 0 && sw == TRUE) {
                if (peek == '|')
                    gch(); // eat up an extra '|'
                return (0);
            }
            break;

        case ';':
            if (brac == 0) {
                putc(c, fout);
                putc('\n', fout);
                return (1);
            }
            break;

        case '{':
            brac++;
            savline = yyline;
            break;

        case '}':
            brac--;
            if (brac == 0) {
                putc(c, fout);
                putc('\n', fout);
                return (1);
            }
            break;

        case '/': // look for comments
            putc(c, fout);
            c = gch();
            if (c != '*')
                goto swt;

            // it really is a comment
            putc(c, fout);
            savline = yyline;
            while ((c = gch())) {
                if (c == '*') {
                    putc(c, fout);
                    if ((c = gch()) == '/')
                        goto loop;
                }
                putc(c, fout);
            }
            yyline = savline;
            error("EOF inside comment");

        case '\'': // character constant
            mth = '\'';
            goto string;

        case '"': // character string
            mth = '"';

        string:
            putc(c, fout);
            while ((c = gch())) {
                if (c == '\\') {
                    putc(c, fout);
                    c = gch();
                } else if (c == mth)
                    goto loop;
                putc(c, fout);
                if (c == '\n') {
                    yyline--;
                    error("Non-terminated string or character constant");
                }
            }
            error("EOF in string or character constant");

        case '\0':
            yyline = savline;
            error("Action does not terminate");
        default:
            break; // usual character
        }
    loop:
        if (c != ' ' && c != '\t' && c != '\n')
            sw = FALSE;
        putc(c, fout);
    }
    error("Premature EOF");
}

int gch(void)
{
    register int c;
    static int hadeof;

    if (hadeof) {
        hadeof = 0;
        yyline = 0;
    }
    prev = pres;
    c = pres = peek;
    peek     = pushptr > pushc ? *--pushptr : getc(fin);
    if (peek == EOF && sargc > 1) {
        hadeof = 1;
        fclose(fin);
        fin = fopen(sargv[++fptr], "r");
        if (fin == NULL) {
            yyline = 0;
            error("Cannot open file %s", sargv[fptr]);
        }
        peek = getc(fin);
        sargc--;
    }
    if (c == EOF) {
        eof = TRUE;
        fclose(fin);
        return (0);
    }
    if (c == '\n')
        yyline++;
    return (c);
}

// The three tree builders test the bound BEFORE they write; v7 tested it after,
// so the node that overflowed was already stored.
static void treeroom(void)
{
    if (tptr >= treesize)
        error("Parse tree too big %s", (treesize == TREESIZE ? "\nTry using %e num" : ""));
}

int mn2(int a, int d, int c)
{
    treeroom();
    name[tptr]    = a;
    left[tptr]    = d;
    right[tptr]   = c;
    parent[tptr]  = 0;
    nullstr[tptr] = 0;
    switch (a) {
    case RSTR:
        parent[d] = tptr;
        break;
    case BAR:
    case RNEWE:
        if (nullstr[d] || nullstr[c])
            nullstr[tptr] = TRUE;
        parent[d] = parent[c] = tptr;
        break;
    case RCAT:
    case DIV:
        if (nullstr[d] && nullstr[c])
            nullstr[tptr] = TRUE;
        parent[d] = parent[c] = tptr;
        break;
    case RSCON:
        parent[d]     = tptr;
        nullstr[tptr] = nullstr[d];
        break;
    default:
        warning("bad switch mn2 %d %d", a, d);
        break;
    }
    return (tptr++);
}

int mn1(int a, int d)
{
    treeroom();
    name[tptr]    = a;
    left[tptr]    = d;
    parent[tptr]  = 0;
    nullstr[tptr] = 0;
    switch (a) {
    case RCCL:
    case RNCCL:
        // d is an offset into ccl[], not a pointer to it
        if (ccl[d] == 0)
            nullstr[tptr] = TRUE;
        break;
    case STAR:
    case QUEST:
        nullstr[tptr] = TRUE;
        parent[d]     = tptr;
        break;
    case PLUS:
    case CARAT:
        nullstr[tptr] = nullstr[d];
        parent[d]     = tptr;
        break;
    case S2FINAL:
        nullstr[tptr] = TRUE;
        break;
    case FINAL:
    case S1FINAL:
        break;
    default:
        warning("bad switch mn1 %d %d", a, d);
        break;
    }
    return (tptr++);
}

int mn0(int a)
{
    treeroom();
    name[tptr]    = a;
    parent[tptr]  = 0;
    nullstr[tptr] = 0;
    if (a >= NCH)
        switch (a) {
        case RNULLS:
            nullstr[tptr] = TRUE;
            break;
        default:
            warning("bad switch mn0 %d", a);
            break;
        }
    return (tptr++);
}

// push one character back, saving what peek held
void unputc(int c)
{
    if (pushptr >= pushc + TOKENSIZE)
        error("Too many characters pushed");
    *pushptr++ = peek;
    peek       = c;
}

// ...and a whole string, rightmost first, so it reads back in order
void unputs(const char *s)
{
    register size_t i, n;

    n = strlen(s);
    if (n == 0)
        return;
    if (pushptr + n >= pushc + TOKENSIZE)
        error("Too many characters pushed");
    *pushptr++ = peek;
    peek       = (unsigned char)s[0];
    for (i = n - 1; i >= 1; i--)
        *pushptr++ = (unsigned char)s[i];
}

int dupl(int n) // duplicate the subtree whose root is n, return ptr to it
{
    register int i;

    i = name[n];
    if (i < NCH)
        return (mn0(i));
    switch (i) {
    case RNULLS:
        return (mn0(i));
    case RCCL:
    case RNCCL:
    case FINAL:
    case S1FINAL:
    case S2FINAL:
        return (mn1(i, left[n]));
    case STAR:
    case QUEST:
    case PLUS:
    case CARAT:
        return (mn1(i, dupl(left[n])));
    case RSTR:
    case RSCON:
        return (mn2(i, dupl(left[n]), right[n]));
    case BAR:
    case RNEWE:
    case RCAT:
    case DIV:
        return (mn2(i, dupl(left[n]), dupl(right[n])));
    default:
        warning("bad switch dupl %d", n);
        break;
    }
    return (0);
}
