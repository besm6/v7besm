// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// C preprocessor
// written by John F. Reiser
// July/August 1978
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defs.h"

#define STATIC

#define STDIN  0
#define STDOUT 1
#define STDERR 2
#define READ   0
#define WRITE  1
#define SALT   '#'
#define BLANK  1
#define IDENT  2
#define NUMBR  3

//
// a superimposed code is used to reduce the number of calls to the
// symbol table lookup routine.  (if the kth character of an identifier
// is 'a' and there are no macro names whose kth character is 'a'
// then the identifier cannot be a macro name, hence there is no need
// to look in the symbol table.)  'scw1' enables the test based on
// single characters and their position in the identifier.  'scw2'
// enables the test based on adjacent pairs of characters and their
// position in the identifier.  scw1 typically costs 1 indexed fetch,
// an AND, and a jump per character of identifier, until the identifier
// is known as a non-macro name or until the end of the identifier.
// scw1 is inexpensive.  scw2 typically costs 4 indexed fetches,
// an add, an AND, and a jump per character of identifier, but it is also
// slightly more effective at reducing symbol table searches.
// scw2 usually costs too much because the symbol table search is
// usually short; but if symbol table search should become expensive,
// the code is here.
// using both scw1 and scw2 is of dubious value.
//
#define scw1 1
#define scw2 0

#if scw2
char t21[ALFSIZ], t22[ALFSIZ], t23[ALFSIZ + 8];
#endif

#if scw1
#define b0 1
#define b1 2
#define b2 4
#define b3 8
#define b4 16
#define b5 32
#define b6 64
#define b7 128
#endif

#define isslo    (cpp.ptrtab == cpp.slotab)
#define isspc(a) (cpp.ptrtab[(unsigned char)a] & SB)

#define eob(a) ((a) >= cpp.pend)
#define bob(a) (cpp.pbeg >= (a))

#define DROP   0xFE // special character not legal ASCII or EBCDIC
#define WARN   DROP
#define SAME   0
#define MAXFRM 31 // max number of formals/actuals to a macro

static char warnc = WARN;

#if tgp
int tgpscan; // flag for dump();
#endif

//
// The one and only instance of the preprocessor's mutable state, declared in
// defs.h.  Bundles what used to be ~40 file-scope globals.
//
struct cppstate cpp;

struct symtab *slookup(char *p1, char *p2, int enterf);
char *trmdir(char *s);
STATIC char *copy(const char *s);
char *subst(char *p, struct symtab *sp);

void sayline()
{
    if (cpp.pflag == 0)
        fprintf(cpp.fout, "# %d \"%s\"\n", cpp.lineno[cpp.ifno], cpp.fnames[cpp.ifno]);
}

// data structure guide
//
// most of the scanning takes place in the buffer:
//
//  (low address)                                             (high address)
//  pbeg                           pbuf                                 pend
//  |      <-- BUFSIZ chars -->      |         <-- BUFSIZ chars -->        |
//  _______________________________________________________________________
// |_______________________________________________________________________|
//          |               |               |
//          |<-- waiting -->|               |<-- waiting -->
//          |    to be      |<-- current -->|    to be
//          |    written    |    token      |    scanned
//          |               |               |
//          outp            inp             p
//
//  *outp   first char not yet written to output file
//  *inp    first char of current token
//  *p      first char not yet scanned
//
// macro expansion: write from *outp to *inp (chars waiting to be written),
// ignore from *inp to *p (chars of the macro call), place generated
// characters in front of *p (in reverse order), update pointers,
// resume scanning.
//
// symbol table pointers point to just beyond the end of macro definitions;
// the first preceding character is the number of formal parameters.
// the appearance of a formal in the body of a definition is marked by
// 2 chars: the char WARN, and a char containing the parameter number.
// the first char of a definition is preceded by a zero character.
//
// when macro expansion attempts to back up over the beginning of the
// buffer, some characters preceding *pend are saved in a side buffer,
// the address of the side buffer is put on 'instack', and the rest
// of the main buffer is moved to the right.  the end of the saved buffer
// is kept in 'endbuf' since there may be nulls in the saved buffer.
//
// similar action is taken when an 'include' statement is processed,
// except that the main buffer must be completely emptied.  the array
// element 'inctop[ifno]' records the last side buffer saved when
// file 'ifno' was included.  these buffers remain dormant while
// the file is being read, and are reactivated at end-of-file.
//
// instack[0 : mactop] holds the addresses of all pending side buffers.
// instack[inctop[ifno]+1 : mactop-1] holds the addresses of the side
// buffers which are "live"; the side buffers instack[0 : inctop[ifno]]
// are dormant, waiting for end-of-file on the current file.
//
// space for side buffers is obtained from 'savch' and is never returned.
// bufstack[0:fretop-1] holds addresses of side buffers which
// are available for use.
//
void dump()
{
    // write part of buffer which lies between  outp  and  inp .
    // this should be a direct call to 'write', but the system slows to a crawl
    // if it has to do an unaligned copy.  thus we buffer.  this silly loop
    // is 15% of the total time, thus even the 'putc' macro is too slow.
    //
    char *p1;
    FILE *f;
    if ((p1 = cpp.outp) == cpp.inp || cpp.flslvl != 0)
        return;
#if tgp
#define MAXOUT 80
    if (!tgpscan) { // scan again to insure <= MAXOUT chars between linefeeds
        char c, *pblank, *p2;
        char savc, stopc, brk;
        tgpscan = 1;
        brk = stopc = pblank = 0;
        p2                   = cpp.inp;
        savc                 = *p2;
        *p2                  = '\0';
        while (c = *p1++) {
            if (c == '\\')
                c = *p1++;
            if (stopc == c)
                stopc = 0;
            else if (c == '"' || c == '\'')
                stopc = c;
            if (p1 - cpp.outp > MAXOUT && pblank != 0) {
                *pblank++ = '\n';
                cpp.inp   = pblank;
                dump();
                brk    = 1;
                pblank = 0;
            }
            if (c == ' ' && stopc == 0)
                pblank = p1 - 1;
        }
        if (brk)
            sayline();
        *p2     = savc;
        cpp.inp = p2;
        p1      = cpp.outp;
        tgpscan = 0;
    }
#endif
    f = cpp.fout;
    while (p1 < cpp.inp)
        putc(*p1++, f);
    cpp.outp = p1;
}

