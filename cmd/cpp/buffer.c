//
// C preprocessor: scan-buffer refill/spill and output flushing.
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

//
// Emit a "# line "file"" marker so the next tool (compiler) knows which source
// line the following text came from.  Suppressed by the -P option
// (cpp.opt_no_lines).  Called whenever the line numbering jumps -- after an
// #include, at end of file, etc.
//
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
// 2 chars: the char WARN_MARK, and a char containing the parameter number.
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

//
// Write the finished text -- the bytes between out_ptr and tok_ptr -- to the
// output file, then advance out_ptr.  Does nothing while inside a skipped #if
// block (false_level != 0).
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
    f = cpp.out_file;
    while (p1 < cpp.tok_ptr)
        putc(*p1++, f);
    cpp.out_ptr = p1;
}

//
// Translate the nine C11 trigraph sequences (§5.2.1.1, translation phase 1) in
// place within [buf, buf+n), returning the new (shorter or equal) length.  Each
// "??x" becomes its single character; unless -w was given, a warning is issued
// per conversion.  A trailing "?" or "??" whose third character is not yet in
// the buffer (src+2 >= end) is left literal, so the caller can carry it across
// the next read (see refill_buffer).
//
static int translate_trigraphs(char *buf, int n)
{
    static const struct {
        char t, r;
    } map[] = { { '=', '#' }, { '(', '[' }, { ')', ']' }, { '<', '{' }, { '>', '}' },
                { '!', '|' }, { '\'', '^' }, { '-', '~' }, { '/', '\\' } };
    char *src = buf, *dst = buf;
    const char *end = buf + n;

    while (src < end) {
        if (src[0] == '?' && src + 2 < end && src[1] == '?') {
            char c = src[2], r = 0;
            for (unsigned k = 0; k < sizeof map / sizeof *map; k++)
                if (map[k].t == c) {
                    r = map[k].r;
                    break;
                }
            if (r) {
                if (!cpp.opt_no_warnings)
                    ppwarn("trigraph ??%c converted to %c", c, r);
                *dst++ = r;
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    return (int)(dst - buf);
}

//
// Make more input available when the scanner has reached the end of what is in
// the buffer.  Steps: flush finished output, keep the partial token
// (tok_ptr..p), then top the buffer up from -- in priority order -- pushed-back
// macro text, then the current file, then (on EOF) the file that #included it.
// At true end of input it flushes and exits.  Returns the updated scan pointer.
//
char *refill_buffer(char *p)
{
    // save chars from tok_ptr to p, read into arena at buf_mid contiguous with p.
    //
    char *np;
    const char *op;

    flush_output();
    np = cpp.buf_mid - (p - cpp.tok_ptr);
    op = cpp.tok_ptr;
    if (AT_BUF_START(np + 1)) {
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
            if (cpp.opt_trigraphs) {
                // Phase-1 trigraph translation on the freshly read bytes.  Any
                // '?' held from the previous read of this file (a trigraph split
                // across the read boundary) is prepended before translating.
                int hold   = cpp.trig_nhold[cpp.inc_level];
                int ninbuf = read(cpp.in_fd, cpp.buf_mid + hold, BUFSIZ - hold);
                if (ninbuf > 0 || hold > 0) {
                    if (ninbuf < 0)
                        ninbuf = 0;
                    for (int i = 0; i < hold; i++)
                        cpp.buf_mid[i] = '?';
                    int m  = translate_trigraphs(cpp.buf_mid, hold + ninbuf);
                    int nh = 0;
                    // Re-hold a trailing '?' run (max 2) unless this was EOF.
                    if (ninbuf > 0)
                        while (nh < 2 && m > 0 && cpp.buf_mid[m - 1] == '?') {
                            --m;
                            ++nh;
                        }
                    cpp.trig_nhold[cpp.inc_level] = nh;
                    cpp.buf_end                   = cpp.buf_mid + m;
                    *cpp.buf_end                  = '\0';
                    return (p);
                }
                // ninbuf == 0 && hold == 0: real EOF, fall through to EOF handling.
            } else {
                int ninbuf = read(cpp.in_fd, cpp.buf_mid, BUFSIZ);
                if (0 < ninbuf) {
                    cpp.buf_end  = cpp.buf_mid + ninbuf;
                    *cpp.buf_end = '\0';
                    return (p);
                }
            }
            // end of #include file
            if (cpp.inc_level == 0) { // end of input
                if (cpp.paren_level > 0) {
                    int n = cpp.paren_level, tlin = cpp.line_no[cpp.inc_level];
                    char *tfil                 = cpp.inc_file[cpp.inc_level];
                    cpp.line_no[cpp.inc_level] = cpp.call_line;
                    if (cpp.call_file)
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
                    return (p);
                }
                if (cpp.paren_level < 0) {
                    // A function-macro '(' look-ahead reached end of input: the
                    // name is not an invocation.  Hand a nul token back to
                    // expand_macro (do NOT exit); it will emit the bare name
                    // verbatim.  The nul is left just short of buf_end so
                    // scan_token returns it instead of re-refilling here.
                    cpp.paren_level = 0;
                    *p              = '\0';
                    cpp.buf_end     = p + 1;
                    *cpp.buf_end    = '\0';
                    return (p);
                }
                if (cpp.in_block_comment)
                    pperror("unterminated comment");
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

//
// Macro expansion pushes generated text in front of the scan point, which can
// run off the low end of the buffer.  spill_buffer makes room: it copies up to
// BUFSIZ characters from the right (high) end of the buffer into a fresh side
// buffer recorded on push_stack (they will be re-read later by refill_buffer),
// then slides the rest of the buffer to the right.  Pointers are adjusted and
// the shifted scan pointer is returned.  If all pushback slots are in use it
// first drains some by flushing.
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
        if (AT_BUF_END(op))
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
    if (AT_BUF_START(np))
        pperror("token too long");
    d = np - cpp.out_ptr;
    cpp.out_ptr += d;
    cpp.tok_ptr += d;
    cpp.recur_bound_adj += d;
    return (p + d);
}
