#include <stdio.h>

enum {
    YYEOF,
    number,
    stop,
    DEFINED,
    EQ,
    NE,
    LE,
    GE,
    LS,
    RS,
    ANDAND,
    OROR,
    UMINUS,
};

struct symtab {
    char *name;
    char *value;
};

#define ALFSIZ 256 // alphabet size
#ifndef BUFSIZ
#define BUFSIZ 512
#endif
#define SBSIZE  12000
#define MAXINC  10
#define MAXFRE  14 // max buffers of macro pushback
#define NPREDEF 20
#define symsiz  400

//
// All mutable per-run state of the preprocessor, bundled into one instance
// (see cpp.c) so it is namespaced instead of scattered across file-scope
// globals.  Shared by cpp.c, parser.c and yylex.c.
//
struct cppstate {
    // scan buffer and cursors
    char *buf_start, *buf_mid, *buf_end; // buffer bounds
    char *out_ptr, *tok_ptr, *scan_ptr;  // output / current-token / scan-ahead cursors
    char hash_seed;                      // seed for lookup()'s hash
    char arena[8 + BUFSIZ + BUFSIZ + 8]; // backing store for the scan buffer

    // character-class / scan tables (built at startup)
    char macro_bits[ALFSIZ + 11]; // superimposed-code filter for macro names
    char char_class[ALFSIZ];      // BLANK / IDENT / NUMBR per character
    char fast_tab[ALFSIZ];        // normal-mode scan classification
    char slow_tab[ALFSIZ];        // #if-expression scan classification
    char *scan_tab;               // active table: fast_tab or slow_tab

    // side buffer for macro pushback
    char side_buf[SBSIZE];
    char *side_ptr; // next free byte in side_buf; init: side_buf
    int push_top, free_top;
    char *push_stack[MAXFRE], *free_stack[MAXFRE], *push_end[MAXFRE];

    // in-flight macro call context
    int paren_level, call_line, recur_depth, recur_bound_adj;
    char *call_file, *macro_name, *recur_bound;

    // include stack
    int inc_push_top[MAXINC], inc_fd[MAXINC], line_no[MAXINC];
    char *inc_file[MAXINC], *inc_dir[MAXINC];
    char *search_dirs[10];
    int in_fd; // init: STDIN
    FILE *out_file;
    int ndirs; // init: 1
    int inc_level;

    // options
    int opt_no_lines, opt_keep_comments, opt_recurse;

    // predefined -D / -U staging
    char *pre_defs[NPREDEF];
    char **pre_defs_end; // init: pre_defs
    char *pre_undefs[NPREDEF];
    char **pre_undefs_end; // init: pre_undefs
    int exit_code;

    // symbol table + directive/keyword handles
    struct symtab symbols[symsiz];
    struct symtab *last_sym;
    struct symtab *sym_define, *sym_undef, *sym_include, *sym_if, *sym_else, *sym_endif, *sym_ifdef,
        *sym_ifndef, *sym_os, *sym_arch, *sym_line, *sym_line_macro, *sym_file_macro;

    // conditional-compilation nesting
    int true_level, false_level;

    // #if expression parser (parser.c / yylex.c)
    int tok_value, look_token, look_value;
};

extern struct cppstate cpp;

#define IB 1
#define SB 2
#define NB 4
#define CB 8
#define QB 16
#define WB 32

#define isid(a)   (cpp.fast_tab[(unsigned char)a] & IB)
#define isnum(a)  (cpp.fast_tab[(unsigned char)a] & NB)
#define iscom(a)  (cpp.fast_tab[(unsigned char)a] & CB)
#define isquo(a)  (cpp.fast_tab[(unsigned char)a] & QB)
#define iswarn(a) (cpp.fast_tab[(unsigned char)a] & WB)

int lex_if_token(void);
int eval_if(void);
char *skip_blanks(char *p);
struct symtab *lookup(char *namep, int enterf);
void pperror(const char *s, ...);
void ppwarn(const char *s, ...);
