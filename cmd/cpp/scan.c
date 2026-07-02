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

#define BEG 0 // scanner start state
#define LF  1 // scanner just returned at a newline (used to resume mid-line)

//
// Scan the next token starting at p and return the pointer just past it; the
// token itself is left as the bytes from cpp.tok_ptr up to the returned pointer.
//
// This is the heart of the preprocessor and the hottest loop, so it is written
// for speed: a "fast scan" races through ordinary characters using IS_SPECIAL()
// (a single table lookup) and only stops on a character that needs attention.
// The big switch then handles that character -- end of buffer, an operator, a
// backslash-newline continuation, a comment, a string or char literal, a
// newline, a number, or an identifier.  Whenever the scanner runs off the end
// of the buffered text it calls refill_buffer() to get more and continues.
//
// The identifier case is where macros happen: it first runs the cheap
// superimposed-code filter (TMAC1), and only if the name could be a macro
// does it call lookup_token(), which may expand the macro in place.
//
// A "#" seen at the very start of a line is returned to the caller
// (process_directives) so directives can be handled.  While evaluating a #if
// expression (IN_SLOW_SCAN) the scanner returns every token individually and
// remembers, via the static "state", that it stopped on a newline.
//
char *scan_token(char *p)
{
    int c, i;
    char quoc;              // the opening quote of the string/char literal being scanned
    static int state = BEG; // remembers a pending newline between calls (slow scan)

    if (state != BEG)
        goto prevlf;
    for (;;) {
    again:
        // fast scan: skip characters that need no special handling
        while (!IS_SPECIAL(*p++))
            ;
        switch ((unsigned char)*(cpp.tok_ptr = p - 1)) {
        case 0: { // possible end of buffered text (a nul), else an ignorable nul byte
            if (AT_BUF_END(--p)) {
                p = refill_buffer(p);
                goto again;
            } else
                ++p; // ignore null byte
        } break;
        // §6.10.3.4: the end of a macro's expansion region (blue paint).  Drop the
        // marker from the output and un-paint the macro, then keep scanning.  Works
        // in both fast and slow (#if) scans since macros expand in #if too.
        case PAINT_END_MARK:
            cpp.tok_ptr = p - 1; // the marker
            flush_output();      // emit finished text up to it
            if (cpp.paint_top > 0)
                --cpp.paint_top; // region closes: the macro may expand again
            cpp.out_ptr = cpp.tok_ptr = p; // skip the marker (never emitted)
            goto again;
        // Two-character operators in a #if expression (||, &&, ==, !=, <=, >=,
        // <<, >>): glue the second character on so the lexer sees one token.
        case '|':
        case '&':
            for (;;) { // sloscan only
                if (*p++ == *cpp.tok_ptr)
                    break;
                if (AT_BUF_END(--p))
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
                if (AT_BUF_END(--p))
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
                if (AT_BUF_END(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        // Backslash: a backslash-newline is a line continuation -- swallow it
        // (and count the line) so the two source lines read as one.
        case '\\':
            for (;;) {
                if (*p++ == '\n') {
                    ++cpp.line_no[cpp.inc_level];
                    // Phase 2 (C11 §5.1.1.2): splice the two physical lines by
                    // deleting the backslash-newline from the output as well as
                    // the token stream, so the surrounding characters join.
                    cpp.tok_ptr = p - 2; // the '\'
                    flush_output();
                    cpp.out_ptr = cpp.tok_ptr = p;
                    break;
                }
                if (AT_BUF_END(--p))
                    p = refill_buffer(p);
                else {
                    ++p;
                    break;
                }
            }
            break;
        // Slash: if followed by '*' this is a comment.  Normally the comment is
        // deleted (treated like a skipped region: false_level is bumped so
        // flush_output writes nothing), but under -C it is kept, and a comment
        // longer than the buffer is split and re-emitted in pieces.  A lone '/'
        // is just an operator.
        case '/':
            for (;;) {
                if (*p++ == '*') { // comment
                    cpp.in_block_comment = 1;
                    if (!cpp.opt_keep_comments) {
                        cpp.tok_ptr = p - 2;
                        flush_output();
                        ++cpp.false_level;
                    }
                    for (;;) {
                        while (!ISCOM(*p++))
                            ;
                        if (p[-1] == '*')
                            for (;;) {
                                if (*p++ == '/')
                                    goto endcom;
                                if (AT_BUF_END(--p)) {
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
                        } else if (AT_BUF_END(--p)) {
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
                    cpp.in_block_comment = 0;
                    if (!cpp.opt_keep_comments) {
                        // A comment becomes one space (C11 §5.1.1.2 phase 3), so
                        // adjacent tokens do not merge -- but only when we are in
                        // live text, not inside a skipped #if branch.
                        --cpp.false_level;
                        if (cpp.false_level == 0)
                            putc(' ', cpp.out_file);
                        cpp.out_ptr = cpp.tok_ptr = p;
                        goto again;
                    }
                    break;
                }
                // A '//' end-of-line comment (C99): consume up to, but not
                // including, the newline, which is left to be scanned normally.
                if (p[-1] == '/') {
                    if (!cpp.opt_keep_comments) {
                        cpp.tok_ptr = p - 2;
                        flush_output();
                        ++cpp.false_level;
                    }
                    for (;;) {
                        while (*p != '\n' && *p != '\0')
                            ++p;
                        if (*p != '\0')
                            break; // reached the newline
                        if (AT_BUF_END(p)) {
                            cpp.tok_ptr = p;
                            p           = refill_buffer(p);
                        } else
                            ++p; // ignore null byte
                    }
                    if (!cpp.opt_keep_comments) {
                        --cpp.false_level;
                        if (cpp.false_level == 0)
                            putc(' ', cpp.out_file);
                        cpp.out_ptr = cpp.tok_ptr = p;
                        goto again;
                    }
                    break; // -C: keep the '//...' run as the current token
                }
                if (AT_BUF_END(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        // String or character literal: copy through until the matching closing
        // quote.  Backslash escapes are honored, and an unescaped newline ends
        // the literal (a lenient rule this old preprocessor uses).
        case '"':
        case '\'': {
            quoc = p[-1]; // remember which quote opened it
            for (;;) {
                while (!ISQUO(*p++))
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
                        if (AT_BUF_END(--p))
                            p = refill_buffer(p);
                        else {
                            ++p;
                            break;
                        }
                    }
                else if (AT_BUF_END(--p))
                    p = refill_buffer(p);
                else
                    ++p; // it was a different quote character
            }
        } break;
        // Newline: count the line.  In a #if expression, stop here (remembering
        // via state=LF that a newline is pending).  Otherwise look at the start
        // of the next line: if it begins with '#', return it to the caller so a
        // directive can be processed; if not, keep scanning.
        case '\n': {
            ++cpp.line_no[cpp.inc_level];
            if (IN_SLOW_SCAN()) {
                state = LF;
                return (p);
            }
        prevlf:
            state = BEG;
            for (;;) {
                // §6.10: horizontal white space may precede the '#' that
                // introduces a directive.  Skip it (BLANK excludes '\n', so we
                // stay on this line) before testing for the '#'.
                while (cpp.char_class[(unsigned char)*p] == BLANK)
                    ++p;
                if (*p++ == '#')
                    return (p);
                if (AT_BUF_END(cpp.tok_ptr = --p))
                    p = refill_buffer(p);
                else
                    goto again;
            }
        } break;
        // A number: consume the run of digits.
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
                while (ISNUM(*p++))
                    ;
                if (AT_BUF_END(--p))
                    p = refill_buffer(p);
                else
                    break;
            }
            break;
        // An identifier (letter or '_' start): this is where macro names are
        // recognized.  If we are skipping a false #if branch, just consume it
        // (goto nomac).  Otherwise run the superimposed-code filter character by
        // character (TMAC1): the moment a position rules out every macro
        // name we jump to nomac and skip the lookup; if the name survives the
        // filter we call lookup_token, which expands it if it is really a macro.
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
        // High bytes start a UTF-8 identifier (e.g. Cyrillic macro names).  The
        // range deliberately skips the marker bytes 0xFA-0xFE: PAINT_END_MARK
        // (0xFB) has its own case earlier in the switch, and the four WB markers
        // (0xFA, 0xFC-0xFE) have no case, so a stray one in live text just falls
        // through and is emitted as-is rather than starting an identifier.
        case 0x80 ... 0xF9:
        case 0xFF:

            if (cpp.false_level)
                goto nomac;
            for (;;) {
                c = p[-1];
                TMAC1(c, B0);
                i = *p++;
                if (!ISID(i))
                    goto endid;
                TMAC1(i, B1);
                c = *p++;
                if (!ISID(c))
                    goto endid;
                TMAC1(c, B2);
                i = *p++;
                if (!ISID(i))
                    goto endid;
                TMAC1(i, B3);
                c = *p++;
                if (!ISID(c))
                    goto endid;
                TMAC1(c, B4);
                i = *p++;
                if (!ISID(i))
                    goto endid;
                TMAC1(i, B5);
                c = *p++;
                if (!ISID(c))
                    goto endid;
                TMAC1(c, B6);
                i = *p++;
                if (!ISID(i))
                    goto endid;
                TMAC1(i, B7);
                while (ISID(*p++))
                    ;
                if (AT_BUF_END(--p)) {
                    refill_buffer(p);
                    p = cpp.tok_ptr + 1;
                    continue;
                }
                goto lokid;
            endid:
                if (AT_BUF_END(--p)) {
                    refill_buffer(p);
                    p = cpp.tok_ptr + 1;
                    continue;
                }
            lokid:
                // the name passed the filter: look it up; if it was a macro,
                // lookup_token set scan_ptr to where scanning should resume.
                lookup_token(cpp.tok_ptr, p, 0);
                if (cpp.scan_ptr) {
                    p = cpp.scan_ptr;
                    goto again;
                } else
                    break;
            nomac:
                // definitely not a macro: just consume the rest of the identifier.
                while (ISID(*p++))
                    ;
                if (AT_BUF_END(--p)) {
                    p = refill_buffer(p);
                    goto nomac;
                } else
                    break;
            }
            break;
        } // end of switch

        // In normal scanning we loop to grab the next token; in a #if expression
        // we hand back one token at a time.
        if (IN_SLOW_SCAN())
            return (p);
    } // end of infinite loop
}

//
// Scan forward past whitespace and return the first non-blank token, so callers
// that only care about real tokens do not have to test for blanks themselves.
//
char *skip_blanks(char *p)
{
    do {
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = scan_token(p);
    } while (cpp.char_class[(unsigned char)*cpp.tok_ptr] == BLANK);
    return (p);
}