char *refill(char *p)
{
    // dump buffer.  save chars from inp to p.  read into buffer at pbuf,
    // contiguous with p.  update pointers, return new p.
    //
    char *np;
    const char *op;

    dump();
    np = cpp.pbuf - (p - cpp.inp);
    op = cpp.inp;
    if (bob(np + 1)) {
        pperror("token too long");
        np = cpp.pbeg;
        p  = cpp.inp + BUFSIZ;
    }
    cpp.macdam += np - cpp.inp;
    cpp.outp = cpp.inp = np;
    while (op < p)
        *np++ = *op++;
    p = np;
    for (;;) {
        if (cpp.mactop > cpp.inctop[cpp.ifno]) { // retrieve hunk of pushed-back macro text
            op = cpp.instack[--cpp.mactop];
            np = cpp.pbuf;
            do {
                while ((*np++ = *op++))
                    ;
            } while (op < cpp.endbuf[cpp.mactop]);
            cpp.pend = np - 1;
            // make buffer space avail for 'include' processing
            if (cpp.fretop < MAXFRE)
                cpp.bufstack[cpp.fretop++] = cpp.instack[cpp.mactop];
            return (p);
        } else { // get more text from file(s)
            cpp.maclvl = 0;
            int ninbuf = read(cpp.fin, cpp.pbuf, BUFSIZ);
            if (0 < ninbuf) {
                cpp.pend  = cpp.pbuf + ninbuf;
                *cpp.pend = '\0';
                return (p);
            }
            // end of #include file
            if (cpp.ifno == 0) { // end of input
                if (cpp.plvl != 0) {
                    int n = cpp.plvl, tlin = cpp.lineno[cpp.ifno];
                    char *tfil           = cpp.fnames[cpp.ifno];
                    cpp.lineno[cpp.ifno] = cpp.maclin;
                    cpp.fnames[cpp.ifno] = cpp.macfil;
                    pperror("%s: unterminated macro call", cpp.macnam);
                    cpp.lineno[cpp.ifno] = tlin;
                    cpp.fnames[cpp.ifno] = tfil;
                    np                   = p;
                    *np++                = '\n'; // shut off unterminated quoted string
                    while (--n >= 0)
                        *np++ = ')'; // supply missing parens
                    cpp.pend = np;
                    *np      = '\0';
                    if (cpp.plvl < 0)
                        cpp.plvl = 0;
                    return (p);
                }
                cpp.inp = p;
                dump();
                exit(cpp.exfail);
            }
            close(cpp.fin);
            cpp.fin     = cpp.fins[--cpp.ifno];
            cpp.dirs[0] = cpp.dirnams[cpp.ifno];
            sayline();
        }
    }
}

#define BEG 0
#define LF  1

