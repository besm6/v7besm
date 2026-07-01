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

//
// a superimposed code is used to reduce the number of calls to the
// symbol table lookup routine.  (if the kth character of an identifier
// is 'a' and there are no macro names whose kth character is 'a'
// then the identifier cannot be a macro name, hence there is no need
// to look in the symbol table.)  'scw1' enables the test based on
// single characters and their position in the identifier.  'scw2'
// enables the test based on adjacent pairs of characters and their
// position in the identifier.  scw1 typically costs 1 indexed fetch,
// an AND, and a jump per character of identifier, until the identifier
// is known as a non-macro name or until the end of the identifier.
// scw1 is inexpensive.  scw2 typically costs 4 indexed fetches,
// an add, an AND, and a jump per character of identifier, but it is also
// slightly more effective at reducing symbol table searches.
// scw2 usually costs too much because the symbol table search is
// usually short; but if symbol table search should become expensive,
// the code is here.
// using both scw1 and scw2 is of dubious value.
//
#define scw1 1
#define scw2 0

#if scw2
char pair_row[ALFSIZ], pair_col[ALFSIZ], pair_bits[ALFSIZ + 8];
#endif

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

#define in_slow_scan  (cpp.scan_tab == cpp.slow_tab)
#define is_special(a) (cpp.scan_tab[(unsigned char)a] & SB)

#define at_buf_end(a)   ((a) >= cpp.buf_end)
#define at_buf_start(a) (cpp.buf_start >= (a))

#define DROP   0xFE // special character not legal ASCII or EBCDIC
#define WARN   DROP
#define SAME   0
#define MAXFRM 31 // max number of formals/actuals to a macro

static char warn_mark = WARN;

#if tgp
int tgp_scanning; // flag for dump();
#endif

//
// The one and only instance of the preprocessor's mutable state, declared in
// defs.h.  Bundles what used to be ~40 file-scope globals.
//
struct cppstate cpp;

struct symtab *lookup_token(char *p1, char *p2, int enterf);
char *dir_of(char *s);
STATIC char *save_string(const char *s);
char *expand_macro(char *p, struct symtab *sp);

void emit_line_marker()
{
    if (cpp.opt_no_lines == 0)
        fprintf(cpp.out_file, "# %d \"%s\"\n", cpp.line_no[cpp.inc_level],
                cpp.inc_file[cpp.inc_level]);
}

