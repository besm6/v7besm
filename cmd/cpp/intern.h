//
// C preprocessor: private declarations shared across the split source files
// (cpp.c, buffer.c, scan.c, direct.c, macro.c, diag.c).  parser.c and yylex.c
// only need defs.h.
//
#include "defs.h"

#define STATIC

#define STDIN  0
#define STDOUT 1
#define STDERR 2
#define READ   0
#define WRITE  1
#define SALT   '#'
#define BLANK  1
#define IDENT  2
#define NUMBR  3

#define DROP   0xFE // special character not legal ASCII or EBCDIC
#define WARN   DROP
#define SAME   0
#define MAXFRM 31 // max number of formals/actuals to a macro

// scan-table selection and buffer-boundary predicates
#define in_slow_scan    (cpp.scan_tab == cpp.slow_tab)
#define is_special(a)   (cpp.scan_tab[(unsigned char)a] & SB)
#define at_buf_end(a)   ((a) >= cpp.buf_end)
#define at_buf_start(a) (cpp.buf_start >= (a))
#define set_fast_scan() cpp.scan_tab = cpp.fast_tab
#define set_slow_scan() cpp.scan_tab = cpp.slow_tab

//
// a superimposed code reduces the number of symbol-table lookups: scw1 tests
// single characters, scw2 tests adjacent pairs.  scw1 is cheap and enabled;
// scw2 is usually not worth its cost and is disabled.
//
#define scw1 1
#define scw2 0

#if scw1
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
#define tmac1(c, bit)      \
    if (!xmac1(c, bit, &)) \
    goto nomac
#define xmac1(c, bit, op) (cpp.macro_bits[(unsigned char)c] op(bit))
#else
#define tmac1(c, bit)
#define xmac1(c, bit, op)
#endif

#if scw2
#define tmac2(c0, c1, cpos)      \
    if (!xmac2(c0, c1, cpos, &)) \
    goto nomac
#define xmac2(c0, c1, cpos, op)                                                    \
    (cpp.macro_bits[pair_row[(unsigned char)c0] + pair_col[(unsigned char)c1]] op( \
        pair_bits + cpos)[(unsigned char)c0])
#else
#define tmac2(c0, c1, cpos)
#define xmac2(c0, c1, cpos, op)
#endif

// Marks formal-parameter references inside stored macro bodies (defined in cpp.c).
extern char warn_mark;

// buffer.c -- scan-buffer refill/spill and output
void emit_line_marker(void);
void flush_output(void);
char *refill_buffer(char *p);
char *spill_buffer(char *p);

// scan.c -- lexical scanner
char *scan_token(char *p);

// direct.c -- directive dispatch and -D/-U/builtin setup
char *process_directives(char *p);
struct symtab *define_symbol(const char *s);
struct symtab *install_directive(const char *s);

// macro.c -- macro definition, symbol table, expansion
char *do_define(char *p);
struct symtab *lookup_token(char *p1, char *p2, int enterf);
char *expand_macro(char *p, struct symtab *sp);

// diag.c -- diagnostics and small string helpers
char *dir_of(char *s);
STATIC char *save_string(const char *s);
char *find_char(char *s, int c);
