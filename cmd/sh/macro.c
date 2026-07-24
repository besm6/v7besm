/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Parameter and command substitution -- the $ and the backquote.
//
#include <unistd.h>

#include "defs.h"
#include "sym.h"

static CHAR quote;  // used locally
static CHAR quoted; // used locally

static INT getch(CHAR endch);
static void comsubst(void);
static void flush(INT ot);
static void skipto(CHAR endch);

//
// v7 declared this LOCAL STRING and never returned anything from it; both callers
// ignore the result, so it says void.
//
static void copyto(CHAR endch)
{
    CHAR c;

    while ((c = getch(endch)) != endch && c) {
        chkstak(staktop);
        pushstak(c | quote);
    }
    zerostak();
    if (c != endch)
        error(badsub);
}

//
// Skip characters up to }
//
static void skipto(CHAR endch)
{
    CHAR c;

    while ((c = readc()) && c != endch) {
        switch (c) {
        case SQUOTE:
            skipto(SQUOTE);
            break;

        case DQUOTE:
            skipto(DQUOTE);
            break;

        case DOLLAR:
            if (readc() == BRACE)
                skipto('}');
            break;
        }
    }
    if (c != endch)
        error(badsub);
}

static INT getch(CHAR endch)
{
    CHAR d;

retry:
    d = readc();
    if (!subchar(d))
        return d;

    if (d == DOLLAR) {
        INT c;
        if (c = readc(), dolchar(c)) {
            NAMPTR n = (NAMPTR)NIL;
            INT dolg = 0;
            BOOL bra;
            STRING argp, v;
            CHAR idb[2];
            STRING id = idb;

            //
            // AN OFFSET, IN AN INT -- see stak.h.  v7 kept relstak()'s result in the
            // same STRING variable it later used as a real address, which works only
            // where an offset and a pointer are the same bits.  Here they are not, so
            // the offset has a variable of its own and `argp' stays a pointer.
            //
            INT argrel;

            if ((bra = (c == BRACE)) != 0)
                c = readc();
            if (letter(c)) {
                argrel = relstak();
                while (alphanum(c)) {
                    chkstak(staktop);
                    pushstak(c);
                    c = readc();
                }
                zerostak();
                n = lookup(absstak(argrel));
                setstak(argrel);
                v     = n->namval;
                id    = n->namid;
                peekc = c | MARK;
            } else if (digchar(c)) {
                *id    = c;
                idb[1] = 0;
                if (astchar(c)) {
                    dolg = 1;
                    c    = '1';
                }
                c -= '0';
                // v7 spelled the last arm (STRING)(dolg=0), casting an integer to a
                // char *; on this machine that fabricates a fat pointer out of a
                // number.  Written out instead.
                if (c == 0) {
                    v = cmdadr;
                } else if (c <= dolc) {
                    v = dolv[c];
                } else {
                    dolg = 0;
                    v    = 0;
                }
            } else if (c == '$') {
                v = pidadr;
            } else if (c == '!') {
                v = pcsadr;
            } else if (c == '#') {
                v = dolladr;
            } else if (c == '?') {
                v = exitadr;
            } else if (c == '-') {
                v = flagadr;
            } else if (bra) {
                error(badsub);
            } else {
                goto retry;
            }

            c = readc();
            if (!defchar(c) && bra)
                error(badsub);

            argp = 0;
            if (bra) {
                if (c != '}') {
                    argrel = relstak();
                    if (((v == 0) ^ setchar(c)) != 0)
                        copyto('}');
                    else
                        skipto('}');
                    argp = absstak(argrel);
                }
            } else {
                peekc = c | MARK;
                c     = 0;
            }

            if (v) {
                if (c != '+') {
                    for (;;) {
                        while ((c = *v++) != 0) {
                            chkstak(staktop);
                            pushstak(c | quote);
                        }
                        if (dolg == 0 || (++dolg > dolc)) {
                            break;
                        } else {
                            v = dolv[dolg];
                            chkstak(staktop);
                            pushstak(SP | (*id == '*' ? quote : 0));
                        }
                    }
                }
            } else if (argp) {
                if (c == '?')
                    failed(id, *argp ? argp : badparam);
                else if (c == '=') {
                    if (n)
                        assign(n, argp);
                    else
                        error(badsub);
                }
            } else if (flags & setflg) {
                failed(id, badparam);
            }
            goto retry;
        } else {
            peekc = c | MARK;
        }
    } else if (d == endch) {
        return d;
    } else if (d == SQUOTE) {
        comsubst();
        goto retry;
    } else if (d == DQUOTE) {
        quoted++;
        quote ^= QUOTE;
        goto retry;
    }
    return d;
}

//
// Strip "" and do $ substitution.  Leaves the result on top of the stack.
//
STRING macro(STRING as)
{
    BOOL savqu = quoted;
    CHAR savq  = quote;
    SHFILEHDR fb;

    push((SHFILE)&fb);
    estabf(as);
    usestak();
    quote  = 0;
    quoted = 0;
    copyto(0);
    pop();
    if (quoted && (stakbot == staktop))
        pushstak(QUOTE);
    quote  = savq;
    quoted = savqu;
    return fixstak();
}

//
// Command substitution.
//
static void comsubst(void)
{
    SHFILEBLK cb;
    CHAR d;
    STKPTR savptr = fixstak();

    usestak();
    while ((d = readc()) != SQUOTE && d) {
        chkstak(staktop);
        pushstak(d);
    }

    {
        STRING argc;
        trim(argc = fixstak());
        push((SHFILE)&cb);
        estabf(argc);
    }
    {
        TREPTR t = makefork(FPOU, cmd(EOFSYM, MTFLG | NLFLG));
        INT pv[2];

        // this is done like this so that the pipe is open only when needed
        chkpipe(pv);
        initf(pv[INPIPE]);
        execute(t, 0, 0, pv);
        close(pv[OTPIPE]);
    }
    tdystak(savptr);
    staktop = movstr(savptr, stakbot);
    while ((d = readc()) != 0) {
        chkstak(staktop);
        pushstak(d | quote);
    }
    await(0);
    while (stakbot != staktop) {
        if ((*--staktop & STRIP) != NL) {
            ++staktop;
            break;
        }
    }
    pop();
}

#define CPYSIZ 512

//
// Copy `in' to `ot', expanding as it goes: the here-document with substitution.
//
void subst(INT in, INT ot)
{
    CHAR c;
    SHFILEBLK fb;
    INT count = CPYSIZ;

    push((SHFILE)&fb);
    initf(in);
    // DQUOTE used to stop it from quoting
    while ((c = (getch(DQUOTE) & STRIP)) != 0) {
        chkstak(staktop);
        pushstak(c);
        if (--count == 0) {
            flush(ot);
            count = CPYSIZ;
        }
    }
    flush(ot);
    pop();
}

static void flush(INT ot)
{
    write(ot, stakbot, staktop - stakbot);
    if (flags & execpr)
        write(output, stakbot, staktop - stakbot);
    staktop = stakbot;
}
