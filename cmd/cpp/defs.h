//
// Shared declarations for the C preprocessor.  Every source file includes this
// (directly, or via intern.h): it defines the one big state struct, the symbol
// table entry type, the sizing limits, and the character-class test macros.
//
#include <stdio.h>

//
// Token codes produced by the little lexer in yylex.c and consumed by the
// expression parser in parser.c when evaluating a "#if" line.  YYEOF/number/
// stop are housekeeping; the rest name multi-character operators that cannot be
// represented by their own ASCII character (a single-character operator such as
// '+' is passed through as its own character code instead).
//
enum {
    YYEOF,   // (unused) end-of-input marker
    number,  // a numeric literal; its value is left in cpp.tok_value
    stop,    // end of the #if expression (newline reached)
    DEFINED, // the "defined" operator
    EQ,      // ==
    NE,      // !=
    LE,      // <=
    GE,      // >=
    LS,      // << (left shift)
    RS,      // >> (right shift)
    ANDAND,  // &&
    OROR,    // ||
    UMINUS,  // unary minus (precedence marker)
};

//
// One entry in the macro/symbol hash table.  "name" points at the macro name
// (or is null for an empty slot); "value" points at the stored replacement
// text, or is null for a name that is known but has no definition.
//
struct symtab {
    char *name;
    char *value;
    char  predefined; // §6.10.8.4: reject #define/#undef of this name
};

#define ALFSIZ 256 // alphabet size: one table slot per possible byte value
// Width of the scan window a single token / logical source line may occupy.  Do
// not inherit <stdio.h>'s BUFSIZ (platform-dependent: 1024 on macOS): the C11
// §5.2.4.1 minimum of 4095 characters in a logical line requires a deterministic,
// larger value.  Stack arrays scale from this (e.g. exptxt[4*BUFSIZ] = 32 KB in
// expand_macro); that is fine here because expansion works by buffer pushback and
// rescan, not deep C recursion.  Keep it comfortably above 4095.
#undef BUFSIZ
#define BUFSIZ 8192
#define SBSIZE  65536 // bytes of "side buffer" for saved macro/definition text (holds every macro's name+body)
#define MAXINC  10    // maximum depth of nested #include files
#define MAXIF   64    // maximum depth of nested #if/#ifdef/#ifndef blocks
#define MAXFRE  14    // max buffers of macro pushback in flight at once
#define NPREDEF 20    // max -D / -U options accepted on the command line
#define SYMSIZ  6151  // number of slots in the symbol hash table (prime; holds the §5.2.4.1 min of 4095 macros)

//
// All mutable per-run state of the preprocessor, bundled into one instance
// (see cpp.c) so it is namespaced instead of scattered across file-scope
// globals.  Shared by cpp.c, parser.c and yylex.c.  Reading the buffer diagram
// at the top of buffer.c makes the "scan buffer and cursors" group clear.
//
struct cppstate {
    // scan buffer and cursors
    char *buf_start, *buf_mid, *buf_end; // low end / midpoint / high end of the live buffer
    char *out_ptr, *tok_ptr, *scan_ptr;  // output / current-token / scan-ahead cursors
    char hash_seed;                      // seed for lookup()'s hash (nonzero while in a directive)
    char arena[8 + BUFSIZ + BUFSIZ + 8]; // backing store the buffer bounds point into

    // character-class / scan tables (built at startup)
    char macro_bits[ALFSIZ + 11]; // superimposed-code filter: "could this be a macro name?"
    char char_class[ALFSIZ];      // BLANK / IDENT / NUMBR per character
    char fast_tab[ALFSIZ];        // normal-mode scan classification
    char slow_tab[ALFSIZ];        // #if-expression scan classification
    char *scan_tab;               // active table: fast_tab or slow_tab

    // side buffer for macro pushback (text saved when the buffer must be reused)
    char side_buf[SBSIZE];
    char *side_ptr;           // next free byte in side_buf; init: side_buf
    int push_top, free_top;   // stack tops: pushed buffers / reusable free buffers
    char *push_stack[MAXFRE]; // saved side-buffer chunks awaiting re-reading
    char *free_stack[MAXFRE]; // side-buffer chunks available for reuse
    char *push_end[MAXFRE];   // end pointer of each pushed chunk (it may contain nuls)

    // context of the macro call whose arguments are currently being collected
    int paren_level;     // '(' depth while scanning a function-macro's actual arguments
    int call_line;       // source line where that macro call began
    int recur_depth;     // expansions since the nesting level last dropped (recursion guard)
    int recur_bound_adj; // correction to recur_bound after the buffer shifts
    char *call_file;     // source file where the macro call began
    char *macro_name;    // name of the macro being expanded (for error messages)
    char *recur_bound;   // scan point that must be passed before nesting is judged to drop

