/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Command line decoding -- the parser.
//
// Every `switch' here has at least one DELIBERATE FALL-THROUGH, and they are all
// load-bearing: `&' falls into `;' so that a background command still joins the list,
// EOFSYM falls into `default' so that end-of-input is checked against the expected
// closing symbol, and `>>' falls into `<' so that the >& and >> forms share their
// operand handling.  v7 wrote them inside its SWITCH/IN/ENDSW macros, where they are
// invisible; each one is marked now.
//
#include "defs.h"
#include "sym.h"

static IOPTR inout(IOPTR lastio);
static void chkword(void);
static void chksym(INT sym);
static TREPTR term(INT flg);
static TREPTR makelist(INT type, TREPTR i, TREPTR r);
static TREPTR list(INT flg);
static REGPTR syncase(INT esym);
static TREPTR item(BOOL flag);
static INT skipnl(void);
static void prsym(INT sym);
_Noreturn static void synbad(void);

TREPTR makefork(INT flgs, TREPTR i)
{
    FORKPTR t;

    t          = (FORKPTR)getstak(FORKTYPE);
    t->forktyp = flgs | TFORK;
    t->forktre = i;
    t->forkio  = 0;
    return (TREPTR)t;
}

static TREPTR makelist(INT type, TREPTR i, TREPTR r)
{
    LSTPTR t;

    if (i == 0 || r == 0) {
        synbad();
    } else {
        t         = (LSTPTR)getstak(LSTTYPE);
        t->lsttyp = type;
        t->lstlef = i;
        t->lstrit = r;
    }
    return (TREPTR)t;
}

//
// cmd
//	empty
//	list
//	list & [ cmd ]
//	list [ ; cmd ]
//
TREPTR cmd(INT sym, INT flg)
{
    TREPTR i, e;

    i = list(flg);

    if (wdval == NL) {
        if (flg & NLFLG) {
            wdval = ';';
            chkpr(NL);
        }
    } else if (i == 0 && (flg & MTFLG) == 0) {
        synbad();
    }

    switch (wdval) {
    case '&':
        if (i)
            i = makefork(FINT | FPRS | FAMP, i);
        else
            synbad();
        // FALLTHROUGH -- a backgrounded command is still an element of the list

    case ';':
        if ((e = cmd(sym, flg | MTFLG)) != 0)
            i = makelist(TLST, i, e);
        break;

    case EOFSYM:
        if (sym == NL)
            break;
        // FALLTHROUGH -- otherwise end of file has to be reported as the wrong symbol

    default:
        if (sym)
            chksym(sym);
    }
    return i;
}

//
// list
//	term
//	list && term
//	list || term
//
static TREPTR list(INT flg)
{
    TREPTR r;
    INT b;

    r = term(flg);
    while (r && ((b = (wdval == ANDFSYM)) || wdval == ORFSYM))
        r = makelist((b ? TAND : TORF), r, term(NLFLG));
    return r;
}

//
// term
//	item
//	item |^ term
//
static TREPTR term(INT flg)
{
    TREPTR t;

    reserv++;
    if (flg & NLFLG)
        skipnl();
    else
        word();

    if ((t = item(TRUE)) != 0 && (wdval == '^' || wdval == '|'))
        return makelist(TFIL, makefork(FPOU, t), makefork(FPIN | FPCL, term(NLFLG)));
    else
        return t;
}

static REGPTR syncase(INT esym)
{
    skipnl();
    if (wdval == esym) {
        return 0;
    } else {
        REGPTR r  = (REGPTR)getstak(REGTYPE);
        r->regptr = 0;
        for (;;) {
            wdarg->argnxt = r->regptr;
            r->regptr     = wdarg;
            if (wdval || (word() != ')' && wdval != '|'))
                synbad();
            if (wdval == '|')
                word();
            else
                break;
        }
        r->regcom = cmd(0, NLFLG | MTFLG);
        if (wdval == ECSYM) {
            r->regnxt = syncase(esym);
        } else {
            chksym(esym);
            r->regnxt = 0;
        }
        return r;
    }
}

