/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// sed -- the stream editor.  Task C5e; see README.md beside this file.
//
// THIS HEADER NO LONGER DEFINES THE PROGRAM'S GLOBALS.  v7 declared all forty-odd of
// them here -- `char genbuf[LBSIZE];' -- in a header both sed0.c and sed1.c include,
// and let the linker merge the two common blocks.  C11 has no tentative definition
// across translation units and b6ld has no common symbols, so the two halves would get
// SEPARATE STORAGE: sed0.c would compile a script into its ptrspace and sed1.c would
// execute an empty one, silently.  They are extern here and defined once in sedglob.c,
// which is what cmd/sh/defs.h and cmd/sh/glob.c already do for the same reason.
//
// (v7's copy also defined `reend', `lbend' and `depth' TWICE in this one file, which
// the common-symbol model swallowed and C11 does not.)
//

#ifndef SED_H
#define SED_H

#include <stdio.h>

// ---- the compiled-expression opcodes ------------------------------------------------

#define CBRA  1
#define CCHR  2
#define CDOT  4
#define CCL   6
#define CNL   8
#define CDOL  10
#define CEOF  11
#define CKET  12
#define CNULL 13
#define CLNUM 14
#define CEND  16
#define CDONT 17
#define CBACK 18

#define STAR 01

// ---- the ceilings -------------------------------------------------------------------

#define NLINES   256  // distinct line-number addresses
#define DEPTH    20   // nested { }
#define PTRSIZE  100  // commands in a script
#define ABUFSIZE 20   // pending a and r texts
#define LBSIZE   4000 // the pattern space, the hold space and one script line
#define LABSIZE  50   // labels
#define LABSZ    8    // characters in a label
#define NBRA     9    // \( \) groups

// RESIZE is the arena every compiled expression, replacement and text argument is carved
// out of.  v7's was 5000.  A character class is thirty-three bytes here rather than
// seventeen and a `y' table is 256 bytes rather than 128 (see CCLSIZE and YSIZE below),
// so the same script buys less; 8000 bytes is 1,334 words and restores the headroom.
#define RESIZE 8000

// ESIZE bounds ONE compiled expression within that arena.  128 in v7 and 512 here, for
// ed(1)'s two reasons (cmd/ed/README.md): a class costs 32 bytes now rather than 16, and
// a UTF-8 letter in a pattern costs four -- CCHR plus the byte, twice.
#define ESIZE 512

// CCLSIZE is the width of one character class, in bytes: 256 bits, one per byte value,
// where v7's was 128.  A class is a bitmap laid into the expression after the CCL opcode
// and indexed `ep[c >> 3] & bittab[c & 07]'.  Sixteen bytes hold c in 0..0177 and this
// machine's text is UTF-8 (../README.md SS11), so every byte value gets a bit of its own
// and `c >> 3' lands in 0..31 BY CONSTRUCTION -- which is what lets the two `& 0177'
// masks on the match side go away rather than be widened.  It also closed a wild store:
// the COMPILE side never masked c at all, and a plain char is unsigned here, so a pattern
// byte of 0300 stored at ep[24], eight bytes past a sixteen-byte class and on top of
// bytecode compile() had already written into the arena.  Changing this constant means
// changing nothing else -- compile(), advance() and the room check are all written in
// terms of it.
#define CCLSIZE 32

// YSIZE is the width of a `y' translation table, one entry per byte value.  v7's was 128,
// written nowhere as a number: it is the `!(c & 0200)' that ends the identity fill and
// the `ep + 0200' that reports the size, so grepping for the width found neither.  The
// execution side indexed it with an unmasked byte from the pattern space, so `y' on any
// byte above 0177 read 128 bytes past the table -- into whatever compile() had put next
// in the arena -- and wrote the result back into the line.
#define YSIZE 256

// The mark in a replacement text, and the whole of what item 4 of README.md cost.
// compsub() has to remember which bytes of an `s' replacement arrived backslash-escaped:
// `\&' is a literal ampersand where a bare `&' is the matched text, and `\1' is a
// back-reference where a bare `1' is a digit.  v7 recorded it by setting bit 0200 of the
// byte and dosub() stripped that bit from every OTHER byte, so a Cyrillic letter in a
// replacement came out as plausible ASCII.  The mark is a PREFIX BYTE now -- 0377, as
// ed(1)'s and the shell's are:
//
//      an ordinary byte c, c != QESC    c
//      an escaped byte \c               QESC c
//      a QESC byte, escaped or not      QESC QESC
//
// A bare QESC never appears -- compsub() only ever writes the pair -- so dosub()'s decode
// cannot be ambiguous.  Escaped and unescaped 0377 share one encoding for the reason they
// do in ed: 0377 is not a metacharacter in a replacement, so its escapedness cannot be
// observed by anything.
#define QESC 0377

// fcode[0] is stdout, so this allows the ten distinct wfile arguments sed.1.umm promises.
// v7 sized the arrays for twelve and tested for ten, which allowed nine.
#define WFILES 11
#define FNSIZE 40 // characters in a wfile name