char *cotoken(char *p)
{
    int c, i;
    char quoc;
    static int state = BEG;

    if (state != BEG)
        goto prevlf;
    for (;;) {
    again:
        while (!isspc(*p++))
            ;
        switch (*(cpp.inp = p - 1)) {
        case 0: {
            if (eob(--p)) {
                p = refill(p);
                goto again;
            } else
                ++p; // ignore null byte
        } break;
        case '|':
        case '&':
            for (;;) { // sloscan only
                if (*p++ == *cpp.inp)
                    break;
                if (eob(--p))
                    p = refill(p);
                else
                    break;
            }
            break;
        case '=':
        case '!':
            for (;;) { // sloscan only
                if (*p++ == '=')
                    break;
                if (eob(--p))
                    p = refill(p);
                else
                    break;
            }
            break;
        case '<':
        case '>':
            for (;;) { // sloscan only
                if (*p++ == '=' || p[-2] == p[-1])
                    break;
                if (eob(--p))
                    p = refill(p);
                else
                    break;
            }
            break;
        case '\\':
            for (;;) {
                if (*p++ == '\n') {
                    ++cpp.lineno[cpp.ifno];
                    break;
                }
                if (eob(--p))
                    p = refill(p);
                else {
                    ++p;
                    break;
                }
            }
            break;
        case '/':
            for (;;) {
                if (*p++ == '*') { // comment
                    if (!cpp.passcom) {
                        cpp.inp = p - 2;
                        dump();
                        ++cpp.flslvl;
                    }
                    for (;;) {
                        while (!iscom(*p++))
                            ;
                        if (p[-1] == '*')
                            for (;;) {
                                if (*p++ == '/')
                                    goto endcom;
                                if (eob(--p)) {
                                    if (!cpp.passcom) {
                                        cpp.inp = p;
                                        p       = refill(p);
                                    } else if ((p - cpp.inp) >= BUFSIZ) { // split long comment
                                        cpp.inp = p;
                                        p       = refill(p); // last char written is '*'
                                        putc('/', cpp.fout); // terminate first part
                                        // and fake start of 2nd
                                        cpp.outp = cpp.inp = p -= 3;
                                        *p++               = '/';
                                        *p++               = '*';
                                        *p++               = '*';
                                    } else
                                        p = refill(p);
                                } else
                                    break;
                            }
                        else if (p[-1] == '\n') {
                            ++cpp.lineno[cpp.ifno];
                            if (!cpp.passcom)
                                putc('\n', cpp.fout);
                        } else if (eob(--p)) {
                            if (!cpp.passcom) {
                                cpp.inp = p;
                                p       = refill(p);
                            } else if ((p - cpp.inp) >= BUFSIZ) { // split long comment
                                cpp.inp = p;
                                p       = refill(p);
                                putc('*', cpp.fout);
                                putc('/', cpp.fout);
                                cpp.outp = cpp.inp = p -= 2;
                                *p++               = '/';
                                *p++               = '*';
                            } else
                                p = refill(p);
                        } else
                            ++p; // ignore null byte
                    }
                endcom:
                    if (!cpp.passcom) {
                        cpp.outp = cpp.inp = p;
                        --cpp.flslvl;
                        goto again;
                    }
                    break;
                }
                if (eob(--p))
                    p = refill(p);
                else
                    break;
            }
            break;
        case '"':
        case '\'': {
            quoc = p[-1];
            for (;;) {
                while (!isquo(*p++))
                    ;
                if (p[-1] == quoc)
                    break;
                if (p[-1] == '\n') {
                    --p;
                    break;
                } // bare \n terminates quotation
                if (p[-1] == '\\')
                    for (;;) {
                        if (*p++ == '\n') {
                            ++cpp.lineno[cpp.ifno];
                            break;
                        } // escaped \n ignored
                        if (eob(--p))
                            p = refill(p);
                        else {
                            ++p;
                            break;
                        }
                    }
                else if (eob(--p))
                    p = refill(p);
                else
                    ++p; // it was a different quote character
            }
        } break;
        case '\n': {
            ++cpp.lineno[cpp.ifno];
            if (isslo) {
                state = LF;
                return (p);
            }
        prevlf:
            state = BEG;
            for (;;) {
                if (*p++ == '#')
                    return (p);
                if (eob(cpp.inp = --p))
                    p = refill(p);
                else
                    goto again;
            }
        } break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            for (;;) {
                while (isnum(*p++))
                    ;
                if (eob(--p))
                    p = refill(p);
                else
                    break;
            }
            break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case '_':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':
#if scw1
#define tmac1(c, bit)      \
    if (!xmac1(c, bit, &)) \
    goto nomac
#define xmac1(c, bit, op) (cpp.macbit[(unsigned char)c] op(bit))
#else
#define tmac1(c, bit)
#define xmac1(c, bit, op)
#endif

#if scw2
#define tmac2(c0, c1, cpos)      \
    if (!xmac2(c0, c1, cpos, &)) \
    goto nomac
#define xmac2(c0, c1, cpos, op) \
    (cpp.macbit[t21[(unsigned char)c0] + t22[(unsigned char)c1]] op(t23 + cpos)[(unsigned char)c0])
#else
#define tmac2(c0, c1, cpos)
#define xmac2(c0, c1, cpos, op)
#endif

            if (cpp.flslvl)
                goto nomac;
            for (;;) {
                c = p[-1];
                tmac1(c, b0);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b1);
                tmac2(c, i, 0);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b2);
                tmac2(i, c, 1);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b3);
                tmac2(c, i, 2);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b4);
                tmac2(i, c, 3);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b5);
                tmac2(c, i, 4);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b6);
                tmac2(i, c, 5);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b7);
                tmac2(c, i, 6);
                tmac2(i, 0, 7);
                while (isid(*p++))
                    ;
                if (eob(--p)) {
                    refill(p);
                    p = cpp.inp + 1;
                    continue;
                }
                goto lokid;
            endid:
                if (eob(--p)) {
                    refill(p);
                    p = cpp.inp + 1;
                    continue;
                }
                tmac2(p[-1], 0, -1 + (p - cpp.inp));
            lokid:
                slookup(cpp.inp, p, 0);
                if (cpp.newp) {
                    p = cpp.newp;
                    goto again;
                } else
                    break;
            nomac:
                while (isid(*p++))
                    ;
                if (eob(--p)) {
                    p = refill(p);
                    goto nomac;
                } else
                    break;
            }
            break;
        } // end of switch

        if (isslo)
            return (p);
    } // end of infinite loop
}

//
// get next non-blank token
//
char *skipbl(char *p)
{
    do {
        cpp.outp = cpp.inp = p;
        p                  = cotoken(p);
    } while (cpp.toktyp[(unsigned char)*cpp.inp] == BLANK);
    return (p);
}

