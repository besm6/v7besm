// Copyright (c) 1980 Regents of the University of California.
// All rights reserved.  The Berkeley software License Agreement
// specifies the terms and conditions for redistribution.

//
// Definitions of the library's globals.  Every one of them is declared in <curses.h> or
// internal.h; this is the single translation unit that gives them storage.
//
// TWO OF THEM ARE DEAD, AND DELIBERATELY SO.  `PC' here and `ospeed' in cr_tty.c are the
// pad character and the line speed, which in 4.xBSD belong to termlib and are read by
// tputs().  ../libtermcap/tputs.c emits no padding at all -- nothing on this machine can be
// overrun -- so ../../include/term.h declares neither name and leaves both to this file, as
// 4.xBSD's curses had them.  setterm() writes PC from the `pc' capability and nothing ever
// reads it.  Two words; keeping them costs nothing and keeps this library linkable against
// a future termcap that does pad.
//
// `normtty' is 4.xBSD's and has no writer here either: tset(1) was what set it, and there
// is no tset in this tree.
//
#include "internal.h"

bool _echoit = TRUE,  // set if stty indicates ECHO
    _rawmode = FALSE, // set if stty indicates RAW mode
    My_term  = FALSE, // set if the user specifies the terminal type
    _endwin  = FALSE; // set if endwin() has been called

char ttytype[50];              // long name of the tty
char *Def_term    = "unknown"; // default terminal type

int _tty_ch = 1, // file channel which is a tty
    LINES,       // number of lines allowed on screen
    COLS,        // number of columns allowed on screen
    _res_flg;    // sgtty flags, saved by savetty() for resetting later

WINDOW *stdscr = NULL, *curscr = NULL;

SGTTY _tty; // tty modes

bool AM, BS, CA, DA, DB, EO, HC, HZ, IN, MI, MS, NC, NS, OS, UL, XB, XN, XT, XS, XX;
char *AL, *BC, *BT, *CD, *CE, *CL, *CM, *CR, *CS, *DC, *DL, *DM, *DO, *ED, *EI, *K0, *K1,
    *K2, *K3, *K4, *K5, *K6, *K7, *K8, *K9, *HO, *IC, *IM, *IP, *KD, *KE, *KH, *KL, *KR, *KS,
    *KU, *LL, *MA, *ND, *NL, *RC, *SC, *SE, *SF, *SO, *SR, *TA, *TE, *TI, *UC, *UE, *UP, *US,
    *VB, *VS, *VE, *AL_PARM, *DL_PARM, *UP_PARM, *DOWN_PARM, *LEFT_PARM, *RIGHT_PARM;
char PC; // see the header comment: written by setterm(), read by nothing

// From the tty modes, via gettmode().
bool GT, NONL, normtty, _pfast;
