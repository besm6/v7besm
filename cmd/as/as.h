//
// Assembler for BESM-6.
// Shared declarations: the token kinds the lexer produces, the segment and
// directive numbers, the sizes of the in-memory tables, and the one big
// `struct assembler` that holds the whole assembler's state.  Every source
// file in cmd/as includes this header.
//
#include <stdint.h>
#include <stdnoreturn.h>

#include "assemble.h"
#include "besm6/b.out.h" // a.out object format: struct exec, N_* / R* codes, fputh()

#define W 6 // word length in bytes (a BESM-6 word is 48 bits = two 24-bit half-words)

// Token kinds returned by next_token().  Single-character tokens (operators,
// brackets, ...) are returned as their own ASCII code instead; these names
// cover everything that is not a single literal character.

#define LEOF    1  // end of input
#define LEOL    2  // end of line; value is the line number that just started
#define LNAME   3  // identifier; value is its index in the symbol table (stab)
#define LCMD    4  // machine instruction mnemonic; value is its index in table[]
#define LACMD   5  // assembler directive (.text, .word, ...); value is its code
#define LNUM    6  // integer literal; the value sits in as.intval
#define LLCMD   7  // raw long-address opcode (@NN); value is the opcode number
#define LSCMD   8  // raw short-address opcode ($NN); value is the opcode number
#define LLSHIFT 9  // the "\<" shift-left operator
#define LRSHIFT 10 // the "\>" shift-right operator
#define LINCR   11 // the "++" token
#define LDECR   12 // the "--" token

// Segment numbers.  A program is built up in several parallel segments; these
// index the per-segment arrays in struct assembler (sfile/rfile/count).  Slot 5
// is reserved (formerly the "abss" segment) to keep these aligned with the
// N_*/R* codes.

#define SCONST 0 // constant pool (de-duplicated literals)
#define STEXT  1 // text: machine code
#define SDATA  2 // initialized data
#define SSTRNG 3 // string constants (folded onto data at output time)
#define SBSS   4 // uninitialized data (reserved space, no image on disk)
#define SEXT   6 // pseudo-segment: an external (undefined) symbol reference
#define SABS   7 // pseudo-segment: a plain absolute value (degenerate case for parse_expr)

// Assembler directive codes (the value carried by an LACMD token).

#define ASCII 1  // .ascii  - emit a string constant
#define BSS   2  // .bss    - switch to the bss segment
#define COMM  3  // .comm   - common block
#define DATA  4  // .data   - switch to the data segment
#define GLOBL 5  // .globl  - mark a name as external (global)
#define HALF  6  // .half   - emit raw half-words (24 bits each)
#define STRNG 7  // .strng  - switch to the string-constant segment
#define TEXT  8  // .text   - switch to the text (code) segment
#define EQU   9  // .equ    - define a name = expression
#define WORD  10 // .word   - emit full 48-bit words

// Per-instruction flags stored in the `type` column of table[].

#define TLONG  01 // long-address instruction (15-bit address field)
#define TALIGN 02 // word-align the segment after emitting this instruction

// Table sizes.  The hash arrays use open addressing, so they are deliberately
// larger than the number of entries they hold; the hash size must be a power
// of two so SUPERHASH's bit-mask works.

#define HASHSZ 2048 // name (symbol) table hash size
#define HCONSZ 256  // constant pool hash size
#define HCMDSZ 1024 // machine-instruction hash size

#define STSIZE  (HASHSZ * 9 / 10) // max symbols (kept below the hash size to stay sparse)
#define CSIZE   (HCONSZ * 9 / 10) // max pooled constants
#define SPACESZ (STSIZE * 8)      // bytes of arena for symbol-name text

#define SRCNAME_MAX 1024 // max length of a source file name from a "# N \"file\"" marker

// Conversion tables wrapped as macros (the arrays live in tables.c).

#define SEGMTYPE(s) segmtype[s] // segment number -> symbol type (N_*)
#define TYPESEGM(s) typesegm[s] // symbol type   -> segment number (S*)
#define SEGMREL(s)  segmrel[s]  // segment number -> relocation type (R*)

// Pre-built instruction words used as fillers / building blocks.

#define EMPCOM 02200000L // empty instruction used to pad the text segment (utc 0)
#define UTCCOM 02200000L // the "<>" construct expands to this utc (opcode 022)
#define WTCCOM 02300000L // the "[]" construct expands to this wtc (opcode 023)

// Hash function used by every table here.  Multiplying by this constant and
// masking to the (power-of-two) table size scatters keys well.
// optimal hash multiplier for a 32-bit word == 011706736335L
// the same for a 16-bit word = 067433

#define SUPERHASH(key, mask) (((short)(key) * (short)067433) & (short)(mask))

// Character classification, driven by the bit flags in ctype[] (see tables.c).

#define ISHEX(c)    (ctype[(c) & 0377] & 1) // hexadecimal digit 0-9 A-F a-f
#define ISOCTAL(c)  (ctype[(c) & 0377] & 2) // octal digit 0-7
#define ISDIGIT(c)  (ctype[(c) & 0377] & 4) // decimal digit 0-9
#define ISLETTER(c) (ctype[(c) & 0377] & 8) // name character (letter, '.', '_', '$', high-bit)

// A BESM-6 word is 48 bits.  Values are carried in a single int64_t; these
// masks and accessors split a word into its two 24-bit half-words where the
// on-disk format needs them (high half = bits 25..48, low half = bits 1..24).