//
// take <= BUFSIZ chars from right end of buffer and put them on instack .
// slide rest of buffer to the right, update pointers, return new p.
//
char *unfill(char *p)
{
    char *np;
    const char *op;
    int d;

    if (cpp.mactop >= MAXFRE) {
        pperror("%s: too much pushback", cpp.macnam);
        p = cpp.inp = cpp.pend;
        dump(); // begin flushing pushback
        while (cpp.mactop > cpp.inctop[cpp.ifno]) {
            p = refill(p);
            p = cpp.inp = cpp.pend;
            dump();
        }
    }
    if (cpp.fretop > 0)
        np = cpp.bufstack[--cpp.fretop];
    else {
        np = cpp.savch;
        cpp.savch += BUFSIZ;
        if (cpp.savch >= cpp.sbf + SBSIZE) {
            pperror("no space");
            exit(cpp.exfail);
        }
        *cpp.savch++ = '\0';
    }
    cpp.instack[cpp.mactop] = np;
    op                      = cpp.pend - BUFSIZ;
    if (op < p)
        op = p;
    for (;;) {
        while ((*np++ = *op++))
            ;
        if (eob(op))
            break;
    } // out with old
    cpp.endbuf[cpp.mactop++] = np; // mark end of saved text
    np                       = cpp.pbuf + BUFSIZ;
    op                       = cpp.pend - BUFSIZ;
    cpp.pend                 = np;
    if (op < p)
        op = p;
    while (cpp.outp < op)
        *--np = *--op; // slide over new
    if (bob(np))
        pperror("token too long");
    d = np - cpp.outp;
    cpp.outp += d;
    cpp.inp += d;
    cpp.macdam += d;
    return (p + d);
}

char *doincl(char *p)
{
    int filok, inctype;
    char *cp;
    char **dirp, *nfil;
    char filname[BUFSIZ];

    p  = skipbl(p);
    cp = filname;
    if (*cpp.inp++ == '<') { // special <> syntax
        inctype = 1;
        for (;;) {
            cpp.outp = cpp.inp = p;
            p                  = cotoken(p);
            if (*cpp.inp == '\n') {
                --p;
                *cp = '\0';
                break;
            }
            if (*cpp.inp == '>') {
                *cp = '\0';
                break;
            }
            while (cpp.inp < p)
                *cp++ = *cpp.inp++;
        }
    } else if (cpp.inp[-1] == '"') { // regular "" syntax
        inctype = 0;
        while (cpp.inp < p)
            *cp++ = *cpp.inp++;
        if (*--cp == '"')
            *cp = '\0';
    } else {
        pperror("bad include syntax", 0);
        inctype = 2;
    }
    // flush current file to \n , then write \n
    ++cpp.flslvl;
    do {
        cpp.outp = cpp.inp = p;
        p                  = cotoken(p);
    } while (*cpp.inp != '\n');
    --cpp.flslvl;
    cpp.inp = p;
    dump();
    if (inctype == 2)
        return (p);
    // look for included file
    if (cpp.ifno + 1 >= MAXINC) {
        pperror("Unreasonable include nesting", 0);
        return (p);
    }
    if ((nfil = cpp.savch) > cpp.sbf + SBSIZE - BUFSIZ) {
        pperror("no space");
        exit(cpp.exfail);
    }
    filok = 0;
    for (dirp = cpp.dirs + inctype; *dirp; ++dirp) {
        if (filname[0] == '/' || **dirp == '\0')
            strcpy(nfil, filname);
        else {
            strcpy(nfil, *dirp);
            strcat(nfil, "/");
            strcat(nfil, filname);
        }
        if (0 < (cpp.fins[cpp.ifno + 1] = open(nfil, READ))) {
            filok   = 1;
            cpp.fin = cpp.fins[++cpp.ifno];
            break;
        }
    }
    if (filok == 0)
        pperror("Can't find include file %s", filname);
    else {
        cpp.lineno[cpp.ifno] = 1;
        cpp.fnames[cpp.ifno] = cp = nfil;
        while (*cp++)
            ;
        cpp.savch             = cp;
        cpp.dirnams[cpp.ifno] = cpp.dirs[0] = trmdir(copy(nfil));
        sayline();
        // save current contents of buffer
        while (!eob(p))
            p = unfill(p);
        cpp.inctop[cpp.ifno] = cpp.mactop;
    }
    return (p);
}

int equfrm(const char *a, const char *p1, char *p2)
{
    char c;
    int flag;
    c    = *p2;
    *p2  = '\0';
    flag = strcmp(a, p1);
    *p2  = c;
    return (flag == SAME);
}

