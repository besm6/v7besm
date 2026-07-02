//
// C preprocessor: private declarations shared across the split source files
// (cpp.c, buffer.c, scan.c, direct.c, macro.c, diag.c).  parser.c and yylex.c
// only need defs.h.
//
#include "defs.h"

#define STATIC // marks a function that is logically private but left global for now

#define STDIN  0   // file descriptor of standard input
#define STDOUT 1   // file descriptor of standard output
#define STDERR 2   // file descriptor of standard error
#define READ   0   // open() mode: read only
#define WRITE  1   // open() mode: write only
#define SALT   '#' // marker byte prefixed to built-in directive names ("#define", ...)
#define BLANK  1   // char_class value: whitespace
#define IDENT  2   // char_class value: identifier character
#define NUMBR  3   // char_class value: digit

#define DROP   0xFE // special character not legal ASCII or EBCDIC
#define WARN   DROP // same byte, used inside macro bodies to flag a formal-parameter slot
#define SAME   0    // strcmp() returns this when two strings are equal
#define MAXFRM 31   // max number of formals/actuals to a macro
#define VA_FLAG 0x80 // OR'd into a stored params byte: last formal is __VA_ARGS__ (absorbs trailing args)

// scan-table selection and buffer-boundary predicates
#define in_slow_scan    (cpp.scan_tab == cpp.slow_tab)        // scanning a #if expression?
#define is_special(a)   (cpp.scan_tab[(unsigned char)a] & SB) // char that stops the fast scan?
#define at_buf_end(a)   ((a) >= cpp.buf_end)                  // pointer a past the live buffer?
#define at_buf_start(a) (cpp.buf_start >= (a))                // pointer a before the live buffer?
#define set_fast_scan() cpp.scan_tab = cpp.fast_tab           // switch to normal scanning
#define set_slow_scan() cpp.scan_tab = cpp.slow_tab           // switch to #if-expression scanning

//
// A "superimposed code" is a cheap pre-test that answers "this identifier
// cannot be a macro name" without searching the symbol table.  As each macro is
// defined, bits are set in cpp.macro_bits[] for the characters it contains (and
// their positions); scanning an identifier then just ANDs those bits, and only
// falls back to a real lookup if every character could belong to some macro.
// scw1 tests single characters (cheap, enabled); scw2 also tests adjacent pairs
// (rarely worth the cost, disabled).
//
#define scw1 1 // enable the single-character superimposed-code test
#define scw2 0 // enable the character-pair test (off: usually not worth it)

#if scw1
// Position bits: the k-th character of an identifier contributes bit b(k) (the
// last character always uses b7).  A macro name "leaves its fingerprint" here.
#define b0 1
#define b1 2
#define b2 4
#define b3 8
#define b4 16
#define b5 32
#define b6 64
#define b7 128
#endif

#if scw1
// tmac1: during scanning, bail out to label "nomac" if character c cannot occur
// at this position in any macro name.  xmac1: test (op '&') or set (op '|=') the
// bit for character c in the single-character filter.
#define tmac1(c, bit)      \
    if (!xmac1(c, bit, &)) \
    goto nomac
#define xmac1(c, bit, op) (cpp.macro_bits[(unsigned char)c] op(bit))
#else
#define tmac1(c, bit)     // scw1 disabled: no-op
#define xmac1(c, bit, op) // scw1 disabled: no-op
#endif

#if scw2
// tmac2/xmac2: the same idea for adjacent character pairs (c0,c1).  Disabled.
#define tmac2(c0, c1, cpos)      \
    if (!xmac2(c0, c1, cpos, &)) \
    goto nomac
#define xmac2(c0, c1, cpos, op)                                                    \
    (cpp.macro_bits[pair_row[(unsigned char)c0] + pair_col[(unsigned char)c1]] op( \
        pair_bits + cpos)[(unsigned char)c0])
#else
#define tmac2(c0, c1, cpos)     // scw2 disabled: no-op
#define xmac2(c0, c1, cpos, op) // scw2 disabled: no-op
#endif

// Marks formal-parameter references inside stored macro bodies (defined in cpp.c).
extern char warn_mark;

// Marks a "#parameter" (stringize) reference inside stored macro bodies (cpp.c).
extern char stringize_mark;

// Marks a "##" (token-paste) parameter operand inside stored macro bodies (cpp.c).
extern char paste_mark;

// buffer.c -- scan-buffer refill/spill and output
void emit_line_marker(void);  // write a "# line file" marker to the output
void flush_output(void);      // write finished text from the buffer to the output file
char *refill_buffer(char *p); // top the buffer up from pushback or the input file
char *spill_buffer(char *p);  // save part of the buffer aside to make room for pushback

// scan.c -- lexical scanner
char *scan_token(char *p); // scan the next token, handling comments/strings/etc.

// direct.c -- directive dispatch and -D/-U/builtin setup
char *process_directives(char *p);               // the main "handle each # line" loop
struct symtab *define_symbol(const char *s);     // turn a "-D name=value" string into a macro
struct symtab *install_directive(const char *s); // register a built-in like "#define"

// macro.c -- macro definition, symbol table, expansion
char *do_define(char *p);                                    // parse and store a #define
struct symtab *lookup_token(char *p1, char *p2, int enterf); // look up the token in [p1,p2)
char *expand_macro(char *p, struct symtab *sp);              // expand a macro call in place

// diag.c -- diagnostics and small string helpers
char *dir_of(char *s);                   // reduce a path to its directory part
STATIC char *save_string(const char *s); // copy a string into the side buffer, return it
char *find_char(char *s, int c);         // like strchr: find character c in string s
