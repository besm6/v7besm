//
// Assembler for BESM-6.
// Shared declarations.
//
#include <stdnoreturn.h>

#include "besm6/b.out.h"

#define W 6 // word length in bytes

// token types

#define LEOF    1
#define LEOL    2
#define LNAME   3
#define LCMD    4
#define LACMD   5
#define LNUM    6
#define LLCMD   7
#define LSCMD   8
#define LLSHIFT 9
#define LRSHIFT 10
#define LINCR   11
#define LDECR   12

// segment numbers

#define SCONST 0
#define STEXT  1
#define SDATA  2
#define SSTRNG 3
#define SBSS   4
#define SEXT   6
#define SABS   7 // degenerate case for getexpr

// assembler directives

#define ACOMM 0
#define ASCII 1
#define BSS   2
#define COMM  3
#define DATA  4
#define GLOBL 5
#define HALF  6
#define STRNG 7
#define TEXT  8
#define EQU   9
#define WORD  10

// instruction types

#define TLONG  01  // long-address instruction
#define TALIGN 02  // align after the instruction

// table sizes
// hash sizes must be powers of two!

#define HASHSZ 2048 // name table hash size
#define HCONSZ 256  // constant segment hash size
#define HCMDSZ 1024 // assembler instruction hash size

#define STSIZE  (HASHSZ * 9 / 10) // name table size
#define CSIZE   (HCONSZ * 9 / 10) // constant segment size
#define SPACESZ (STSIZE * 8)      // size of array for names

#define SEGMTYPE(s) segmtype[s] // segment number to symbol type
#define TYPESEGM(s) typesegm[s] // symbol type to segment number
#define SEGMREL(s)  segmrel[s]  // segment number to relocation type

#define EMPCOM 02200000L // empty instruction - filler (utc 0)
#define UTCCOM 02200000L // the <> instruction (utc, opcode 022)
#define WTCCOM 02300000L // the [] instruction (wtc, opcode 023)

// optimal hash multiplier for a 32-bit word == 011706736335L
// the same for a 16-bit word = 067433

#define SUPERHASH(key, mask) (((short)(key) * (short)067433) & (short)(mask))

#define ISHEX(c)    (ctype[(c) & 0377] & 1)
#define ISOCTAL(c)  (ctype[(c) & 0377] & 2)
#define ISDIGIT(c)  (ctype[(c) & 0377] & 4)
#define ISLETTER(c) (ctype[(c) & 0377] & 8)

// on the second pass hashtab is unused; reuse it as newindex
// to reindex relocation when flag x or X is set

#define newindex hashtab

// Two halves of a 48-bit word.
struct word {
    long left, right;
};

// Table of machine instructions.
struct table {
    long val;
    const char *name;
    int type;
};

// Constant segment entry.
struct constent {
    long h, h2, hr2;
};

// Global state (defined in as.c).
extern FILE *sfile[SABS], *rfile[SABS];
extern long count[SABS];
extern int segm;
extern char *infile, *outfile;
extern char tfilename[];
extern int line;  // current line number
extern int debug; // debug flag
extern int xflags, Xflag, uflag;
extern int stlength; // symbol table length in bytes
extern int stalign;  // symbol table alignment
extern long cbase, tbase, dbase, adbase, bbase;
extern struct nlist stab[STSIZE];
extern int stabfree;
extern char space[SPACESZ]; // storage for symbol names
extern int lastfree;      // counter of used space
extern int regleft;       // register number to the left of the instruction
extern struct constent constab[CSIZE];
extern int nconst;
extern char name[256];
extern struct word intval;
extern int extref;
extern int blexflag, backlex, blextype;
extern int hashtab[HASHSZ], hashctab[HCMDSZ];
extern int hashconst[HCONSZ];
extern int aflag;   // don't align on word boundary
extern int cmdmode; // lexer expects a machine instruction (allow + - * / in name)

// Read-only tables (defined in tables.c).
extern const int ctype[256];
extern const int segmtype[];
extern const int segmrel[];
extern const int typesegm[];
extern const struct table table[];

// Shared functions.
noreturn void uerror(char *fmt, ...);
int getlex(int *pval);
void ungetlex(int val, int type);
long getexpr(int *s);
void hashinit(void);
int lookacmd(void);
int lookcmd(void);
int lookname(void);
void align(int s);
void pass1(void);
void middle(void);
void makeheader(void);
void pass2(void);
void makereloc(void);
void makesymtab(void);