//
// process '#define'
//
char *dodef(char *p)
{
    char *pin, *psav, *cf;
    char **pf, **qf;
    int b, c, params;
    struct symtab *np;
    char *oldval, *oldsavch;
    char *formal[MAXFRM]; // formal[n] is name of nth formal
    char formtxt[BUFSIZ]; // space for formal names

    if (cpp.savch > cpp.sbf + SBSIZE - BUFSIZ) {
        pperror("too much defining");
        return (p);
    }
    oldsavch = cpp.savch; // to reclaim space if redefinition
    ++cpp.flslvl;         // prevent macro expansion during 'define'
    p   = skipbl(p);
    pin = cpp.inp;
    if (cpp.toktyp[(unsigned char)*pin] != IDENT) {
        ppwarn("illegal macro name");
        while (*cpp.inp != '\n')
            p = skipbl(p);
        return (p);
    }
    np = slookup(pin, p, 1);
    if ((oldval = np->value))
        cpp.savch = oldsavch; // was previously defined
    b  = 1;
    cf = pin;
    while (cf < p) { // update macbit
        c = *cf++;
        xmac1(c, b, |=);
        b = (b + b) & 0xFF;
        if (cf != p)
            xmac2(c, *cf, -1 + (cf - pin), |=);
        else
            xmac2(c, 0, -1 + (cf - pin), |=);
    }
    params   = 0;
    cpp.outp = cpp.inp = p;
    p                  = cotoken(p);
    pin                = cpp.inp;
    if (*pin == '(') { // with parameters; identify the formals
        cf = formtxt;
        pf = formal;
        for (;;) {
            p   = skipbl(p);
            pin = cpp.inp;
            if (*pin == '\n') {
                --cpp.lineno[cpp.ifno];
                --p;
                pperror("%s: missing )", np->name);
                break;
            }
            if (*pin == ')')
                break;
            if (*pin == ',')
                continue;
            if (cpp.toktyp[(unsigned char)*pin] != IDENT) {
                c  = *p;
                *p = '\0';
                pperror("bad formal: %s", pin);
                *p = c;
            } else if (pf >= &formal[MAXFRM]) {
                c  = *p;
                *p = '\0';
                pperror("too many formals: %s", pin);
                *p = c;
            } else {
                *pf++ = cf;
                while (pin < p)
                    *cf++ = *pin++;
                *cf++ = '\0';
                ++params;
            }
        }
        if (params == 0)
            --params; // #define foo() ...
    } else if (*pin == '\n') {
        --cpp.lineno[cpp.ifno];
        --p;
    }

    // remember beginning of macro body, so that we can
    // warn if a redefinition is different from old value.
    //
    oldsavch = psav = cpp.savch;
    for (;;) { // accumulate definition until linefeed
        cpp.outp = cpp.inp = p;
        p                  = cotoken(p);
        pin                = cpp.inp;
        if (*pin == '\\' && pin[1] == '\n')
            continue; // ignore escaped lf
        if (*pin == '\n')
            break;
        if (params) { // mark the appearance of formals in the definiton
            if (cpp.toktyp[(unsigned char)*pin] == IDENT) {
                for (qf = pf; --qf >= formal;) {
                    if (equfrm(*qf, pin, p)) {
                        *psav++ = qf - formal + 1;
                        *psav++ = WARN;
                        pin     = p;
                        break;
                    }
                }
            } else if (*pin == '"' || *pin == '\'') { // inside quotation marks, too
                char quoc = *pin;
                for (*psav++ = *pin++; pin < p && *pin != quoc;) {
                    while (pin < p && !isid(*pin))
                        *psav++ = *pin++;
                    cf = pin;
                    while (cf < p && isid(*cf))
                        ++cf;
                    for (qf = pf; --qf >= formal;) {
                        if (equfrm(*qf, pin, cf)) {
                            *psav++ = qf - formal + 1;
                            *psav++ = WARN;
                            pin     = cf;
                            break;
                        }
                    }
                    while (pin < cf)
                        *psav++ = *pin++;
                }
            }
        }
        while (pin < p)
            *psav++ = *pin++;
    }
    *psav++ = params;
    *psav++ = '\0';
    if ((cf = oldval) != NULL) { // redefinition
        --cf;                    // skip no. of params, which may be zero
        while (*--cf)
            ;                              // go back to the beginning
        if (0 != strcmp(++cf, oldsavch)) { // redefinition different from old
            --cpp.lineno[cpp.ifno];
            ppwarn("%s redefined", np->name);
            ++cpp.lineno[cpp.ifno];
            np->value = psav - 1;
        } else
            psav = oldsavch; // identical redef.; reclaim space
    } else
        np->value = psav - 1;
    --cpp.flslvl;
    cpp.inp   = pin;
    cpp.savch = psav;
    return (p);
}

#define fasscan() cpp.ptrtab = cpp.fastab
#define sloscan() cpp.ptrtab = cpp.slotab

//
// find and handle preprocessor control lines
//
char *control(char *p)
{
    const struct symtab *np;
    for (;;) {
        fasscan();
        p = cotoken(p);
        if (*cpp.inp == '\n')
            ++cpp.inp;
        dump();
        sloscan();
        p          = skipbl(p);
        *--cpp.inp = SALT;
        cpp.outp   = cpp.inp;
        ++cpp.flslvl;
        np = slookup(cpp.inp, p, 0);
        --cpp.flslvl;
        if (np == cpp.defloc) { // define
            if (cpp.flslvl == 0) {
                p = dodef(p);
                continue;
            }
        } else if (np == cpp.incloc) { // include
            if (cpp.flslvl == 0) {
                p = doincl(p);
                continue;
            }
        } else if (np == cpp.ifnloc) { // ifndef
            ++cpp.flslvl;
            p  = skipbl(p);
            np = slookup(cpp.inp, p, 0);
            --cpp.flslvl;
            if (cpp.flslvl == 0 && np->value == 0)
                ++cpp.trulvl;
            else
                ++cpp.flslvl;
        } else if (np == cpp.ifdloc) { // ifdef
            ++cpp.flslvl;
            p  = skipbl(p);
            np = slookup(cpp.inp, p, 0);
            --cpp.flslvl;
            if (cpp.flslvl == 0 && np->value != 0)
                ++cpp.trulvl;
            else
                ++cpp.flslvl;
        } else if (np == cpp.eifloc) { // endif
            if (cpp.flslvl) {
                if (--cpp.flslvl == 0)
                    sayline();
            } else if (cpp.trulvl)
                --cpp.trulvl;
            else
                pperror("If-less endif", 0);
        } else if (np == cpp.elsloc) { // else
            if (cpp.flslvl) {
                if (--cpp.flslvl != 0)
                    ++cpp.flslvl;
                else {
                    ++cpp.trulvl;
                    sayline();
                }
            } else if (cpp.trulvl) {
                ++cpp.flslvl;
                --cpp.trulvl;
            } else
                pperror("If-less else", 0);
        } else if (np == cpp.udfloc) { // undefine
            if (cpp.flslvl == 0) {
                ++cpp.flslvl;
                p = skipbl(p);
                slookup(cpp.inp, p, DROP);
                --cpp.flslvl;
            }
        } else if (np == cpp.ifloc) { // if
#if tgp
            pperror(" IF not implemented, true assumed", 0);
            if (cpp.flslvl == 0)
                ++cpp.trulvl;
            else
                ++cpp.flslvl;
#else
            cpp.newp = p;
            if (cpp.flslvl == 0 && yyparse())
                ++cpp.trulvl;
            else
                ++cpp.flslvl;
            p = cpp.newp;
#endif
        } else if (np == cpp.lneloc) { // line
            if (cpp.flslvl == 0 && cpp.pflag == 0) {
                cpp.outp = cpp.inp = p;
                *--cpp.outp        = '#';
                while (*cpp.inp != '\n')
                    p = cotoken(p);
                continue;
            }
        } else if (*++cpp.inp == '\n')
            cpp.outp = cpp.inp; // allows blank line after #
        else
            pperror("undefined control", 0);
        // flush to lf
        ++cpp.flslvl;
        while (*cpp.inp != '\n') {
            cpp.outp = cpp.inp = p;
            p                  = cotoken(p);
        }
        --cpp.flslvl;
    }
}

