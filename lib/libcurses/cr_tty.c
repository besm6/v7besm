// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Terminal type initialisation, and the flags computed at entry.  Almost entirely stolen
// from Bill Joy's ex version 2.6, as 4.xBSD's comment says.
//
// setterm() USED TO WRITE THROUGH ITS OWN ARGUMENT.  It ended `strncpy(ttytype,
// longname(genbuf, type), ...)' -- and longname() fills its SECOND argument, so that wrote
// the terminal's long name into `type', the caller's string.  initscr() passes Def_term on
// the My_term path, and Def_term is a string literal (curses.c): on this machine that is a
// word of the CONST POOL, and under a pure link (-n, NMAGIC) it is shared text that other
// processes are reading.  It now goes through a buffer of its own.  See also longname.c,
// whose copy is bounded now for the same class of reason.
//
// THE TIOCGWINSZ BLOCKS ARE GONE rather than left as #ifdefs that can never be true: there
// is no `struct winsize' and no TIOCGWINSZ in <sgtty.h> here.  LINES and COLS come from the
// `li#' and `co#' capabilities, or from the 24x80 default below, and nothing asks the
// driver how big the console is.  Same rule ../libtermcap dropped its termios shim under.
//
// `ospeed' is defined here and read by nobody -- see curses.c's header comment for the
// division of labour with ../libtermcap, which pads nothing and therefore declares neither
// this nor PC.
//
#include "internal.h"
#include <string.h>
#include <term.h>

static bool *sflags[] = { &AM, &BS, &DA, &DB, &EO, &HC, &HZ, &IN, &MI, &MS,
                          &NC, &NS, &OS, &UL, &XB, &XN, &XT, &XS, &XX };

static char *_PC,
    // The `pc' capability lands in _PC, whose slot in this table matches "pc" in the name
    // string below.  Do not compact the table: the two run in lockstep.
    **sstrs[] = { &AL, &BC, &BT, &CD, &CE, &CL, &CM, &CR, &CS,      &DC,      &DL,
                  &DM, &DO, &ED, &EI, &K0, &K1, &K2, &K3, &K4,      &K5,      &K6,
                  &K7, &K8, &K9, &HO, &IC, &IM, &IP, &KD, &KE,      &KH,      &KL,
                  &KR, &KS, &KU, &LL, &MA, &ND, &NL, &_PC, &RC,     &SC,      &SE,
                  &SF, &SO, &SR, &TA, &TE, &TI, &UC, &UE, &UP,      &US,      &VB,
                  &VS, &VE, &AL_PARM, &DL_PARM, &UP_PARM, &DOWN_PARM, &LEFT_PARM,
                  &RIGHT_PARM };

// Space for the decoded capability strings.  ONE KILOCHARACTER IS PROVABLY ENOUGH, where
// 4.xBSD allowed two: tgetent() bounds a whole entry at TBUFSIZ (1024) characters
// (../../include/term.h), and tgetstr() only ever DECODES -- `\E' to one byte, `^X' to one
// byte, `\101' to one byte -- so the decoded strings of one entry cannot together outrun
// the text they came from.  171 words rather than 342.
char _tspace[1024];

static char *aoftspace; // running allocation point within _tspace

// Where setterm() probes `cm' from.  Zero, deliberately: tgoto() answers "OOPS" for a
// capability it cannot substitute into, and the probe only wants to know which.
static int probecol, probeline;

short ospeed = -1;

// Read the tty modes and derive the flags that change how the cursor is moved.
void gettmode(void)
{
    if (ioctl(_tty_ch, TIOCGETP, (char *)&_tty) < 0)
        return;
    savetty();
    if (ioctl(_tty_ch, TIOCSETP, (char *)&_tty) < 0)
        _tty.sg_flags = _res_flg;
    ospeed   = _tty.sg_ospeed;
    _res_flg = _tty.sg_flags;
    GT       = ((_tty.sg_flags & XTABS) == 0);
    NONL     = ((_tty.sg_flags & CRMOD) == 0);
    _pfast   = NONL;
    _tty.sg_flags &= ~XTABS;
    ioctl(_tty_ch, TIOCSETP, (char *)&_tty);
}

// Pull every flag and string capability out of the entry tgetent() left behind.
static void zap(void)
{
    register char *namp;
    register bool **fp;
    register char ***sp;

    namp = "ambsdadbeohchzinmimsncnsosulxbxnxtxsxx";
    fp   = sflags;
    do {
        *(*fp++) = tgetflag(namp);
        namp += 2;
    } while (*namp);
    namp = "albcbtcdceclcmcrcsdcdldmdoedeik0k1k2k3k4k5k6k7k8k9hoicimipkdkekhklkrkskullmandnl"
           "pcrcscsesfsosrtatetiucueupusvbvsveALDLUPDOLERI";
    sp = sstrs;
    do {
        *(*sp++) = tgetstr(namp, &aoftspace);
        namp += 2;
    } while (*namp);
    if (XS)
        SO = SE = NULL;
    else {
        if (tgetnum("sg") > 0)
            SO = NULL;
        if (tgetnum("ug") > 0)
            US = NULL;
        if (!SO && US) {
            SO = US;
            SE = UE;
        }
    }
    if (DO && !NL)
        NL = DO;
}

int setterm(char *type)
{
    int unknown;
    static char genbuf[1024];
    static char lname[50]; // longname()'s target -- NEVER `type'; see the header comment

    if (type[0] == '\0')
        type = "xx";
    unknown = FALSE;
    if (tgetent(genbuf, type) != 1) {
        unknown++;
        strcpy(genbuf, "xx|dumb:");
    }

    if (LINES == 0)
        LINES = tgetnum("li");
    if (LINES <= 5)
        LINES = 24;

    if (COLS == 0)
        COLS = tgetnum("co");
    if (COLS <= 4)
        COLS = 80;

    aoftspace = _tspace;
    zap(); // get the terminal description

    // Handle the funny termcap capabilities.
    if (CS && SC && RC)
        AL = DL = "";
    if (AL_PARM && AL == NULL)
        AL = "";
    if (DL_PARM && DL == NULL)
        DL = "";
    if (IC && IM == NULL)
        IM = "";
    if (IC && EI == NULL)
        EI = "";
    if (!GT)
        BT = NULL; // if we cannot tab, we cannot backtab either

    if (tgoto(CM, probecol, probeline)[0] == 'O')
        CA = FALSE, CM = 0;
    else
        CA = TRUE;

    PC        = _PC ? _PC[0] : FALSE;
    aoftspace = _tspace;
    longname(genbuf, lname);
    strncpy(ttytype, lname, sizeof(ttytype) - 1);
    ttytype[sizeof(ttytype) - 1] = '\0';
    if (unknown)
        return ERR;
    return OK;
}

// Return a capability from the entry setterm() read, decoded into the same arena.
char *getcap(char *name)
{
    return tgetstr(name, &aoftspace);
}
