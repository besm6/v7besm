// @(#)dextern	4.2	(Berkeley)	3/21/86
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

// MANIFEST CONSTANT DEFINITIONS

// base of nonterminal internal numbers
#define NTBASE 010000

// internal codes for error and accept actions

#define ERRCODE    8190
#define ACCEPTCODE 8191

// The size profile, keyed on `besm6'.  v7 had three (HUGE/MEDIUM/TINY) and shipped
// the one that violates the asserts below.  README.md, "Sizes".
//
// The besm6 numbers are MEASURED, not guessed: at the host sizes these tables are
// 66,813 words of bss against a 28,672-word address space, and the values below are
// the six grammars C11-C17 will feed this yacc with a third to two thirds on top.
// Capacity does not enter the generated output, so both builds agree byte for byte.
// NTERMS alone is not cut to the measurement: below 128 a grammar loses eight-bit
// character literals (../README.md SS11).  README.md, "Building for the BESM-6".
#if besm6
#   define ACTSIZE  2200 // action table -- awk.g.y's optimizer used 1,727
#   define MEMSIZE  4800 // production and optimizer storage -- awk.g.y used 3,660
#   define NSTATES  320  // awk.g.y made 242
#   define NTERMS   144  // awk.g.y declares 95; above 128, so an eight-bit literal fits
#   define NPROD    200  // awk.g.y has 122 rules
#   define NNONTERM 48   // awk.g.y has 30
#   define TEMPSIZE 320  // >= NSTATES, and >= NTERMS + NNONTERM + 1
#   define CNAMSZ   2400 // the name arena: 400 words, six chars to each
#   define LSETSIZE 128  // awk.g.y made 98 distinct lookahead sets
#   define WSETSIZE 112  // bc.y used 88, the worst of the six
#else
#   define ACTSIZE  12000
#   define MEMSIZE  24000
#   define NSTATES  750
#   define NTERMS   300
#   define NPROD    600
#   define NNONTERM 300
#   define TEMPSIZE 1200
#   define CNAMSZ   5000
#   define LSETSIZE 600
#   define WSETSIZE 350
#endif

#define NAMESIZE 50
#define NTYPES   63

// Lookahead sets, sixteen bits to the int.  v7's WORD32 alternative shifted by
// 31, which a 41-bit int cannot do, and is gone rather than unselected.
#define SETBITS 16
#define TBITSET ((16 + NTERMS) / 16)

// bit packing macros (may be machine dependent)
#define BIT(a, i)    ((a)[(i) >> 4] & (1 << ((i) & 017)))
#define SETBIT(a, i) ((a)[(i) >> 4] |= (1 << ((i) & 017)))

// number of words needed to hold n+1 bits
#define NWORDS(n) (((n) + 16) / 16)

// The relationships which must hold -- a comment for thirty years, and TINY broke
// one of them.  temp1[] is indexed by state, by production and by token-plus-
// nonterminal, so its bound is the largest of the three.  README.md, "Sizes".
_Static_assert(TBITSET *SETBITS >= NTERMS + 1, "TBITSET ints must hold NTERMS+1 bits");
_Static_assert(WSETSIZE >= NNONTERM, "WSETSIZE >= NNONTERM");
_Static_assert(LSETSIZE >= NNONTERM, "LSETSIZE >= NNONTERM");
_Static_assert(TEMPSIZE >= NTERMS + NNONTERM + 1, "TEMPSIZE >= NTERMS + NNONTERM + 1");
_Static_assert(TEMPSIZE >= NSTATES, "TEMPSIZE >= NSTATES");
_Static_assert(TEMPSIZE >= NPROD, "TEMPSIZE >= NPROD");

// associativities

#define NOASC 0 // no assoc.
#define LASC  1 // left assoc.
#define RASC  2 // right assoc.
#define BASC  3 // binary assoc.

// flags for state generation

#define DONE          0
#define MUSTDO        1
#define MUSTLOOKAHEAD 2

// flags for a rule having an action, and being reduced

#define ACTFLAG 04
#define REDFLAG 010

// output parser flags
#define YYFLAG1 (-1000)

// macros for getting associativity and precedence levels

#define ASSOC(i)  ((i) & 03)
#define PLEVEL(i) (((i) >> 4) & 077)
#define TYPE(i)   ((i >> 10) & 077)

// macros for setting associativity and precedence levels

#define SETASC(i, j)  i |= j
#define SETPLEV(i, j) i |= (j << 4)
#define SETTYPE(i, j) i |= (j << 10)