// advance() recurses once per star operator that consumes something, so its depth is set
// by the compiled expression rather than, as it looks, by the length of the line.  The
// stack is 4,096 words at 070000 and NOTHING CHECKS IT (../README.md SS6).  grep(1) met
// this in task C5c and the failure is not the one a reader expects: past the ceiling the
// matcher returns a WRONG answer -- an empty one -- for a good while before it faults.  A
// real expression recurses two or three deep.
//
// TWELVE RATHER THAN grep's SIXTEEN, and the number is measured rather than copied.
// b6disasm puts one advance1() frame at 169 words (`15 utm 0251') and its counted wrapper
// at 7, so a level costs 176 of the 4,096 the stack has -- and the level the recursion
// starts from is not the same in the two paths that reach it.  An address match enters
// from execute() and is about 840 words in; an `s' command enters through command(),
// whose own frame is 540, and starts at about 1,010.  Measured with a probe in the
// wrapper, the deeper of the two faults at level 15 and the shallower at 18, so grep's
// sixteen would have been over the edge on one path and under it on the other.
//
// AND THE DIAGNOSTIC ITSELF NEEDS THE STACK: _doprnt's frame is 281 words, so a limit set
// where the recursion just fits is a limit that faults while PRINTING that it was
// reached.  Twelve levels end about 1,150 words short of the guard, which is the room the
// message wants.  Ask what a program still has to do after it has decided to stop.
#define MAXDEPTH 12

// ---- the command codes --------------------------------------------------------------

#define ACOM  01
#define BCOM  020
#define CCOM  02
#define CDCOM 025
#define CNCOM 022
#define COCOM 017
#define CPCOM 023
#define DCOM  03
#define ECOM  015
#define EQCOM 013
#define FCOM  016
#define GCOM  027
#define CGCOM 030
#define HCOM  031
#define CHCOM 032
#define ICOM  04
#define LCOM  05
#define NCOM  012
#define PCOM  010
#define QCOM  011
#define RCOM  06
#define SCOM  07
#define TCOM  021
#define WCOM  014
#define CWCOM 024
#define YCOM  026
#define XCOM  033

// ---- one compiled command -----------------------------------------------------------

// v7 wrote this as a union of two structs that differed in exactly one member -- `char
// *re1' against `union reptr *lb1' -- and then selected members through the union
// itself, `rep->ad1', which no C11 compiler accepts.  Naming the one member that really
// does alias is both valid and honest, and it is not a cosmetic point here: a char * is
// a FAT pointer (bit-48 marker, byte offset, word address) where a struct pointer is a
// bare word address, so the two members genuinely have different representations and
// only the commands that write re1 may read it.
struct reptr {
    char *ad1;
    char *ad2;
    union {
        char *re1;         // a, c, i, r, s and y: text or a compiled expression
        struct reptr *lb1; // b, t, { and D: the branch target
    } u;
    char *rhs;
    FILE *fcode;
    char command;
    char gfl;
    char pfl;
    char inar;
    char negfl;
};

struct label {
    char asc[LABSZ + 1];
    struct reptr *chain;
    struct reptr *address;
};

// ---- the globals, defined in sedglob.c ----------------------------------------------

extern FILE *fin;
extern struct reptr *abuf[ABUFSIZE];
extern struct reptr **aptr;
extern char *lastre;
extern char ibuf[512];
extern char *cbp;
extern char *ebp;
extern char genbuf[LBSIZE];
extern char *loc1;
extern char *loc2;
extern char *locs;
extern char seof;
extern char *reend;
extern char *lbend;
extern char *hend;
extern char *lcomend;
extern struct reptr *ptrend;
extern int eflag;
extern int dolflag;
extern int sflag;
extern int jflag;
extern int numbra;
extern int delflag;
extern int lnum;
extern char linebuf[LBSIZE + 1];
extern char holdsp[LBSIZE + 1];
extern char *spend;
extern char *hspend;
extern int nflag;
extern int gflag;
extern char *braelist[NBRA];
extern char *braslist[NBRA];
extern int tlno[NLINES];
extern int nlno;
extern char fname[WFILES][FNSIZE];
extern FILE *fcode[WFILES];
extern int nfiles;

extern char *cp;
extern struct reptr ptrspace[PTRSIZE];
extern struct reptr *rep;
extern char respace[RESIZE];
extern struct label ltab[LABSIZE];
extern struct label *lab;
extern struct label *labend;
extern struct label *labtab;

extern int infile; // v7 called this `f'
extern int depth;
extern int redepth;

extern int eargc;
extern char **eargv;

extern char bittab[];
extern struct reptr **cmpend[DEPTH];
extern struct reptr *pending;
extern char *badp;
extern char bad;
extern char compfl;

extern char CGMES[];
extern char TMMES[];
extern char LTL[];
extern char AD0MES[];
extern char AD1MES[];

// ---- the only function either half calls in the other ------------------------------
//
// Everything else in sed0.c and sed1.c is static to its own file: the compiler and the
// executor share storage, not entry points.

void execute(char *file);

// Set when an input file could not be opened, and the whole of what it does is make the
// exit status 2.  v7 printed `Can't open' and exited 0, so a script could not tell a
// missing file from an empty one.
extern int errflag;

#endif /* SED_H */