// data structure guide
//
// most of the scanning takes place in the buffer:
//
//  (low address)                                             (high address)
//  buf_start                     buf_mid                             buf_end
//  |      <-- BUFSIZ chars -->      |         <-- BUFSIZ chars -->        |
//  _______________________________________________________________________
// |_______________________________________________________________________|
//          |               |               |
//          |<-- waiting -->|               |<-- waiting -->
//          |    to be      |<-- current -->|    to be
//          |    written    |    token      |    scanned
//          |               |               |
//         out_ptr         tok_ptr         p
//
//  *out_ptr   first char not yet written to output file
//  *tok_ptr   first char of current token
//  *p         first char not yet scanned
//
// macro expansion: write from *out_ptr to *tok_ptr (chars waiting to be written),
// ignore from *tok_ptr to *p (chars of the macro call), place generated
// characters in front of *p (in reverse order), update pointers,
// resume scanning.
//
// symbol table pointers point to just beyond the end of macro definitions;
// the first preceding character is the number of formal parameters.
// the appearance of a formal in the body of a definition is marked by
// 2 chars: the char WARN, and a char containing the parameter number.
// the first char of a definition is preceded by a zero character.
//
// when macro expansion attempts to back up over the beginning of the
// buffer, some characters preceding *buf_end are saved in a side buffer,
// the address of the side buffer is put on 'push_stack', and the rest
// of the main buffer is moved to the right.  the end of the saved buffer
// is kept in 'push_end' since there may be nulls in the saved buffer.
//
// similar action is taken when an 'include' statement is processed,
// except that the main buffer must be completely emptied.  the array
// element 'inc_push_top[inc_level]' records the last side buffer saved when
// file 'inc_level' was included.  these buffers remain dormant while
// the file is being read, and are reactivated at end-of-file.
//
// push_stack[0 : push_top] holds the addresses of all pending side buffers.
// push_stack[inc_push_top[inc_level]+1 : push_top-1] holds the addresses of the
// side buffers which are "live"; the buffers push_stack[0 : inc_push_top[inc_level]]
// are dormant, waiting for end-of-file on the current file.
//
// space for side buffers is obtained from 'side_ptr' and is never returned.
// free_stack[0:free_top-1] holds addresses of side buffers which
// are available for use.
//
void flush_output()
{
    // write part of buffer which lies between  out_ptr  and  tok_ptr .
    // this should be a direct call to 'write', but the system slows to a crawl
    // if it has to do an unaligned copy.  thus we buffer.  this silly loop
    // is 15% of the total time, thus even the 'putc' macro is too slow.
    //
    char *p1;
    FILE *f;
    if ((p1 = cpp.out_ptr) == cpp.tok_ptr || cpp.false_level != 0)
        return;
#if tgp
#define MAXOUT 80
    if (!tgp_scanning) { // scan again to insure <= MAXOUT chars between linefeeds
        char c, *pblank, *p2;
        char savc, stopc, brk;
        tgp_scanning = 1;
        brk = stopc = pblank = 0;
        p2                   = cpp.tok_ptr;
        savc                 = *p2;
        *p2                  = '\0';
        while (c = *p1++) {
            if (c == '\\')
                c = *p1++;
            if (stopc == c)
                stopc = 0;
            else if (c == '"' || c == '\'')
                stopc = c;
            if (p1 - cpp.out_ptr > MAXOUT && pblank != 0) {
                *pblank++   = '\n';
                cpp.tok_ptr = pblank;
                flush_output();
                brk    = 1;
                pblank = 0;
            }
            if (c == ' ' && stopc == 0)
                pblank = p1 - 1;
        }
        if (brk)
            emit_line_marker();
        *p2          = savc;
        cpp.tok_ptr  = p2;
        p1           = cpp.out_ptr;
        tgp_scanning = 0;
    }
#endif
    f = cpp.out_file;
    while (p1 < cpp.tok_ptr)
        putc(*p1++, f);
    cpp.out_ptr = p1;
}

char *refill_buffer(char *p)
{
    // flush buffer.  save chars from tok_ptr to p.  read into arena at buf_mid,
    // contiguous with p.  update pointers, return new p.
    //
    char *np;
    const char *op;

    flush_output();
    np = cpp.buf_mid - (p - cpp.tok_ptr);
    op = cpp.tok_ptr;
    if (at_buf_start(np + 1)) {
        pperror("token too long");
        np = cpp.buf_start;
        p  = cpp.tok_ptr + BUFSIZ;
    }
    cpp.recur_bound_adj += np - cpp.tok_ptr;
    cpp.out_ptr = cpp.tok_ptr = np;
    while (op < p)
        *np++ = *op++;
    p = np;
    for (;;) {
        if (cpp.push_top >
            cpp.inc_push_top[cpp.inc_level]) { // retrieve hunk of pushed-back macro text
            op = cpp.push_stack[--cpp.push_top];
            np = cpp.buf_mid;
            do {
                while ((*np++ = *op++))
                    ;
            } while (op < cpp.push_end[cpp.push_top]);
            cpp.buf_end = np - 1;
            // make buffer space avail for 'include' processing
            if (cpp.free_top < MAXFRE)
                cpp.free_stack[cpp.free_top++] = cpp.push_stack[cpp.push_top];
            return (p);
        } else { // get more text from file(s)
            cpp.recur_depth = 0;
            int ninbuf      = read(cpp.in_fd, cpp.buf_mid, BUFSIZ);
            if (0 < ninbuf) {
                cpp.buf_end  = cpp.buf_mid + ninbuf;
                *cpp.buf_end = '\0';
                return (p);
            }
            // end of #include file
            if (cpp.inc_level == 0) { // end of input
                if (cpp.paren_level != 0) {
                    int n = cpp.paren_level, tlin = cpp.line_no[cpp.inc_level];
                    char *tfil                  = cpp.inc_file[cpp.inc_level];
                    cpp.line_no[cpp.inc_level]  = cpp.call_line;
                    cpp.inc_file[cpp.inc_level] = cpp.call_file;
                    pperror("%s: unterminated macro call", cpp.macro_name);
                    cpp.line_no[cpp.inc_level]  = tlin;
                    cpp.inc_file[cpp.inc_level] = tfil;
                    np                          = p;
                    *np++                       = '\n'; // shut off unterminated quoted string
                    while (--n >= 0)
                        *np++ = ')'; // supply missing parens
                    cpp.buf_end = np;
                    *np         = '\0';
                    if (cpp.paren_level < 0)
                        cpp.paren_level = 0;
                    return (p);
                }
                cpp.tok_ptr = p;
                flush_output();
                exit(cpp.exit_code);
            }
            close(cpp.in_fd);
            cpp.in_fd          = cpp.inc_fd[--cpp.inc_level];
            cpp.search_dirs[0] = cpp.inc_dir[cpp.inc_level];
            emit_line_marker();
        }
    }
}