    // blue paint (§6.10.3.4): macros whose expansion is currently being rescanned.
    // A macro on this stack is left un-expanded if its name recurs, then popped
    // when its region-end marker is scanned.  Each active macro appears at most
    // once, so the depth is bounded by the number of defined macros (SYMSIZ).
    struct symtab *paint_stack[SYMSIZ];
    int paint_top;

    // include stack: one slot per open file, indexed by inc_level
    int inc_push_top[MAXINC]; // push_stack index in effect when this file was entered
    int inc_fd[MAXINC];       // open file descriptor
    int line_no[MAXINC];      // current line number
    char *inc_file[MAXINC];   // file name (for line markers and errors)
    char *inc_dir[MAXINC];    // directory the file was found in (searched first for its #includes)
    int trig_nhold[MAXINC];   // trailing '?' count carried across a read boundary (per level, -trigraphs)
    char *search_dirs[10];    // #include search path: [0]=current dir, then -I dirs, then system
    int in_fd;                // fd of the file currently being read; init: STDIN
    FILE *out_file;           // where preprocessed text is written (usually stdout)
    int ndirs;                // number of entries filled in search_dirs; init: 1
    int inc_level;            // current #include nesting depth (0 = top-level file)

    // command-line option flags
    int opt_no_lines;      // -P: do not emit "# line" markers
    int opt_keep_comments; // -C: keep comments in the output instead of stripping them
    int opt_recurse;       // -R: allow a macro to expand recursively
    int opt_trigraphs;     // -trigraphs: translate the nine C11 trigraph sequences (phase 1)
    int opt_no_warnings;   // -w: suppress warnings (e.g. the per-trigraph conversion notice)

    // -D / -U options staged on the command line, applied once input starts
    char *pre_defs[NPREDEF];   // -D definitions
    char **pre_defs_end;       // one past the last stored -D; init: pre_defs
    char *pre_undefs[NPREDEF]; // -U names
    char **pre_undefs_end;     // one past the last stored -U; init: pre_undefs
    int exit_code;             // accumulated error count; becomes the process exit status

    // symbol table plus quick handles to the built-in directive/macro entries
    struct symtab symbols[SYMSIZ]; // the macro hash table
    struct symtab *last_sym;       // most recent lookup() result (cache for lookup_token)
    struct symtab *sym_define, *sym_undef, *sym_include, *sym_if, *sym_elif, *sym_else, *sym_endif,
        *sym_ifdef, *sym_ifndef, *sym_os, *sym_arch, *sym_line, *sym_error, *sym_pragma,
        *sym_pragma_op, *sym_line_macro, *sym_file_macro;

    // #if conditional nesting: how many enclosing blocks are taken vs skipped
    int true_level, false_level;

    // nonzero while a block comment '/* ... */' is being scanned, so an EOF that
    // arrives before the closing '*/' can be diagnosed (see refill_buffer)
    int in_block_comment;

    // per-#if-group state so #elif/#else take at most one branch.  if_top is the
    // current #if/#ifdef/#ifndef nesting depth; if_taken[if_top] is nonzero once a
    // branch in that group has been taken (or the whole group is nested inside a
    // skipped region), meaning no further branch may be taken.
    int if_top;
    char if_taken[MAXIF + 1];

    // scratch shared by the #if expression lexer (yylex.c) and parser (parser.c)
    int tok_value;  // value of the number token just scanned
    int look_token; // one-token lookahead: type of the next token
    int look_value; // value that went with the lookahead token
};

extern struct cppstate cpp; // the single global instance, defined in cpp.c

// Bits stored in fast_tab[] to classify each input character (see the is* macros).
#define IB 1  // identifier character (letter, digit or '_')
#define SB 2  // "special": stops the fast scanner so it can look closer
#define NB 4  // numeric digit
#define CB 8  // could begin a comment ('/')
#define QB 16 // quote character (' or ")
#define WB 32 // a macro_mark marker byte used inside stored macro bodies

#define ISID(a)   (cpp.fast_tab[(unsigned char)a] & IB) // is a an identifier char?
#define ISNUM(a)  (cpp.fast_tab[(unsigned char)a] & NB) // is a a digit?
#define ISCOM(a)  (cpp.fast_tab[(unsigned char)a] & CB) // could a start a comment?
#define ISQUO(a)  (cpp.fast_tab[(unsigned char)a] & QB) // is a a quote char?
#define ISWARN(a) (cpp.fast_tab[(unsigned char)a] & WB) // is a a macro_mark marker?

// Functions defined in the core files but also called from parser.c / yylex.c.
int lex_if_token(void);                         // scan one #if-expression token (yylex.c)
int eval_if(void);                              // evaluate a whole #if expression (parser.c)
char *skip_blanks(char *p);                     // advance past whitespace tokens (scan.c)
struct symtab *lookup(char *namep, int enterf); // find/insert a symbol (macro.c)
void pperror(const char *s, ...);               // report an error (diag.c)
void ppwarn(const char *s, ...);                // report a warning (diag.c)
