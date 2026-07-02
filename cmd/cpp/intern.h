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
#define SAME   0    // strcmp() returns this when two strings are equal
#define MAXFRM 127  // max number of formals/actuals to a macro (§5.2.4.1 minimum; also the
                    // encoding ceiling: the param-number byte and the VA_FLAG=0x80 params-count
                    // byte both top out at 127, so a 128th formal is rejected, not misencoded)
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
// scw1 tests single characters (cheap, enabled).
//
#define scw1 1 // enable the single-character superimposed-code test

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

// Marker bytes stored inside macro bodies (and, for PAINT_END_MARK, the live scan
// buffer).  Their values sit outside legal ASCII/EBCDIC so they cannot collide with
// real source characters.  As an enum these constants are int-typed, so (char may be
// signed on this host) writing one into a char buffer needs a (char) cast, and a byte
// read back must be cast to (unsigned char) before comparing it; see scan.c/macro.c.
enum macro_mark {
    WARN_MARK        = 0xFE, // a formal-parameter slot: insert the expanded actual
    STRINGIZE_MARK   = 0xFD, // '#param': insert the quoted raw actual
    PASTE_MARK       = 0xFC, // '##' operand: insert the raw actual, fused to its neighbor
    PAINT_END_MARK   = 0xFB, // §6.10.3.4 end of a macro's expansion region (blue paint)
    COMMA_PASTE_MARK = 0xFA, // GNU ", ## __VA_ARGS__": raw actual, drop the comma if empty
};

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
char *expand_text(const char *a0, const char *a1, char *out, int cap); // fully expand [a0,a1) into out

// diag.c -- diagnostics and small string helpers
char *dir_of(char *s);                   // reduce a path to its directory part
STATIC char *save_string(const char *s); // copy a string into the side buffer, return it
char *find_char(char *s, int c);         // like strchr: find character c in string s
