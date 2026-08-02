/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// sed -- the storage.  Task C5e.
//
// v7 had no such file: sed.h DEFINED every one of these objects, in a header both sed0.c
// and sed1.c include, and the PDP-11 linker merged the two common blocks into one.  C11
// has no tentative definition across translation units and b6ld has no common symbols,
// so without this file each half of the program would get its own copy of ptrspace,
// respace, linebuf and the rest -- the compiler would compile a script into storage the
// executor never looks at, with no diagnostic from anything.  cmd/sh/glob.c is the same
// file for the same reason (cmd/sh/defs.h).
//
// The storage belongs to neither half, which is why it is here rather than in sed0.c:
// sed0.c is the compiler and sed1.c the executor, and every object below is written by
// one and read by the other.
//

#include "sed.h"

char CGMES[]  = "command garbled: %s\n";
char TMMES[]  = "Too much text: %s\n";
char LTL[]    = "Label too long: %s\n";
char AD0MES[] = "No addresses allowed: %s\n";
char AD1MES[] = "Only one address allowed: %s\n";

// The bit within a character-class byte.  Independent of CCLSIZE -- it serves the
// `c & 07' half of the index and is eight entries whatever the table's width.
char bittab[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

FILE *fin;
struct reptr *abuf[ABUFSIZE];
struct reptr **aptr;
char *lastre;
char ibuf[512];
char *cbp;
char *ebp;
char genbuf[LBSIZE];
char *loc1;
char *loc2;
char *locs;
char seof;
char *reend;
char *lbend;
char *hend;
char *lcomend;
struct reptr *ptrend;
int eflag;
int dolflag;
int sflag;
int jflag;
int numbra;
int delflag;

// v7 spelled this `long'.  A long is an int is one 41-bit word here (../README.md SS3),
// so the declaration says nothing it did not already say; the three `%ld' that printed it
// are `%d' now for the same reason.
int lnum;

char linebuf[LBSIZE + 1];
char holdsp[LBSIZE + 1];
char *spend;
char *hspend;
int nflag;
int gflag;
char *braelist[NBRA];
char *braslist[NBRA];
int tlno[NLINES];
int nlno;
char fname[WFILES][FNSIZE];
FILE *fcode[WFILES];
int nfiles;

char *cp;
struct reptr ptrspace[PTRSIZE];
struct reptr *rep;
char respace[RESIZE];
struct label ltab[LABSIZE];
struct label *lab;
struct label *labend;
struct label *labtab = ltab;

// v7 called this `f'.  A one-letter global in a two-file program is worth a name.
int infile;

int depth;   // { } nesting, at compile time
int redepth; // advance() recursion, at run time

int eargc;
char **eargv;

struct reptr **cmpend[DEPTH];
struct reptr *pending;
char bad;
char *badp;
char compfl;
int errflag;