struct symtab *stsym(const char *s)
{
    char buf[BUFSIZ];
    char *p;

    // make definition look exactly like end of #define line
    // copy to avoid running off end of world when param list is at end
    p = buf;
    while ((*p++ = *s++))
        ;
    p = buf;
    while (isid(*p++))
        ; // skip first identifier
    if (*--p == '=') {
        *p++ = ' ';
        while (*p++)
            ;
    } else {
        s = " 1";
        while ((*p++ = *s++))
            ;
    }
    cpp.pend = p;
    *--p     = '\n';
    sloscan();
    dodef(buf);
    return (cpp.lastsym);
}

// kluge
struct symtab *ppsym(const char *s)
{
    struct symtab *sp;

    cpp.cinit    = SALT;
    *cpp.savch++ = SALT;
    sp           = stsym(s);
    --sp->name;
    cpp.cinit = 0;
    return (sp);
}

void verror(const char *s, va_list ap)
{
    if (cpp.fnames[cpp.ifno][0]) {
        fprintf(stderr, "%s: ", cpp.fnames[cpp.ifno]);
    }
    fprintf(stderr, "%d: ", cpp.lineno[cpp.ifno]);
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
    ++cpp.exfail;
}

void pperror(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    verror(s, ap);
    va_end(ap);
}

void yyerror(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    verror(s, ap);
    va_end(ap);
}

void ppwarn(const char *s, ...)
{
    int fail = cpp.exfail;
    va_list ap;

    cpp.exfail = -1;
    va_start(ap, s);
    verror(s, ap);
    va_end(ap);
    cpp.exfail = fail;
}

struct symtab *lookup(char *namep, int enterf)
{
    const char *np, *snp;
    int c, i;
    int around;
    struct symtab *sp;

    // namep had better not be too long (currently, <=8 chars)
    np     = namep;
    around = 0;
    i      = cpp.cinit;
    while ((c = *np++))
        i += i + c;
    c = i; // c=i for usage on pdp11
    c %= symsiz;
    if (c < 0)
        c += symsiz;
    sp = &cpp.stab[c];
    while ((snp = sp->name)) {
        np = namep;
        while (*snp++ == *np) {
            if (*np++ == '\0') {
                if (enterf == DROP) {
                    sp->name[0] = DROP;
                    sp->value   = 0;
                }
                return (cpp.lastsym = sp);
            }
        }
        if (--sp < &cpp.stab[0]) {
            if (around) {
                pperror("too many defines", 0);
                exit(cpp.exfail);
            } else {
                ++around;
                sp = &cpp.stab[symsiz - 1];
            }
        }
    }
    if (enterf > 0)
        sp->name = namep;
    return (cpp.lastsym = sp);
}

struct symtab *slookup(char *p1, char *p2, int enterf)
{
    char *p3;
    char c2, c3;
    struct symtab *np;
    c2  = *p2;
    *p2 = '\0'; // mark end of token
    if ((p2 - p1) > 8)
        p3 = p1 + 8;
    else
        p3 = p2;
    c3  = *p3;
    *p3 = '\0'; // truncate to 8 chars or less
    if (enterf == 1)
        p1 = copy(p1);
    np  = lookup(p1, enterf);
    *p3 = c3;
    *p2 = c2;
    if (np->value != 0 && cpp.flslvl == 0)
        cpp.newp = subst(p2, np);
    else
        cpp.newp = 0;
    return (np);
}

