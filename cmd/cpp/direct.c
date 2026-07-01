//
// C preprocessor: directive dispatch, #include, and -D/-U/builtin setup.
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

static char *do_include(char *p)
{
    int filok, inctype;
    char *cp;
    char **dirp, *nfil;
    char filname[BUFSIZ];

    p  = skip_blanks(p);
    cp = filname;
    if (*cpp.tok_ptr++ == '<') { // special <> syntax
        inctype = 1;
        for (;;) {
            cpp.out_ptr = cpp.tok_ptr = p;
            p                         = scan_token(p);
            if (*cpp.tok_ptr == '\n') {
                --p;
                *cp = '\0';
                break;
            }
            if (*cpp.tok_ptr == '>') {
                *cp = '\0';
                break;
            }
            while (cpp.tok_ptr < p)
                *cp++ = *cpp.tok_ptr++;
        }
    } else if (cpp.tok_ptr[-1] == '"') { // regular "" syntax
        inctype = 0;
        while (cpp.tok_ptr < p)
            *cp++ = *cpp.tok_ptr++;
        if (*--cp == '"')
            *cp = '\0';
    } else {
        pperror("bad include syntax", 0);
        inctype = 2;
    }
    // flush current file to \n , then write \n
    ++cpp.false_level;
    do {
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = scan_token(p);
    } while (*cpp.tok_ptr != '\n');
    --cpp.false_level;
    cpp.tok_ptr = p;
    flush_output();
    if (inctype == 2)
        return (p);
    // look for included file
    if (cpp.inc_level + 1 >= MAXINC) {
        pperror("Unreasonable include nesting", 0);
        return (p);
    }
    if ((nfil = cpp.side_ptr) > cpp.side_buf + SBSIZE - BUFSIZ) {
        pperror("no space");
        exit(cpp.exit_code);
    }
    filok = 0;
    for (dirp = cpp.search_dirs + inctype; *dirp; ++dirp) {
        if (filname[0] == '/' || **dirp == '\0')
            strcpy(nfil, filname);
        else {
            strcpy(nfil, *dirp);
            strcat(nfil, "/");
            strcat(nfil, filname);
        }
        if (0 < (cpp.inc_fd[cpp.inc_level + 1] = open(nfil, READ))) {
            filok     = 1;
            cpp.in_fd = cpp.inc_fd[++cpp.inc_level];
            break;
        }
    }
    if (filok == 0)
        pperror("Can't find include file %s", filname);
    else {
        cpp.line_no[cpp.inc_level]  = 1;
        cpp.inc_file[cpp.inc_level] = cp = nfil;
        while (*cp++)
            ;
        cpp.side_ptr               = cp;
        cpp.inc_dir[cpp.inc_level] = cpp.search_dirs[0] = dir_of(save_string(nfil));
        emit_line_marker();
        // save current contents of buffer
        while (!at_buf_end(p))
            p = spill_buffer(p);
        cpp.inc_push_top[cpp.inc_level] = cpp.push_top;
    }
    return (p);
}

struct symtab *define_symbol(const char *s)
{
    char buf[BUFSIZ];
    char *p;

    // make definition look exactly like end of #define line
    // copy to avoid running off end of world when param list is at end
    p = buf;
    while ((*p++ = *s++))
        ;
    p = buf;
    while (isid(*p++))
        ; // skip first identifier
    if (*--p == '=') {
        *p++ = ' ';
        while (*p++)
            ;
    } else {
        s = " 1";
        while ((*p++ = *s++))
            ;
    }
    cpp.buf_end = p;
    *--p        = '\n';
    set_slow_scan();
    do_define(buf);
    return (cpp.last_sym);
}

// kluge
struct symtab *install_directive(const char *s)
{
    struct symtab *sp;

    cpp.hash_seed   = SALT;
    *cpp.side_ptr++ = SALT;
    sp              = define_symbol(s);
    --sp->name;
    cpp.hash_seed = 0;
    return (sp);
}

//
// find and handle preprocessor control lines
//
char *process_directives(char *p)
{
    const struct symtab *np;
    for (;;) {
        set_fast_scan();
        p = scan_token(p);
        if (*cpp.tok_ptr == '\n')
            ++cpp.tok_ptr;
        flush_output();
        set_slow_scan();
        p              = skip_blanks(p);
        *--cpp.tok_ptr = SALT;
        cpp.out_ptr    = cpp.tok_ptr;
        ++cpp.false_level;
        np = lookup_token(cpp.tok_ptr, p, 0);
        --cpp.false_level;
        if (np == cpp.sym_define) { // define
            if (cpp.false_level == 0) {
                p = do_define(p);
                continue;
            }
        } else if (np == cpp.sym_include) { // include
            if (cpp.false_level == 0) {
                p = do_include(p);
                continue;
            }
        } else if (np == cpp.sym_ifndef) { // ifndef
            ++cpp.false_level;
            p  = skip_blanks(p);
            np = lookup_token(cpp.tok_ptr, p, 0);
            --cpp.false_level;
            if (cpp.false_level == 0 && np->value == 0)
                ++cpp.true_level;
            else
                ++cpp.false_level;
        } else if (np == cpp.sym_ifdef) { // ifdef
            ++cpp.false_level;
            p  = skip_blanks(p);
            np = lookup_token(cpp.tok_ptr, p, 0);
            --cpp.false_level;
            if (cpp.false_level == 0 && np->value != 0)
                ++cpp.true_level;
            else
                ++cpp.false_level;
        } else if (np == cpp.sym_endif) { // endif
            if (cpp.false_level) {
                if (--cpp.false_level == 0)
                    emit_line_marker();
            } else if (cpp.true_level)
                --cpp.true_level;
            else
                pperror("If-less endif", 0);
        } else if (np == cpp.sym_else) { // else
            if (cpp.false_level) {
                if (--cpp.false_level != 0)
                    ++cpp.false_level;
                else {
                    ++cpp.true_level;
                    emit_line_marker();
                }
            } else if (cpp.true_level) {
                ++cpp.false_level;
                --cpp.true_level;
            } else
                pperror("If-less else", 0);
        } else if (np == cpp.sym_undef) { // undefine
            if (cpp.false_level == 0) {
                ++cpp.false_level;
                p = skip_blanks(p);
                lookup_token(cpp.tok_ptr, p, DROP);
                --cpp.false_level;
            }
        } else if (np == cpp.sym_if) { // if
#if tgp
            pperror(" IF not implemented, true assumed", 0);
            if (cpp.false_level == 0)
                ++cpp.true_level;
            else
                ++cpp.false_level;
#else
            cpp.scan_ptr = p;
            if (cpp.false_level == 0 && eval_if())
                ++cpp.true_level;
            else
                ++cpp.false_level;
            p = cpp.scan_ptr;
#endif
        } else if (np == cpp.sym_line) { // line
            if (cpp.false_level == 0 && cpp.opt_no_lines == 0) {
                cpp.out_ptr = cpp.tok_ptr = p;
                *--cpp.out_ptr            = '#';
                while (*cpp.tok_ptr != '\n')
                    p = scan_token(p);
                continue;
            }
        } else if (*++cpp.tok_ptr == '\n')
            cpp.out_ptr = cpp.tok_ptr; // allows blank line after #
        else
            pperror("undefined control", 0);
        // flush to lf
        ++cpp.false_level;
        while (*cpp.tok_ptr != '\n') {
            cpp.out_ptr = cpp.tok_ptr = p;
            p                         = scan_token(p);
        }
        --cpp.false_level;
    }
}
