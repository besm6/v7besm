//
// C preprocessor: the lexical scanner (token recognition).
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

#if scw2
char pair_row[ALFSIZ], pair_col[ALFSIZ], pair_bits[ALFSIZ + 8];
#endif

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
