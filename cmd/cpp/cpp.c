// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// C preprocessor
// written by John F. Reiser
// July/August 1978
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

//
// The one and only instance of the preprocessor's mutable state, declared in
// defs.h.  Bundles what used to be ~40 file-scope globals.
//
struct cppstate cpp;

// The byte written into a stored macro body to flag "a parameter goes here".
char warn_mark = WARN;

//
// Print the command-line help and the meaning of each option.
//
void usage()
{
    printf("Usage:\n");
    printf("    cpp [options] [infile [outfile]]\n");
    printf("Options:\n");
    printf("    -I path             Add path to the search list for header files\n");
    printf("    -D macro[=value]    Fake a definition at the beginning\n");
    printf("    -U macro            Undefine a macro at the beginning\n");
    printf("    -R                  Allow macro recursion\n");
    printf("    -P                  Inhibit generation of line markers\n");
    printf("    -C                  Do not discard comments\n");
    printf("    -E                  Ignored for compatibility\n");
}

//
// Program entry point.  Startup happens in a fixed order:
//   1. seed the state fields that need a non-zero initial value;
//   2. build the character-classification tables (fast_tab / slow_tab /
//      char_class) that drive the scanner;
//   3. parse the command line (-I/-D/-U/-P/-C/-R and the input/output files);
//   4. register the built-in directives (#define, #if, ...) and predefined
//      macros (__LINE__, __FILE__, and any -D options), and apply -U removals;
//   5. set the buffer pointers and hand control to process_directives, which
//      runs until end of input.
// The process exit status is the number of errors reported.
//
int main(int argc, char *argv[])
{
    int i, c;
    char *p;
    char *tf, **cp2;

    // 1. Fields that used to have static initializers referencing other fields;
    // set them up before anything runs (the instance is otherwise zeroed).
    cpp.side_ptr       = cpp.side_buf;
    cpp.pre_defs_end   = cpp.pre_defs;
    cpp.pre_undefs_end = cpp.pre_undefs;
    cpp.in_fd          = STDIN;
    cpp.ndirs          = 1;

    cpp.out_file = stdout;

    // 2. Build the scan tables: mark which bytes are identifier chars, digits,
    // quotes, comment starters, whitespace, etc.  The scanner then classifies a
    // character with a single table lookup.
    p = "_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    i = 0;
    while ((c = *p++)) {
        cpp.fast_tab[(unsigned char)c] |= IB | NB | SB;
        cpp.char_class[(unsigned char)c] = IDENT;
#if scw2
        // 53 == 63-10; digits rarely appear in identifiers,
        // and can never be the first char of an identifier.
        // 11 == 53*53/sizeof(macro_bits) .
        //
        ++i;
        pair_row[(unsigned char)c] = (53 * i) / 11;
        pair_col[(unsigned char)c] = i % 11;
#endif
    }
    p = "0123456789.";
    while ((c = *p++)) {
        cpp.fast_tab[(unsigned char)c] |= NB | SB;
        cpp.char_class[(unsigned char)c] = NUMBR;
    }
    p = "\n\"'/\\";
    while ((c = *p++))
        cpp.fast_tab[(unsigned char)c] |= SB;
    p = "\n\"'\\";
    while ((c = *p++))
        cpp.fast_tab[(unsigned char)c] |= QB;
    p = "*\n";
    while ((c = *p++))
        cpp.fast_tab[(unsigned char)c] |= CB;
    cpp.fast_tab[(unsigned char)warn_mark] |= WB;
    cpp.fast_tab['\0'] |= CB | QB | SB | WB;
    for (i = ALFSIZ; --i >= 0;)
        cpp.slow_tab[i] = cpp.fast_tab[i] | SB;
    p = " \t\013\f\r"; // note no \n;	\v not legal for vertical tab?
    while ((c = *p++))
        cpp.char_class[(unsigned char)c] = BLANK;
#if scw2
    for (pair_bits[i = ALFSIZ + 7] = 1; --i >= 0;)
        if ((pair_bits[i] = (pair_bits + 1)[i] << 1) == 0)
            pair_bits[i] = 1;
#endif

    // 3. Parse the command line: options begin with '-', otherwise the argument
    // is the input file (first) or output file (second).
    cpp.inc_file[cpp.inc_level = 0] = "";
    for (i = 1; i < argc; i++) {
        switch (argv[i][0]) {
        case '-':
            switch (argv[i][1]) {
            case 'P':
                cpp.opt_no_lines++;
            case 'E':
                continue;
            case 'R':
                ++cpp.opt_recurse;
                continue;
            case 'C':
                cpp.opt_keep_comments++;
                continue;
            case 'D':
                if (cpp.pre_defs_end > cpp.pre_defs + NPREDEF) {
                    pperror("too many -D options, ignoring %s", argv[i]);
                    continue;
                }
                // ignore plain "-D" (no argument)
                if (*(argv[i] + 2))
                    *cpp.pre_defs_end++ = argv[i] + 2;
                continue;
            case 'U':
                if (cpp.pre_undefs_end > cpp.pre_undefs + NPREDEF) {
                    pperror("too many -U options, ignoring %s", argv[i]);
                    continue;
                }
                *cpp.pre_undefs_end++ = argv[i] + 2;
                continue;
            case 'I':
                if (cpp.ndirs > 8)
                    pperror("excessive -I file (%s) ignored", argv[i]);
                else
                    cpp.search_dirs[cpp.ndirs++] = argv[i] + 2;
                continue;
            case '\0':
                continue;
            default:
                pperror("unknown flag %s", argv[i]);
                continue;
            }
        default:
            if (cpp.in_fd == STDIN) {
                cpp.in_fd = open(argv[i], READ);
                if (cpp.in_fd < 0) {
                    pperror("No source file %s", argv[i]);
                    exit(8);
                }
                cpp.inc_file[cpp.inc_level] = save_string(argv[i]);
                cpp.search_dirs[0] = cpp.inc_dir[cpp.inc_level] = dir_of(argv[i]);

                // too dangerous to have file name in same syntactic position
                // be input or output file depending on file redirections,
                // so force output to stdout, willy-nilly
                //      [i don't see what the problem is.  jfr]
                //
            } else if (cpp.out_file == stdout) {
                static char sobuf[BUFSIZ];
                cpp.out_file = fopen(argv[i], "w");
                if (!cpp.out_file) {
                    pperror("Can't create %s", argv[i]);
                    exit(8);
                }
                fclose(stdout);
                setbuffer(cpp.out_file, sobuf, sizeof(sobuf));
            } else
                pperror("extraneous name %s", argv[i]);
        }
    }
    if (isatty(cpp.in_fd)) {
        usage();
        exit(8);
    }

    cpp.inc_fd[cpp.inc_level] = cpp.in_fd;
    cpp.exit_code             = 0;

    // 4. Finish the include search path, then register the built-in directives
    // and predefined macros.
    // after user -I files here are the standard include libraries
    cpp.search_dirs[cpp.ndirs++] = "/usr/include";
    cpp.search_dirs[cpp.ndirs++] = 0;
    cpp.sym_define               = install_directive("define");
    cpp.sym_undef                = install_directive("undef");
    cpp.sym_include              = install_directive("include");
    cpp.sym_else                 = install_directive("else");
    cpp.sym_endif                = install_directive("endif");
    cpp.sym_ifdef                = install_directive("ifdef");
    cpp.sym_ifndef               = install_directive("ifndef");
    cpp.sym_if                   = install_directive("if");
    cpp.sym_line                 = install_directive("line");
    for (i = sizeof(cpp.macro_bits) / sizeof(cpp.macro_bits[0]); --i >= 0;)
        cpp.macro_bits[i] = 0;
#if unix
    cpp.sym_os = define_symbol("unix");
#endif
#if gcos
    cpp.sym_os = define_symbol("gcos");
#endif
#if ibm
    cpp.sym_os = define_symbol("ibm");
#endif
#if pdp11
    cpp.sym_arch = define_symbol("pdp11");
#endif
#if vax
    cpp.sym_arch = define_symbol("vax");
#endif
#if interdata
    cpp.sym_arch = define_symbol("interdata");
#endif
#if tss
    cpp.sym_arch = define_symbol("tss");
#endif
#if os
    cpp.sym_arch = define_symbol("os");
#endif
#if mert
    cpp.sym_arch = define_symbol("mert");
#endif
    cpp.sym_line_macro = define_symbol("__LINE__");
    cpp.sym_file_macro = define_symbol("__FILE__");

    tf                          = cpp.inc_file[cpp.inc_level];
    cpp.inc_file[cpp.inc_level] = "command line";
    cpp.line_no[cpp.inc_level]  = 1;
    cp2                         = cpp.pre_defs;
    while (cp2 < cpp.pre_defs_end)
        define_symbol(*cp2++);
    cp2 = cpp.pre_undefs;
    while (cp2 < cpp.pre_undefs_end) {
        if ((p = find_char(*cp2, '=')))
            *p++ = '\0';
        lookup(*cp2++, DROP);
    }
    cpp.inc_file[cpp.inc_level] = tf;
    cpp.buf_start               = cpp.arena + 8;
    cpp.buf_mid                 = cpp.buf_start + BUFSIZ;
    cpp.buf_end                 = cpp.buf_mid + BUFSIZ;

    // 5. Point the buffer cursors at the (empty) buffer and run the main loop.
    // The first refill inside process_directives reads the actual input.
    cpp.true_level  = 0;
    cpp.false_level = 0;
    cpp.line_no[0]  = 1;
    emit_line_marker();
    cpp.out_ptr = cpp.tok_ptr = cpp.buf_end;
    process_directives(cpp.buf_end);
    return (cpp.exit_code);
}