#define BEG 0
#define LF  1

char *scan_token(char *p)
{
    int c, i;
    char quoc;
    static int state = BEG;

    if (state != BEG)
        goto prevlf;
    for (;;) {
    again:
        while (!is_special(*p++))
            ;
        switch (*(cpp.tok_ptr = p - 1)) {
        case 0: {
            if (at_buf_end(--p)) {
                p = refill_buffer(p);
                goto again;
            } else
                ++p; // ignore null byte
        } break;
        case '|':
        case '&':
            for (;;) { // sloscan only
                if (*p++ == *cpp.tok_ptr)
                    break;
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        case '=':
        case '!':
            for (;;) { // sloscan only
                if (*p++ == '=')
                    break;
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        case '<':
        case '>':
            for (;;) { // sloscan only
                if (*p++ == '=' || p[-2] == p[-1])
                    break;
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        case '\\':
            for (;;) {
                if (*p++ == '\n') {
                    ++cpp.line_no[cpp.inc_level];
                    break;
                }
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else {
                    ++p;
                    break;
                }
            }
            break;
        case '/':
            for (;;) {
                if (*p++ == '*') { // comment
                    if (!cpp.opt_keep_comments) {
                        cpp.tok_ptr = p - 2;
                        flush_output();
                        ++cpp.false_level;
                    }
                    for (;;) {
                        while (!iscom(*p++))
                            ;
                        if (p[-1] == '*')
                            for (;;) {
                                if (*p++ == '/')
                                    goto endcom;
                                if (at_buf_end(--p)) {
                                    if (!cpp.opt_keep_comments) {
                                        cpp.tok_ptr = p;
                                        p           = refill_buffer(p);
                                    } else if ((p - cpp.tok_ptr) >= BUFSIZ) { // split long comment
                                        cpp.tok_ptr = p;
                                        p           = refill_buffer(p); // last char written is '*'
                                        putc('/', cpp.out_file);        // terminate first part
                                        // and fake start of 2nd
                                        cpp.out_ptr = cpp.tok_ptr = p -= 3;
                                        *p++                      = '/';
                                        *p++                      = '*';
                                        *p++                      = '*';
                                    } else
                                        p = refill_buffer(p);
                                } else
                                    break;
                            }
                        else if (p[-1] == '\n') {
                            ++cpp.line_no[cpp.inc_level];
                            if (!cpp.opt_keep_comments)
                                putc('\n', cpp.out_file);
                        } else if (at_buf_end(--p)) {
                            if (!cpp.opt_keep_comments) {
                                cpp.tok_ptr = p;
                                p           = refill_buffer(p);
                            } else if ((p - cpp.tok_ptr) >= BUFSIZ) { // split long comment
                                cpp.tok_ptr = p;
                                p           = refill_buffer(p);
                                putc('*', cpp.out_file);
                                putc('/', cpp.out_file);
                                cpp.out_ptr = cpp.tok_ptr = p -= 2;
                                *p++                      = '/';
                                *p++                      = '*';
                            } else
                                p = refill_buffer(p);
                        } else
                            ++p; // ignore null byte
                    }
                endcom:
                    if (!cpp.opt_keep_comments) {
                        cpp.out_ptr = cpp.tok_ptr = p;
                        --cpp.false_level;
                        goto again;
                    }
                    break;
                }
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        case '"':
        case '\'': {
            quoc = p[-1];
            for (;;) {
                while (!isquo(*p++))
                    ;
                if (p[-1] == quoc)
                    break;
                if (p[-1] == '\n') {
                    --p;
                    break;
                } // bare \n terminates quotation
                if (p[-1] == '\\')
                    for (;;) {
                        if (*p++ == '\n') {
                            ++cpp.line_no[cpp.inc_level];
                            break;
                        } // escaped \n ignored
                        if (at_buf_end(--p))
                            p = refill_buffer(p);
                        else {
                            ++p;
                            break;
                        }
                    }
                else if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    ++p; // it was a different quote character
            }
        } break;
        case '\n': {
            ++cpp.line_no[cpp.inc_level];
            if (in_slow_scan) {
                state = LF;
                return (p);
            }
        prevlf:
            state = BEG;
            for (;;) {
                if (*p++ == '#')
                    return (p);
                if (at_buf_end(cpp.tok_ptr = --p))
                    p = refill_buffer(p);
                else
                    goto again;
            }
        } break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            for (;;) {
                while (isnum(*p++))
                    ;
                if (at_buf_end(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case '_':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':
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

            if (cpp.false_level)
                goto nomac;
            for (;;) {
                c = p[-1];
                tmac1(c, b0);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b1);
                tmac2(c, i, 0);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b2);
                tmac2(i, c, 1);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b3);
                tmac2(c, i, 2);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b4);
                tmac2(i, c, 3);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b5);
                tmac2(c, i, 4);
                c = *p++;
                if (!isid(c))
                    goto endid;
                tmac1(c, b6);
                tmac2(i, c, 5);
                i = *p++;
                if (!isid(i))
                    goto endid;
                tmac1(i, b7);
                tmac2(c, i, 6);
                tmac2(i, 0, 7);
                while (isid(*p++))
                    ;
                if (at_buf_end(--p)) {
                    refill_buffer(p);
                    p = cpp.tok_ptr + 1;
                    continue;
                }
                goto lokid;
            endid:
                if (at_buf_end(--p)) {
                    refill_buffer(p);
                    p = cpp.tok_ptr + 1;
                    continue;
                }
                tmac2(p[-1], 0, -1 + (p - cpp.tok_ptr));
            lokid:
                lookup_token(cpp.tok_ptr, p, 0);
                if (cpp.scan_ptr) {
                    p = cpp.scan_ptr;
                    goto again;
                } else
                    break;
            nomac:
                while (isid(*p++))
                    ;
                if (at_buf_end(--p)) {
                    p = refill_buffer(p);
                    goto nomac;
                } else
                    break;
            }
            break;
        } // end of switch

        if (in_slow_scan)
            return (p);
    } // end of infinite loop
}

//
// get next non-blank token
//
char *skip_blanks(char *p)
{
    do {
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = scan_token(p);
    } while (cpp.char_class[(unsigned char)*cpp.tok_ptr] == BLANK);
    return (p);
}

//
// take <= BUFSIZ chars from right end of buffer and put them on push_stack .
// slide rest of buffer to the right, update pointers, return new p.
//
char *spill_buffer(char *p)
{
    char *np;
    const char *op;
    int d;

    if (cpp.push_top >= MAXFRE) {
        pperror("%s: too much pushback", cpp.macro_name);
        p = cpp.tok_ptr = cpp.buf_end;
        flush_output(); // begin flushing pushback
        while (cpp.push_top > cpp.inc_push_top[cpp.inc_level]) {
            p = refill_buffer(p);
            p = cpp.tok_ptr = cpp.buf_end;
            flush_output();
        }
    }
    if (cpp.free_top > 0)
        np = cpp.free_stack[--cpp.free_top];
    else {
        np = cpp.side_ptr;
        cpp.side_ptr += BUFSIZ;
        if (cpp.side_ptr >= cpp.side_buf + SBSIZE) {
            pperror("no space");
            exit(cpp.exit_code);
        }
        *cpp.side_ptr++ = '\0';
    }
    cpp.push_stack[cpp.push_top] = np;
    op                           = cpp.buf_end - BUFSIZ;
    if (op < p)
        op = p;
    for (;;) {
        while ((*np++ = *op++))
            ;
        if (at_buf_end(op))
            break;
    } // out with old
    cpp.push_end[cpp.push_top++] = np; // mark end of saved text
    np                           = cpp.buf_mid + BUFSIZ;
    op                           = cpp.buf_end - BUFSIZ;
    cpp.buf_end                  = np;
    if (op < p)
        op = p;
    while (cpp.out_ptr < op)
        *--np = *--op; // slide over new
    if (at_buf_start(np))
        pperror("token too long");
    d = np - cpp.out_ptr;
    cpp.out_ptr += d;
    cpp.tok_ptr += d;
    cpp.recur_bound_adj += d;
    return (p + d);
}

char *do_include(char *p)
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

int formal_matches(const char *a, const char *p1, char *p2)
{
    char c;
    int flag;
    c    = *p2;
    *p2  = '\0';
    flag = strcmp(a, p1);
    *p2  = c;
    return (flag == SAME);
}

//
// process '#define'
//
char *do_define(char *p)
{
    char *pin, *psav, *cf;
    char **pf, **qf;
    int b, c, params;
    struct symtab *np;
    char *oldval, *oldsavch;
    char *formal[MAXFRM]; // formal[n] is name of nth formal
    char formtxt[BUFSIZ]; // space for formal names

    if (cpp.side_ptr > cpp.side_buf + SBSIZE - BUFSIZ) {
        pperror("too much defining");
        return (p);
    }
    oldsavch = cpp.side_ptr; // to reclaim space if redefinition
    ++cpp.false_level;       // prevent macro expansion during 'define'
    p   = skip_blanks(p);
    pin = cpp.tok_ptr;
    if (cpp.char_class[(unsigned char)*pin] != IDENT) {
        ppwarn("illegal macro name");
        while (*cpp.tok_ptr != '\n')
            p = skip_blanks(p);
        return (p);
    }
    np = lookup_token(pin, p, 1);
    if ((oldval = np->value))
        cpp.side_ptr = oldsavch; // was previously defined
    b  = 1;
    cf = pin;
    while (cf < p) { // update macro_bits
        c = *cf++;
        xmac1(c, b, |=);
        b = (b + b) & 0xFF;
        if (cf != p)
            xmac2(c, *cf, -1 + (cf - pin), |=);
        else
            xmac2(c, 0, -1 + (cf - pin), |=);
    }
    params      = 0;
    cpp.out_ptr = cpp.tok_ptr = p;
    p                         = scan_token(p);
    pin                       = cpp.tok_ptr;
    if (*pin == '(') { // with parameters; identify the formals
        cf = formtxt;
        pf = formal;
        for (;;) {
            p   = skip_blanks(p);
            pin = cpp.tok_ptr;
            if (*pin == '\n') {
                --cpp.line_no[cpp.inc_level];
                --p;
                pperror("%s: missing )", np->name);
                break;
            }
            if (*pin == ')')
                break;
            if (*pin == ',')
                continue;
            if (cpp.char_class[(unsigned char)*pin] != IDENT) {
                c  = *p;
                *p = '\0';
                pperror("bad formal: %s", pin);
                *p = c;
            } else if (pf >= &formal[MAXFRM]) {
                c  = *p;
                *p = '\0';
                pperror("too many formals: %s", pin);
                *p = c;
            } else {
                *pf++ = cf;
                while (pin < p)
                    *cf++ = *pin++;
                *cf++ = '\0';
                ++params;
            }
        }
        if (params == 0)
            --params; // #define foo() ...
    } else if (*pin == '\n') {
        --cpp.line_no[cpp.inc_level];
        --p;
    }

    // remember beginning of macro body, so that we can
    // warn if a redefinition is different from old value.
    //
    oldsavch = psav = cpp.side_ptr;
    for (;;) { // accumulate definition until linefeed
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = scan_token(p);
        pin                       = cpp.tok_ptr;
        if (*pin == '\\' && pin[1] == '\n')
            continue; // ignore escaped lf
        if (*pin == '\n')
            break;
        if (params) { // mark the appearance of formals in the definiton
            if (cpp.char_class[(unsigned char)*pin] == IDENT) {
                for (qf = pf; --qf >= formal;) {
                    if (formal_matches(*qf, pin, p)) {
                        *psav++ = qf - formal + 1;
                        *psav++ = WARN;
                        pin     = p;
                        break;
                    }
                }
            } else if (*pin == '"' || *pin == '\'') { // inside quotation marks, too
                char quoc = *pin;
                for (*psav++ = *pin++; pin < p && *pin != quoc;) {
                    while (pin < p && !isid(*pin))
                        *psav++ = *pin++;
                    cf = pin;
                    while (cf < p && isid(*cf))
                        ++cf;
                    for (qf = pf; --qf >= formal;) {
                        if (formal_matches(*qf, pin, cf)) {
                            *psav++ = qf - formal + 1;
                            *psav++ = WARN;
                            pin     = cf;
                            break;
                        }
                    }
                    while (pin < cf)
                        *psav++ = *pin++;
                }
            }
        }
        while (pin < p)
            *psav++ = *pin++;
    }
    *psav++ = params;
    *psav++ = '\0';
    if ((cf = oldval) != NULL) { // redefinition
        --cf;                    // skip no. of params, which may be zero
        while (*--cf)
            ;                              // go back to the beginning
        if (0 != strcmp(++cf, oldsavch)) { // redefinition different from old
            --cpp.line_no[cpp.inc_level];
            ppwarn("%s redefined", np->name);
            ++cpp.line_no[cpp.inc_level];
            np->value = psav - 1;
        } else
            psav = oldsavch; // identical redef.; reclaim space
    } else
        np->value = psav - 1;
    --cpp.false_level;
    cpp.tok_ptr  = pin;
    cpp.side_ptr = psav;
    return (p);
}

#define set_fast_scan() cpp.scan_tab = cpp.fast_tab
#define set_slow_scan() cpp.scan_tab = cpp.slow_tab

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

void vreport(const char *s, va_list ap)
{
    if (cpp.inc_file[cpp.inc_level][0]) {
        fprintf(stderr, "%s: ", cpp.inc_file[cpp.inc_level]);
    }
    fprintf(stderr, "%d: ", cpp.line_no[cpp.inc_level]);
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
    ++cpp.exit_code;
}

void pperror(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

void parse_error(const char *s, ...)
{
    va_list ap;

    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
}

void ppwarn(const char *s, ...)
{
    int fail = cpp.exit_code;
    va_list ap;

    cpp.exit_code = -1;
    va_start(ap, s);
    vreport(s, ap);
    va_end(ap);
    cpp.exit_code = fail;
}

struct symtab *lookup(char *namep, int enterf)
{
    const char *np, *snp;
    int c, i;
    int around;
    struct symtab *sp;

    // namep had better not be too long (currently, <=8 chars)
    np     = namep;
    around = 0;
    i      = cpp.hash_seed;
    while ((c = *np++))
        i += i + c;
    c = i; // c=i for usage on pdp11
    c %= symsiz;
    if (c < 0)
        c += symsiz;
    sp = &cpp.symbols[c];
    while ((snp = sp->name)) {
        np = namep;
        while (*snp++ == *np) {
            if (*np++ == '\0') {
                if (enterf == DROP) {
                    sp->name[0] = DROP;
                    sp->value   = 0;
                }
                return (cpp.last_sym = sp);
            }
        }
        if (--sp < &cpp.symbols[0]) {
            if (around) {
                pperror("too many defines", 0);
                exit(cpp.exit_code);
            } else {
                ++around;
                sp = &cpp.symbols[symsiz - 1];
            }
        }
    }
    if (enterf > 0)
        sp->name = namep;
    return (cpp.last_sym = sp);
}

struct symtab *lookup_token(char *p1, char *p2, int enterf)
{
    char *p3;
    char c2, c3;
    struct symtab *np;
    c2  = *p2;
    *p2 = '\0'; // mark end of token
    if ((p2 - p1) > 8)
        p3 = p1 + 8;
    else
        p3 = p2;
    c3  = *p3;
    *p3 = '\0'; // truncate to 8 chars or less
    if (enterf == 1)
        p1 = save_string(p1);
    np  = lookup(p1, enterf);
    *p3 = c3;
    *p2 = c2;
    if (np->value != 0 && cpp.false_level == 0)
        cpp.scan_ptr = expand_macro(p2, np);
    else
        cpp.scan_ptr = 0;
    return (np);
}

char *expand_macro(char *p, struct symtab *sp)
{
    char *ca, *vp;
    int params;
    char *actual[MAXFRM]; // actual[n] is text of nth actual
    char acttxt[BUFSIZ];  // space for actuals

    if (0 == (vp = sp->value))
        return (p);
    if ((p - cpp.recur_bound) <= cpp.recur_bound_adj) {
        if (++cpp.recur_depth > symsiz && !cpp.opt_recurse) {
            pperror("%s: macro recursion", sp->name);
            return (p);
        }
    } else
        cpp.recur_depth = 0; // level decreased
    cpp.recur_bound     = p;
    cpp.recur_bound_adj = 0; // new target for decrease in level
    cpp.macro_name      = sp->name;
    flush_output();
    if (sp == cpp.sym_line_macro) {
        vp    = acttxt;
        *vp++ = '\0';
        sprintf(vp, "%d", cpp.line_no[cpp.inc_level]);
        while (*vp++)
            ;
    } else if (sp == cpp.sym_file_macro) {
        vp    = acttxt;
        *vp++ = '\0';
        sprintf(vp, "\"%s\"", cpp.inc_file[cpp.inc_level]);
        while (*vp++)
            ;
    }
    if (0 != (params = *--vp & 0xFF)) { // definition calls for params
        char **pa;
        ca = acttxt;
        pa = actual;
        if (params == 0xFF)
            params = 1; // #define foo() ...
        set_slow_scan();
        ++cpp.false_level; // no expansion during search for actuals
        cpp.paren_level = -1;
        do
            p = skip_blanks(p);
        while (*cpp.tok_ptr == '\n'); // skip \n too
        if (*cpp.tok_ptr == '(') {
            cpp.call_line = cpp.line_no[cpp.inc_level];
            cpp.call_file = cpp.inc_file[cpp.inc_level];
            for (cpp.paren_level = 1; cpp.paren_level != 0;) {
                *ca++ = '\0';
                for (;;) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = scan_token(p);
                    if (*cpp.tok_ptr == '(')
                        ++cpp.paren_level;
                    if (*cpp.tok_ptr == ')' && --cpp.paren_level == 0) {
                        --params;
                        break;
                    }
                    if (cpp.paren_level == 1 && *cpp.tok_ptr == ',') {
                        --params;
                        break;
                    }
                    while (cpp.tok_ptr < p)
                        *ca++ = *cpp.tok_ptr++;
                    if (ca > &acttxt[BUFSIZ])
                        pperror("%s: actuals too long", sp->name);
                }
                if (pa >= &actual[MAXFRM])
                    ppwarn("%s: argument mismatch", sp->name);
                else
                    *pa++ = ca;
            }
        }
        if (params != 0)
            ppwarn("%s: argument mismatch", sp->name);
        while (--params >= 0)
            *pa++ = &""[1]; // null string for missing actuals
        --cpp.false_level;
        set_fast_scan();
    }
    for (;;) { // push definition onto front of input stack
        while (!iswarn(*--vp)) {
            if (at_buf_start(p)) {
                cpp.out_ptr = cpp.tok_ptr = p;
                p                         = spill_buffer(p);
            }
            *--p = *vp;
        }
        if (*vp == warn_mark) { // insert actual param
            ca = actual[*--vp - 1];
            while (*--ca) {
                if (at_buf_start(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *ca;
            }
        } else
            break;
    }
    cpp.out_ptr = cpp.tok_ptr = p;
    return (p);
}

char *dir_of(char *s)
{
    char *p = s;
    while (*p++)
        ;
    --p;
    while (p > s && *--p != '/')
        ;
    if (p == s)
        *p++ = '.';
    *p = '\0';
    return (s);
}

STATIC char *save_string(const char *s)
{
    char *old;

    old = cpp.side_ptr;
    while ((*cpp.side_ptr++ = *s++))
        ;
    return (old);
}

char *find_char(char *s, int c)
{
    while (*s)
        if (*s++ == c)
            return (--s);
    return (0);
}

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

int main(int argc, char *argv[])
{
    int i, c;
    char *p;
    char *tf, **cp2;

    // Fields that used to have static initializers referencing other fields;
    // set them up before anything runs (the instance is otherwise zeroed).
    cpp.side_ptr       = cpp.side_buf;
    cpp.pre_defs_end   = cpp.pre_defs;
    cpp.pre_undefs_end = cpp.pre_undefs;
    cpp.in_fd          = STDIN;
    cpp.ndirs          = 1;

    cpp.out_file = stdout;
    p            = "_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    i            = 0;
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

    cpp.true_level  = 0;
    cpp.false_level = 0;
    cpp.line_no[0]  = 1;
    emit_line_marker();
    cpp.out_ptr = cpp.tok_ptr = cpp.buf_end;
    process_directives(cpp.buf_end);
    return (cpp.exit_code);
}