char *subst(char *p, struct symtab *sp)
{
    char *ca, *vp;
    int params;
    char *actual[MAXFRM]; // actual[n] is text of nth actual
    char acttxt[BUFSIZ];  // space for actuals

    if (0 == (vp = sp->value))
        return (p);
    if ((p - cpp.macforw) <= cpp.macdam) {
        if (++cpp.maclvl > symsiz && !cpp.rflag) {
            pperror("%s: macro recursion", sp->name);
            return (p);
        }
    } else
        cpp.maclvl = 0; // level decreased
    cpp.macforw = p;
    cpp.macdam  = 0; // new target for decrease in level
    cpp.macnam  = sp->name;
    dump();
    if (sp == cpp.ulnloc) {
        vp    = acttxt;
        *vp++ = '\0';
        sprintf(vp, "%d", cpp.lineno[cpp.ifno]);
        while (*vp++)
            ;
    } else if (sp == cpp.uflloc) {
        vp    = acttxt;
        *vp++ = '\0';
        sprintf(vp, "\"%s\"", cpp.fnames[cpp.ifno]);
        while (*vp++)
            ;
    }
    if (0 != (params = *--vp & 0xFF)) { // definition calls for params
        char **pa;
        ca = acttxt;
        pa = actual;
        if (params == 0xFF)
            params = 1; // #define foo() ...
        sloscan();
        ++cpp.flslvl; // no expansion during search for actuals
        cpp.plvl = -1;
        do
            p = skipbl(p);
        while (*cpp.inp == '\n'); // skip \n too
        if (*cpp.inp == '(') {
            cpp.maclin = cpp.lineno[cpp.ifno];
            cpp.macfil = cpp.fnames[cpp.ifno];
            for (cpp.plvl = 1; cpp.plvl != 0;) {
                *ca++ = '\0';
                for (;;) {
                    cpp.outp = cpp.inp = p;
                    p                  = cotoken(p);
                    if (*cpp.inp == '(')
                        ++cpp.plvl;
                    if (*cpp.inp == ')' && --cpp.plvl == 0) {
                        --params;
                        break;
                    }
                    if (cpp.plvl == 1 && *cpp.inp == ',') {
                        --params;
                        break;
                    }
                    while (cpp.inp < p)
                        *ca++ = *cpp.inp++;
                    if (ca > &acttxt[BUFSIZ])
                        pperror("%s: actuals too long", sp->name);
                }
                if (pa >= &actual[MAXFRM])
                    ppwarn("%s: argument mismatch", sp->name);
                else
                    *pa++ = ca;
            }
        }
        if (params != 0)
            ppwarn("%s: argument mismatch", sp->name);
        while (--params >= 0)
            *pa++ = &""[1]; // null string for missing actuals
        --cpp.flslvl;
        fasscan();
    }
    for (;;) { // push definition onto front of input stack
        while (!iswarn(*--vp)) {
            if (bob(p)) {
                cpp.outp = cpp.inp = p;
                p                  = unfill(p);
            }
            *--p = *vp;
        }
        if (*vp == warnc) { // insert actual param
            ca = actual[*--vp - 1];
            while (*--ca) {
                if (bob(p)) {
                    cpp.outp = cpp.inp = p;
                    p                  = unfill(p);
                }
                *--p = *ca;
            }
        } else
            break;
    }
    cpp.outp = cpp.inp = p;
    return (p);
}

char *trmdir(char *s)
{
    char *p = s;
    while (*p++)
        ;
    --p;
    while (p > s && *--p != '/')
        ;
    if (p == s)
        *p++ = '.';
    *p = '\0';
    return (s);
}

STATIC char *copy(const char *s)
{
    char *old;

    old = cpp.savch;
    while ((*cpp.savch++ = *s++))
        ;
    return (old);
}

char *strdex(char *s, int c)
{
    while (*s)
        if (*s++ == c)
            return (--s);
    return (0);
}

int yywrap()
{
    return (1);
}

void usage()
{
    printf("Usage:\n");
    printf("    cpp [options] [infile [outfile]]\n");
    printf("Options:\n");
    printf("    -I path             Add path to the search list for header files\n");
    printf("    -D macro[=value]    Fake a definition at the beginning\n");
    printf("    -U macro            Undefine a macro at the beginning\n");
    printf("    -R                  Allow macro recursion\n");
    printf("    -P                  Inhibit generation of line markers\n");
    printf("    -C                  Do not discard comments\n");
    printf("    -E                  Ignored for compatibility\n");
}