#define WORD_MASK (((int64_t)1 << 48) - 1) // 48-bit value mask
#define HALF_MASK 077777777L               // 24-bit half-word mask
#define HIHALF(w) ((long)(((w) >> 24) & HALF_MASK)) // high 24 bits as a long
#define LOHALF(w) ((long)((w) & HALF_MASK))         // low  24 bits as a long

// On the second pass the symbol-name hash table is no longer needed, so the
// same array is reused to remap symbol indices when -x/-X drops local symbols.
// "newindex" is just a readable alias for that reuse.

#define newindex as.hashtab

// One row of the machine-instruction table (tables.c).
struct table {
    long val;         // base opcode word (modifier and address get OR-ed in later)
    const char *name; // mnemonic, e.g. "xta"
    int type;         // TLONG / TALIGN flags
};

// One pooled constant.  Identical constants are stored once; see intern_constant().
struct constant {
    int64_t val; // the constant's 48-bit value
    long rel;    // relocation type for the constant (R* code, possibly with a symbol index)
};

// The entire mutable state of the assembler, in one struct so it is easy to
// reset between runs (the unit tests assemble many files in one process).
// The single instance is `as`, defined in as.c.
struct assembler {
    FILE *sfile[SABS]; // per-segment temp file holding the segment's code/data image
    FILE *rfile[SABS]; // per-segment temp file holding the matching relocation half-words
    long count[SABS];  // per-segment size, counted in 24-bit half-words

    int segm; // segment currently being assembled into (one of S*)

    char *infile;       // input file name (NULL = stdin)
    char *outfile;      // output file name
    char tfilename[14]; // template for the temp files: "/tmp/asXXXXXX"
    int line;           // current physical input line number (fallback for error messages)
    int srcline;        // source line from the last "# N \"file\"" marker (0 = none seen)
    char srcfile[SRCNAME_MAX]; // source file name from the last marker ("" = none seen)
    int debug;          // -d: debug flag

    int xflags; // -x: discard local symbols from the output
    int Xflag;  // -X: discard only locals whose name starts with '.'
    int uflag;  // -u: treat an undefined name as an error rather than external

    int stlength; // symbol-table size in bytes (computed in finalize_symtab)
    int stalign;  // zero padding bytes appended after the symbol table

    long cbase;  // const segment base address (word index), set in emit_segments
    long tbase;  // text  segment base address
    long dbase;  // data  segment base address
    long adbase; // string-constant segment base address
    long bbase;  // bss   segment base address

    struct nlist stab[STSIZE]; // the symbol table
    int stabfree;              // number of symbols used so far

    char space[SPACESZ]; // arena that holds the symbol-name strings
    int lastfree;        // bytes of the arena used so far

    int regleft; // index register written to the left of an instruction (the "N M" prefix)

    struct constant constab[CSIZE]; // the constant pool
    int nconst;                     // number of pooled constants

    char name[256];   // scratch buffer: the identifier/number text just scanned
    int64_t intval;   // scratch: the value of the integer literal just scanned (full 48-bit word)
    int extref;       // symbol index of the external name referenced by the current operand

    int blexflag; // a token has been pushed back (see unget_token / next_token)
    int backlex;  // the pushed-back token's value
    int blextype; // the pushed-back token's type

    int hashtab[HASHSZ];   // hash buckets for the symbol table (reused as newindex on pass 2)
    int hashctab[HCMDSZ];  // hash buckets for the machine-instruction table
    int hashconst[HCONSZ]; // hash buckets for the constant pool

    int aflag;   // -a: do not word-align after instructions
    int cmdmode; // lexer expects a machine instruction (so '+ - * /' may be part of a name)
};

extern struct assembler as; // the one global assembler state (as.c)

// Read-only tables (defined in tables.c).
extern const int ctype[256];       // character classification bit flags
extern const int segmtype[];       // segment number -> symbol type
extern const int segmrel[];        // segment number -> relocation type
extern const int typesegm[];       // symbol type -> segment number
extern const struct table table[]; // machine-instruction definitions

// Shared functions (each is documented at its definition).
noreturn void fatal(char *fmt, ...); // print an error and exit (main.c / test harness)
int next_token(int *pval);           // read one token, return its kind (lex.c)
void unget_token(int val, int type); // push one token back (lex.c)
void parse_line_marker(void);        // parse a cc-style "# N \"file\"" line marker (lex.c)
char *format_location(char *buf, int size); // render the "file:line: " diagnostic prefix (as.c)
long parse_expr(int *s);             // parse an expression, return value + segment (expr.c)
void init_hash_tables(void);         // build the instruction/symbol/constant hashes (symtab.c)
int lookup_directive(void);          // match as.name against the directive names (symtab.c)
int lookup_instruction(void);        // match as.name against the instruction table (symtab.c)
int lookup_name(void);               // find or create a symbol for as.name (symtab.c)
void align_segment(int s);           // word-align segment s (pass1.c)
void generate_code(void);            // pass 1: parse source into the segment temp files (pass1.c)
void finalize_symtab(void); // between passes: align segments, size the symbol table (pass2.c)
void write_header(void);    // emit the a.out header (pass2.c)
void emit_segments(void);   // pass 2: relocate and write const + code segments (pass2.c)
void write_reloc(void);     // emit the relocation records (pass2.c)
void write_symtab(void);    // emit the symbol table (pass2.c)