//
// item
//
//	( cmd ) [ < in  ] [ > out ]
//	word word* [ < in ] [ > out ]
//	if ... then ... else ... fi
//	for ... while ... do ... done
//	case ... in ... esac
//	begin ... end
//
static TREPTR item(BOOL flag)
{
    TREPTR t;
    IOPTR io;

    if (flag)
        io = inout((IOPTR)0);
    else
        io = 0;

    switch (wdval) {
    case CASYM: {
        t = (TREPTR)getstak(SWTYPE);
        chkword();
        ((SWPTR)t)->swarg = wdarg->argval;
        skipnl();
        chksym(INSYM | BRSYM);
        ((SWPTR)t)->swlst = syncase(wdval == INSYM ? ESSYM : KTSYM);
        ((SWPTR)t)->swtyp = TSW;
        break;
    }

    case IFSYM: {
        INT w;
        t                 = (TREPTR)getstak(IFTYPE);
        ((IFPTR)t)->iftyp = TIF;
        ((IFPTR)t)->iftre = cmd(THSYM, NLFLG);
        ((IFPTR)t)->thtre = cmd(ELSYM | FISYM | EFSYM, NLFLG);
        ((IFPTR)t)->eltre = ((w = wdval) == ELSYM ? cmd(FISYM, NLFLG)
                                                  : (w == EFSYM ? (wdval = IFSYM, item(0)) : 0));
        if (w == EFSYM)
            return t;
        break;
    }

    case FORSYM: {
        t                   = (TREPTR)getstak(FORTYPE);
        ((FORPTR)t)->fortyp = TFOR;
        ((FORPTR)t)->forlst = 0;
        chkword();
        ((FORPTR)t)->fornam = wdarg->argval;
        //
        // skipnl() RETURNS A VALUE, and this is the one place it is used.  v7 declared
        // it `LOCAL VOID', where VOID was a typedef for int -- so spelling VOID as void
        // during the port would have made this test read a discarded value and `for x
        // in a b c' would have quietly parsed as a bare `for x'.  It is INT here.
        //
        if (skipnl() == INSYM) {
            chkword();
            ((FORPTR)t)->forlst = (COMPTR)item(0);
            if (wdval != NL && wdval != ';')
                synbad();
            chkpr(wdval);
            skipnl();
        }
        chksym(DOSYM | BRSYM);
        ((FORPTR)t)->fortre = cmd(wdval == DOSYM ? ODSYM : KTSYM, NLFLG);
        break;
    }

    case WHSYM:
    case UNSYM: {
        t                 = (TREPTR)getstak(WHTYPE);
        ((WHPTR)t)->whtyp = (wdval == WHSYM ? TWH : TUN);
        ((WHPTR)t)->whtre = cmd(DOSYM, NLFLG);
        ((WHPTR)t)->dotre = cmd(ODSYM, NLFLG);
        break;
    }

    case BRSYM:
        t = cmd(KTSYM, NLFLG);
        break;

    case '(': {
        PARPTR p;
        p         = (PARPTR)getstak(PARTYPE);
        p->partre = cmd(')', NLFLG);
        p->partyp = TPAR;
        t         = makefork(0, (TREPTR)p);
        break;
    }

    default:
        if (io == 0)
            return 0;
        // FALLTHROUGH -- a bare redirection with no command is still a command node

    case 0: {
        ARGPTR argp;
        ARGPTR *argtail;
        ARGPTR *argset     = 0;
        INT keywd          = 1;
        t                  = (TREPTR)getstak(COMTYPE);
        ((COMPTR)t)->comio = io; // initial io chain
        argtail            = &(((COMPTR)t)->comarg);
        while (wdval == 0) {
            argp = wdarg;
            if (wdset && keywd) {
                argp->argnxt = (ARGPTR)argset;
                argset       = (ARGPTR *)argp;
            } else {
                *argtail = argp;
                argtail  = &(argp->argnxt);
                keywd    = flags & keyflg;
            }
            word();
            if (flag)
                ((COMPTR)t)->comio = inout(((COMPTR)t)->comio);
        }

        ((COMPTR)t)->comtyp = TCOM;
        ((COMPTR)t)->comset = (ARGPTR)argset;
        *argtail            = 0;
        return t;
    }
    }

    reserv++;
    word();
    if ((io = inout(io)) != 0) {
        t        = makefork(0, t);
        t->treio = io;
    }
    return t;
}

static INT skipnl(void)
{
    while (reserv++, word() == NL)
        chkpr(NL);
    return wdval;
}

static IOPTR inout(IOPTR lastio)
{
    INT iof;
    IOPTR iop;
    CHAR c;

    iof = wdnum;

    switch (wdval) {
    case DOCSYM:
        iof |= IODOC;
        break;

    case APPSYM:
    case '>':
        if (wdnum == 0)
            iof |= 1;
        iof |= IOPUT;
        if (wdval == APPSYM) {
            iof |= IOAPP;
            break;
        }
        // FALLTHROUGH -- a plain `>' shares `<'s handling of the &-and-> suffixes

    case '<':
        if ((c = nextc(0)) == '&')
            iof |= IOMOV;
        else if (c == '>')
            iof |= IORDW;
        else
            peekc = c | MARK;
        break;

    default:
        return lastio;
    }

    chkword();
    iop         = (IOPTR)getstak(IOTYPE);
    iop->ioname = wdarg->argval;
    iop->iofile = iof;
    if (iof & IODOC) {
        iop->iolst = iopend;
        iopend     = iop;
    }
    word();
    iop->ionxt = inout(lastio);
    return iop;
}

static void chkword(void)
{
    if (word())
        synbad();
}

static void chksym(INT sym)
{
    INT x = sym & wdval;

    if (((x & SYMFLG) ? x : sym) != wdval)
        synbad();
}

static void prsym(INT sym)
{
    if (sym & SYMFLG) {
        SYSPTR sp = reserved;
        while (sp->sysval && sp->sysval != sym)
            sp++;
        prs(sp->sysnam);
    } else if (sym == EOFSYM) {
        prs(endoffile);
    } else {
        if (sym & SYMREP)
            prc(sym);
        if (sym == NL)
            prs("newline");
        else
            prc(sym);
    }
}

_Noreturn static void synbad(void)
{
    prp();
    prs(synmsg);
    if ((flags & ttyflg) == 0) {
        prs(atline);
        prn(standin->flin);
    }
    prs(colon);
    prc(LQ);
    if (wdval)
        prsym(wdval);
    else
        prs(wdarg->argval);
    prc(RQ);
    prs(unexpected);
    newline();
    exitsh(SYNBAD);
}