// looping macros

#define TLOOP(i)     for (i = 1; i <= ntokens; ++i)
#define NTLOOP(i)    for (i = 0; i <= nnonter; ++i)
#define PLOOP(s, i)  for (i = s; i < nprod; ++i)
#define SLOOP(i)     for (i = 0; i < nstate; ++i)
#define WSBUMP(x)    ++x
#define WSLOOP(s, j) for (j = s; j < cwp; ++j)
#define ITMLOOP(i, p, q) \
    q = pstate[i + 1];   \
    for (p = pstate[i]; p < q; ++p)
#define SETLOOP(i) for (i = 0; i < tbitset; ++i)

// Bytes of stdio buffer per stream on the BESM-6.  yacc holds seven open at once
// and calls no malloc, so this IS its heap: seven default BUFSIZ buffers are 3,584
// words against the ~2,100 the profile leaves.  A whole number of words -- 6*171,
// as cmd/ld/intern.h's LDBUFSIZ.  README.md, "Building for the BESM-6".
#define YYBUFSIZ 1026

// Shrink one freshly opened stream's buffer; BESM-6 only, before any I/O on it.
void shrink_buffer(FILE *f);

// I/O descriptors.  ftemp and faction are tmpfile() streams, rewound between
// passes; v7 kept them as yacc.tmp and yacc.acts and reopened them by name.
// The three output names stay fixed.  README.md, "Temporary files".
extern FILE *finput;  // input file
extern FILE *faction; // temp stream for saving actions
extern FILE *fdefine; // file for # defines
extern FILE *ftable;  // y.tab.c file
extern FILE *ftemp;   // temp stream to pass 2
extern FILE *foutput; // y.output file

// structure declarations

struct looksets {
    int lset[TBITSET];
};

struct item {
    int *pitem;
    struct looksets *look;
};

struct toksymb {
    char *name;
    int value;
};

struct ntsymb {
    char *name;
    int tvalue;
};

struct wset {
    int *pitem;
    int flag;
    struct looksets ws;
};

// token information

extern int ntokens; // number of tokens
extern struct toksymb tokset[];
extern int toklev[]; // vector with the precedence of the terminals

// nonterminal information

extern int nnonter; // the number of nonterminals
extern struct ntsymb nontrst[];

// grammar rule information

extern int nprod;     // number of productions
extern int *prdptr[]; // pointers to descriptions of productions
extern int levprd[];  // contains production levels to break conflicts

// state information

extern int nstate;            // number of states
extern struct item *pstate[]; // pointers to the descriptions of the states
extern int tystate[];         // contains type information about the states
extern int defact[];          // the default action of the state
extern int tstates[];         // the states deriving each token
extern int ntstates[];        // the states deriving each nonterminal
extern int mstates[];         // the continuation of the chains begun in tstates and ntstates

// lookahead set information

extern struct looksets lkst[];
extern int nolook; // flag to turn off lookahead computations

// working set information

extern struct wset wsets[];
extern struct wset *cwp;

// storage for productions

extern int mem0[];
extern int *mem;

// storage for action table

extern int amem[];  // action table storage
extern int *memp;   // next free action table position
extern int indgo[]; // index to the stored goto table

// temporary vector, indexable by states, terms, or ntokens

extern int temp1[];
extern int lineno; // current line number

// statistics collection variables

extern int zzgoent;
extern int zzgobest;
extern int zzacent;
extern int zzexcp;
extern int zzclose;
extern int zzrrconf;
extern int zzsrconf;
// define functions with strange types...

extern char *cstash(const char *s);
extern struct looksets *flset(struct looksets *p);
extern char *symnam(int i);
extern char *writem(int *pp);

// default settings for a number of macros

// output file name

#ifndef OFILE
#define OFILE "y.tab.c"
#endif

// user output file name

#ifndef FILEU
#define FILEU "y.output"
#endif

// output file for # defines

#ifndef FILED
#define FILED "y.tab.h"
#endif

// Where the parser skeleton is: a path profile on the cc.c model, not the
// #define v7 left here.  README.md, "Finding the skeleton".
const char *find_parser(void);

void setup(int argc, char *argv[]);
void output(void);
void go2out(void);
void hideprod(void);
void callopt(void);
void error(char *fmt, ...);
void warray(const char *s, const int *v, int n);
void aryfil(int *v, int n, int c);
void closure(int i);
int apack(int *p, int n);
void putitem(int *ptr, struct looksets *lptr);
int state(int c);