int main(int argc, char *argv[])
{
    int i, c;
    char *p;
    char *tf, **cp2;

    // Fields that used to have static initializers referencing other fields;
    // set them up before anything runs (the instance is otherwise zeroed).
    cpp.savch  = cpp.sbf;
    cpp.predef = cpp.prespc;
    cpp.prund  = cpp.punspc;
    cpp.fin    = STDIN;
    cpp.nd     = 1;

    cpp.fout = stdout;
    p        = "_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    i        = 0;
    while ((c = *p++)) {
        cpp.fastab[(unsigned char)c] |= IB | NB | SB;
        cpp.toktyp[(unsigned char)c] = IDENT;
#if scw2
        // 53 == 63-10; digits rarely appear in identifiers,
        // and can never be the first char of an identifier.
        // 11 == 53*53/sizeof(macbit) .
        //
        ++i;
        t21[(unsigned char)c] = (53 * i) / 11;
        t22[(unsigned char)c] = i % 11;
#endif
    }
    p = "0123456789.";
    while ((c = *p++)) {
        cpp.fastab[(unsigned char)c] |= NB | SB;
        cpp.toktyp[(unsigned char)c] = NUMBR;
    }
    p = "\n\"'/\\";
    while ((c = *p++))
        cpp.fastab[(unsigned char)c] |= SB;
    p = "\n\"'\\";
    while ((c = *p++))
        cpp.fastab[(unsigned char)c] |= QB;
    p = "*\n";
    while ((c = *p++))
        cpp.fastab[(unsigned char)c] |= CB;
    cpp.fastab[(unsigned char)warnc] |= WB;
    cpp.fastab['\0'] |= CB | QB | SB | WB;
    for (i = ALFSIZ; --i >= 0;)
        cpp.slotab[i] = cpp.fastab[i] | SB;
    p = " \t\013\f\r"; // note no \n;	\v not legal for vertical tab?
    while ((c = *p++))
        cpp.toktyp[(unsigned char)c] = BLANK;
#if scw2
    for (t23[i = ALFSIZ + 7] = 1; --i >= 0;)
        if ((t23[i] = (t23 + 1)[i] << 1) == 0)
            t23[i] = 1;
#endif

    cpp.fnames[cpp.ifno = 0] = "";
    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            switch (argv[i][1]) {
            case 'P':
                cpp.pflag++;
            case 'E':
                continue;
            case 'R':
                ++cpp.rflag;
                continue;
            case 'C':
                cpp.passcom++;
                continue;
            case 'D':
                if (cpp.predef > cpp.prespc + NPREDEF) {
                    pperror("too many -D options, ignoring %s", argv[i]);
                    continue;
                }
                // ignore plain "-D" (no argument)
                if (*(argv[i] + 2))
                    *cpp.predef++ = argv[i] + 2;
                continue;
            case 'U':
                if (cpp.prund > cpp.punspc + NPREDEF) {
                    pperror("too many -U options, ignoring %s", argv[i]);
                    continue;
                }
                *cpp.prund++ = argv[i] + 2;
                continue;
            case 'I':
                if (cpp.nd > 8)
                    pperror("excessive -I file (%s) ignored", argv[i]);
                else
                    cpp.dirs[cpp.nd++] = argv[i] + 2;
                continue;
            case '\0':
                continue;
            default:
                pperror("unknown flag %s", argv[i]);
                continue;
            }
        default:
            if (cpp.fin == STDIN) {
                cpp.fin = open(argv[i], READ);
                if (cpp.fin < 0) {
                    pperror("No source file %s", argv[i]);
                    exit(8);
                }
                cpp.fnames[cpp.ifno] = copy(argv[i]);
                cpp.dirs[0] = cpp.dirnams[cpp.ifno] = trmdir(argv[i]);

                // too dangerous to have file name in same syntactic position
                // be input or output file depending on file redirections,
                // so force output to stdout, willy-nilly
                //      [i don't see what the problem is.  jfr]
                //
            } else if (cpp.fout == stdout) {
                static char sobuf[BUFSIZ];
                cpp.fout = fopen(argv[i], "w");
                if (!cpp.fout) {
                    pperror("Can't create %s", argv[i]);
                    exit(8);
                }
                fclose(stdout);
                setbuffer(cpp.fout, sobuf, sizeof(sobuf));
            } else
                pperror("extraneous name %s", argv[i]);
        }
    }
    if (isatty(cpp.fin)) {
        usage();
        exit(8);
    }

    cpp.fins[cpp.ifno] = cpp.fin;
    cpp.exfail         = 0;
    // after user -I files here are the standard include libraries
    cpp.dirs[cpp.nd++] = "/usr/include";
    cpp.dirs[cpp.nd++] = 0;
    cpp.defloc         = ppsym("define");
    cpp.udfloc         = ppsym("undef");
    cpp.incloc         = ppsym("include");
    cpp.elsloc         = ppsym("else");
    cpp.eifloc         = ppsym("endif");
    cpp.ifdloc         = ppsym("ifdef");
    cpp.ifnloc         = ppsym("ifndef");
    cpp.ifloc          = ppsym("if");
    cpp.lneloc         = ppsym("line");
    for (i = sizeof(cpp.macbit) / sizeof(cpp.macbit[0]); --i >= 0;)
        cpp.macbit[i] = 0;
#if unix
    cpp.ysysloc = stsym("unix");
#endif
#if gcos
    cpp.ysysloc = stsym("gcos");
#endif
#if ibm
    cpp.ysysloc = stsym("ibm");
#endif
#if pdp11
    cpp.varloc = stsym("pdp11");
#endif
#if vax
    cpp.varloc = stsym("vax");
#endif
#if interdata
    cpp.varloc = stsym("interdata");
#endif
#if tss
    cpp.varloc = stsym("tss");
#endif
#if os
    cpp.varloc = stsym("os");
#endif
#if mert
    cpp.varloc = stsym("mert");
#endif
    cpp.ulnloc = stsym("__LINE__");
    cpp.uflloc = stsym("__FILE__");

    tf                   = cpp.fnames[cpp.ifno];
    cpp.fnames[cpp.ifno] = "command line";
    cpp.lineno[cpp.ifno] = 1;
    cp2                  = cpp.prespc;
    while (cp2 < cpp.predef)
        stsym(*cp2++);
    cp2 = cpp.punspc;
    while (cp2 < cpp.prund) {
        if ((p = strdex(*cp2, '=')))
            *p++ = '\0';
        lookup(*cp2++, DROP);
    }
    cpp.fnames[cpp.ifno] = tf;
    cpp.pbeg             = cpp.buffer + 8;
    cpp.pbuf             = cpp.pbeg + BUFSIZ;
    cpp.pend             = cpp.pbuf + BUFSIZ;

    cpp.trulvl    = 0;
    cpp.flslvl    = 0;
    cpp.lineno[0] = 1;
    sayline();
    cpp.outp = cpp.inp = cpp.pend;
    control(cpp.pend);
    return (cpp.exfail);
}
