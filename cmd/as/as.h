//
// Assembler for BESM-6.
// Shared declarations.
//
#include <stdnoreturn.h>

#include "assemble.h"
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
#define SABS   7 // degenerate case for parse_expr

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

#define TLONG  01 // long-address instruction
#define TALIGN 02 // align after the instruction

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

#define newindex as.hashtab

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

// Global state of the assembler (defined in as.c).
struct assembler {
    FILE *sfile[SABS], *rfile[SABS];
    long count[SABS];
    int segm;
    char *infile, *outfile;
    char tfilename[14]; // "/tmp/asXXXXXX"
    int line;           // current line number
    int debug;          // debug flag
    int xflags, Xflag, uflag;
    int stlength; // symbol table length in bytes
    int stalign;  // symbol table alignment
    long cbase, tbase, dbase, adbase, bbase;
    struct nlist stab[STSIZE];
    int stabfree;
    char space[SPACESZ]; // storage for symbol names
    int lastfree;        // counter of used space
    int regleft;         // register number to the left of the instruction
    struct constent constab[CSIZE];
    int nconst;
    char name[256];
    struct word intval;
    int extref;
    int blexflag, backlex, blextype;
    int hashtab[HASHSZ], hashctab[HCMDSZ];
    int hashconst[HCONSZ];
    int aflag;   // don't align on word boundary
    int cmdmode; // lexer expects a machine instruction (allow + - * / in name)
};

extern struct assembler as;

// Read-only tables (defined in tables.c).
extern const int ctype[256];
extern const int segmtype[];
extern const int segmrel[];
extern const int typesegm[];
extern const struct table table[];

// Shared functions.
noreturn void fatal(char *fmt, ...);
int next_token(int *pval);
void unget_token(int val, int type);
long parse_expr(int *s);
void init_hash_tables(void);
int lookup_directive(void);
int lookup_instruction(void);
int lookup_name(void);
void align_segment(int s);
void generate_code(void);
void finalize_symtab(void);
void write_header(void);
void emit_segments(void);
void write_reloc(void);
void write_symtab(void);
