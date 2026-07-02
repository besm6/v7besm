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

//
// Handle a "#include" line.  Read the file name (either <name> or "name"),
// finish and flush the current line, then search for the file: <> names skip the
// current directory, "" names include it.  On success we open the file, push a
// new level on the include stack, save the current buffer contents aside (so we
// can resume the including file at end-of-file), and start reading the new file.
//
static char *do_include(char *p)
{
    int filok, inctype; // filok: file was opened; inctype: 0="" 1=<> 2=bad
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
    // Try each directory on the search path in turn until one opens.
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
        cpp.trig_nhold[cpp.inc_level] = 0; // fresh file: no trigraph '?' carried in
        cpp.inc_file[cpp.inc_level] = cp = nfil;
        while (*cp++)
            ;
        cpp.side_ptr               = cp;
        cpp.inc_dir[cpp.inc_level] = cpp.search_dirs[0] = dir_of(save_string(nfil));
        emit_line_marker();
        // save current contents of buffer
        while (!AT_BUF_END(p))
            p = spill_buffer(p);
        cpp.inc_push_top[cpp.inc_level] = cpp.push_top;
    }
    return (p);
}

//
// Apply a command-line "-D name" or "-D name=value" option by turning it into
// text that looks exactly like the tail of a #define line and feeding it to
// do_define().  "name" alone becomes "name 1"; "name=value" becomes
// "name value".  Returns the created symbol.
//
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
    while (ISID(*p++))
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
    SET_SLOW_SCAN();
    do_define(buf);
    return (cpp.last_sym);
}

//
// Register a built-in directive keyword such as "define" or "ifdef" in the
// symbol table.  The name is stored with a leading SALT ('#') byte and a special
// hash seed so that directives live in their own corner of the table and never
// collide with ordinary macro names.  Returns the entry (saved in cpp.sym_*).
//
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
// Enter a new #if/#ifdef/#ifndef group.  "was_live" is nonzero when we are not
// already inside a skipped region; "taken" is nonzero when this group's opening
// branch is taken.  Push the per-group "already decided" flag -- eligible (0)
// only when the group is live and its opening branch was not taken -- and bump
// the taken/skipped counters accordingly.
//
static void enter_if(int was_live, int taken)
{
    if (taken)
        ++cpp.true_level;
    else
        ++cpp.false_level;
    if (cpp.if_top >= MAXIF) {
        pperror("Too many nested #if", 0);
        return; // matching #endif still unwinds via the level counters
    }
    ++cpp.if_top;
    cpp.if_taken[cpp.if_top] = (was_live && !taken) ? 0 : 1;
}

//
// The directive loop -- the top level of the preprocessor.  It scans the input;
// scan_token hands back control at the '#' that starts each directive line.  We
// read the directive keyword, dispatch on which built-in it is (comparing the
// looked-up symbol against the cpp.sym_* handles), and act on it: #define,
// #include, the #if/#ifdef/#ifndef/#else/#endif conditional stack (tracked by
// true_level/false_level), #undef, and #line.  Text inside a false conditional
// branch is scanned but not emitted.  This function does not return.
//
char *process_directives(char *p)
{
    for (;;) {
        const struct symtab *np; // the directive keyword's symbol-table entry
        SET_FAST_SCAN();
        p = scan_token(p);
        if (*cpp.tok_ptr == '\n')
            ++cpp.tok_ptr;
        flush_output();
        SET_SLOW_SCAN();
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
            {
                int was_live = (cpp.false_level == 0);
                enter_if(was_live, was_live && np->value == 0);
            }
        } else if (np == cpp.sym_ifdef) { // ifdef
            ++cpp.false_level;
            p  = skip_blanks(p);
            np = lookup_token(cpp.tok_ptr, p, 0);
            --cpp.false_level;
            {
                int was_live = (cpp.false_level == 0);
                enter_if(was_live, was_live && np->value != 0);
            }
        } else if (np == cpp.sym_endif) { // endif
            if (cpp.if_top == 0)
                pperror("If-less endif", 0);
            else {
                if (cpp.false_level) {
                    if (--cpp.false_level == 0)
                        emit_line_marker();
                } else if (cpp.true_level)
                    --cpp.true_level;
                --cpp.if_top;
            }
        } else if (np == cpp.sym_elif) { // elif
            if (cpp.if_top == 0)
                pperror("If-less elif", 0);
            else if (cpp.if_taken[cpp.if_top]) {
                // A branch was already taken (or the whole group is nested in a
                // skipped region): this #elif must not be taken.
                if (cpp.false_level == 0) {
                    --cpp.true_level; // close the currently active branch
                    ++cpp.false_level;
                }
                // else already skipping -- stay skipping
            } else {
                // Still eligible: the group's own skip is exactly one false_level,
                // so drop it and evaluate the #elif condition.
                cpp.scan_ptr = p;
                --cpp.false_level;
                if (eval_if()) {
                    ++cpp.true_level;
                    cpp.if_taken[cpp.if_top] = 1;
                } else
                    ++cpp.false_level;
                p = cpp.scan_ptr;
            }
        } else if (np == cpp.sym_else) { // else
            if (cpp.if_top == 0)
                pperror("If-less else", 0);
            else if (cpp.if_taken[cpp.if_top]) {
                // Some branch already taken: skip the #else branch.
                if (cpp.false_level == 0) {
                    --cpp.true_level;
                    ++cpp.false_level;
                }
            } else {
                // No branch taken yet: take the #else branch.
                --cpp.false_level;
                ++cpp.true_level;
                cpp.if_taken[cpp.if_top] = 1;
                emit_line_marker();
            }
        } else if (np == cpp.sym_undef) { // undefine
            if (cpp.false_level == 0) {
                struct symtab *usp;
                char saved;
                int is_def;
                ++cpp.false_level;
                p   = skip_blanks(p);
                usp = lookup_token(cpp.tok_ptr, p, 0); // look only, do not DROP yet
                // "defined" is never a table entry, so match it by token text.
                saved  = *p;
                *p     = '\0';
                is_def = (strcmp(cpp.tok_ptr, "defined") == 0);
                *p     = saved;
                if (is_def) // §6.10.8.4
                    pperror("\"defined\" cannot be undefined");
                else if (usp->name && (unsigned char)usp->name[0] != DROP && usp->predefined)
                    pperror("predefined macro \"%s\" cannot be undefined", usp->name);
                else
                    lookup_token(cpp.tok_ptr, p, DROP);
                --cpp.false_level;
            }
        } else if (np == cpp.sym_if) { // if
            int was_live = (cpp.false_level == 0);
            int taken    = 0;
            if (was_live) {
                cpp.scan_ptr = p;
                taken        = eval_if();
                p            = cpp.scan_ptr;
            }
            enter_if(was_live, taken);
        } else if (np == cpp.sym_line) { // line (§6.10.4)
            if (cpp.false_level == 0) {
                char optext[BUFSIZ]; // raw operand text
                char expbuf[BUFSIZ]; // operands after macro expansion
                char fbuf[BUFSIZ];   // extracted file name
                char *cp, *e;

                // Collect the raw operand text up to end of line.  Reaching the
                // '\n' here counts the directive line (scan_token bumps line_no);
                // we set the presumed line number afterwards so it is not perturbed.
                p  = skip_blanks(p);
                cp = optext;
                while (*cpp.tok_ptr != '\n') {
                    while (cpp.tok_ptr < p && cp < optext + sizeof(optext) - 1)
                        *cp++ = *cpp.tok_ptr++;
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = scan_token(p);
                }
                *cp = '\0';
                // §6.10.4p5: the operands are macro-expanded before use.
                expand_text(optext, cp, expbuf, sizeof(expbuf));
                e = expbuf;
                while (*e == ' ' || *e == '\t')
                    ++e;
                if (*e < '0' || *e > '9')
                    pperror("illegal #line", 0); // §6.10.4: first operand must be a digit sequence
                else {
                    const char *fname = 0;
                    int num           = (int)strtol(e, &e, 10);
                    while (*e == ' ' || *e == '\t')
                        ++e;
                    if (*e == '"') { // optional "file-name"
                        char *fp = fbuf;
                        ++e;
                        while (*e && *e != '"' && fp < fbuf + sizeof(fbuf) - 1)
                            *fp++ = *e++;
                        *fp   = '\0';
                        fname = fbuf;
                    }
                    // Update the presumed location that __LINE__/__FILE__ report.
                    cpp.line_no[cpp.inc_level] = num;
                    if (fname)
                        cpp.inc_file[cpp.inc_level] = save_string(fname);
                    if (cpp.opt_no_lines == 0)
                        emit_line_marker();
                }
                continue;
            }
        } else if (np == cpp.sym_error) { // error (§6.10.5)
            if (cpp.false_level == 0) {
                char msg[BUFSIZ]; // the diagnostic tokens, used literally
                char *cp = msg;

                p = skip_blanks(p);
                while (*cpp.tok_ptr != '\n') {
                    while (cpp.tok_ptr < p && cp < msg + sizeof(msg) - 1)
                        *cp++ = *cpp.tok_ptr++;
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = scan_token(p);
                }
                *cp = '\0';
                pperror("#error %s", msg); // msg is a %s arg, so a '%' in it is harmless
                continue;
            }
            // Inside a skipped conditional group: inert; fall through to the
            // shared drain-to-'\n' loop below, like a skipped #define/#undef.
        } else if (np == cpp.sym_pragma) { // pragma (§6.10.6): accept, ignore unknown
            // No handling: fall through to the shared drain, which swallows the
            // line without error (a conformant tool ignores unknown pragmas).
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
