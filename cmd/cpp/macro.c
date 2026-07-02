//
// C preprocessor: macro definition, symbol table, and expansion.
//
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

// The implicit final formal of a variadic macro; a body reference to it is bound
// to all trailing arguments (§6.10.3).
static const char va_args_name[] = "__VA_ARGS__";

//
// Does formal-parameter name "a" equal the token in the buffer spanning
// [p1, p2)?  The token is not null-terminated, so we temporarily write a '\0' at
// p2, compare, then restore the byte.  Used when scanning a macro body to spot
// where a parameter name appears.
//
static int formal_matches(const char *a, const char *p1, char *p2)
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
// Is macro "sp" currently painted -- i.e. is its expansion region still being
// rescanned (§6.10.3.4)?  If so, a recurrence of its name is left un-expanded
// instead of looping.  The stack is small (bounded by the nesting of open
// expansions), so a linear scan is cheap.
//
static int macro_is_painted(const struct symtab *sp)
{
    int i;
    for (i = 0; i < cpp.paint_top; i++)
        if (cpp.paint_stack[i] == sp)
            return 1;
    return 0;
}

//
// Handle a "#define" line: parse the macro name, the optional parameter list,
// and the replacement text, and store it in the symbol table.
//
// The stored body is not copied verbatim: wherever a parameter name appears it
// is replaced by a two-byte marker (a parameter number followed by WARN_MARK) so that
// expand_macro can later splice in the actual arguments without re-parsing.  A
// redefinition that differs from the previous one draws a warning; an identical
// one is silently accepted and its space reclaimed.
//
char *do_define(char *p)
{
    char *pin, *psav, *cf;
    char **pf, **qf;
    int b, c, params;
    int variadic = 0; // set once a '...' formal is seen: last formal is __VA_ARGS__
    int va_num   = 0; // 1-based number of the variadic formal (the last one), else 0
    struct symtab *np;
    char *oldval, *oldsavch;
    char *hashpos = NULL; // side-buffer position of a pending '#' (stringize) operator
    const char *body_start; // side-buffer position where the replacement text begins
    int paste_pending = 0; // a '##' was just seen; its right operand pastes onto the left
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
    // §6.10.8.4: "defined" is reserved and may not be used as a macro name.
    if (strcmp(np->name, "defined") == 0) {
        pperror("\"defined\" cannot be used as a macro name");
        while (*cpp.tok_ptr != '\n') // consume the rest of the line
            p = skip_blanks(p);
        --cpp.false_level;
        return (p);
    }
    // §6.10.8.4: a predefined macro may not be the subject of #define, whatever
    // the replacement list (so this precedes the identical-redefinition check).
    if (np->predefined) {
        pperror("predefined macro \"%s\" cannot be redefined", np->name);
        while (*cpp.tok_ptr != '\n')
            p = skip_blanks(p);
        --cpp.false_level;
        return (p);
    }
    if ((oldval = np->value))
        cpp.side_ptr = oldsavch; // was previously defined
    b  = 1;
    cf = pin;
    while (cf < p) { // update macro_bits
        c = *cf++;
        XMAC1(c, b, |=);
        b = (b + b) & 0xFF;
    }
    params      = 0;
    cpp.out_ptr = cpp.tok_ptr = p;
    p                         = scan_token(p);
    pin                       = cpp.tok_ptr;
    // If the name is immediately followed by '(' this is a function-like macro:
    // collect the parameter names into formal[] / formtxt.
    if (*pin == '(') { // with parameters; identify the formals
        int prev_was_formal = 0; // previous token was a normal named formal
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
            if (*pin == ',') {
                prev_was_formal = 0;
                continue;
            }
            if (*pin == '.' && pin[1] == '.' && pin[2] == '.') {
                // '...' : the variadic formal that binds all trailing arguments
                // (§6.10.3).  In slow scan the first '.' is a one-char token, so
                // step p over the other two.
                if (prev_was_formal) {
                    // GNU named varargs `#define P(args...)`: the identifier just
                    // before '...' becomes the variadic name; do not add a
                    // separate __VA_ARGS__ formal.
                    variadic = 1;
                    prev_was_formal = 0;
                    p += 2;
                    continue;
                }
                // Anonymous C99 form `#define P(...)`: an implicit final formal
                // literally named __VA_ARGS__.
                if (pf >= &formal[MAXFRM])
                    pperror("%s: too many formals", np->name);
                else {
                    *pf++ = cf;
                    strcpy(cf, va_args_name);
                    cf += sizeof(va_args_name); // name plus its '\0'
                    ++params;
                }
                variadic = 1;
                p += 2;
                continue;
            }
            if (cpp.char_class[(unsigned char)*pin] != IDENT) {
                c  = *p;
                *p = '\0';
                pperror("bad formal: %s", pin);
                *p = c;
                prev_was_formal = 0;
            } else if (pf >= &formal[MAXFRM]) {
                c  = *p;
                *p = '\0';
                pperror("too many formals: %s", pin);
                *p = c;
                prev_was_formal = 0;
            } else {
                *pf++ = cf;
                while (pin < p)
                    *cf++ = *pin++;
                *cf++ = '\0';
                ++params;
                prev_was_formal = 1;
            }
        }
        if (params == 0)
            --params; // #define foo() ...
        if (variadic)
            va_num = params; // the variadic formal is always the last one
    } else if (*pin == '\n') {
        --cpp.line_no[cpp.inc_level];
        --p;
    }

    // remember beginning of macro body, so that we can
    // warn if a redefinition is different from old value.
    //
    oldsavch = psav = cpp.side_ptr;
    body_start      = psav;
    // Copy the replacement text into the side buffer, token by token, replacing
    // each occurrence of a parameter name (even inside string/char literals)
    // with the marker "<param number><WARN_MARK>".
    for (;;) { // accumulate definition until linefeed
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = scan_token(p);
        pin                       = cpp.tok_ptr;
        if (*pin == '\\' && pin[1] == '\n')
            continue; // ignore escaped lf
        if (*pin == '\n')
            break;
        if (params) { // mark the appearance of formals in the definiton
            int paste_now;
            // '##' (token paste), §6.10.3.3: its right operand is pasted directly
            // onto the left, so first strip any white space following the '##'.
            if (paste_pending && cpp.char_class[(unsigned char)*pin] == BLANK)
                continue;
            paste_now     = paste_pending; // is *this* token the right operand of a '##'?
            paste_pending = 0;
            // The '#' (stringize) operator, §6.10.3.2.  In a function-like macro
            // body a '#' must be followed by a parameter; the pair is replaced by
            // a stringize marker so expand_macro can quote the raw argument.  A
            // '#' not followed by a parameter is a constraint violation.
            if (hashpos != NULL) { // resolve the '#' seen in a previous iteration
                char *hp = hashpos;
                hashpos  = NULL;
                if (cpp.char_class[(unsigned char)*pin] == BLANK) {
                    hashpos = hp; // blanks between '#' and its parameter: keep waiting
                    while (pin < p)
                        *psav++ = *pin++;
                    continue;
                }
                if (*pin == '#' && (p - pin) == 1) { // '##' token-paste operator
                    psav = hp;                        // drop the pending '#'
                    while (psav > body_start && (psav[-1] == ' ' || psav[-1] == '\t'))
                        --psav; // strip white space preceding the '##'
                    if (psav <= body_start) {
                        pperror("'##' at start of macro replacement list");
                        continue; // no left operand to paste onto
                    }
                    if ((unsigned char)psav[-1] == WARN_MARK) // left operand is a parameter: keep it raw
                        psav[-1] = (char)PASTE_MARK;
                    paste_pending = 1; // the next token is the right operand
                    continue;
                }
                c = 0; // parameter number, or 0 if the token is not a formal
                if (cpp.char_class[(unsigned char)*pin] == IDENT)
                    for (qf = pf; --qf >= formal;)
                        if (formal_matches(*qf, pin, p)) {
                            c = qf - formal + 1;
                            break;
                        }
                if (c != 0) {     // '#' <parameter>: emit stringize marker
                    psav    = hp; // drop the '#' and any blanks after it
                    *psav++ = c;
                    *psav++ = (char)STRINGIZE_MARK;
                    pin     = p;
                    continue;
                }
                pperror("'#' is not followed by a macro parameter");
                // fall through: copy the current token, leaving the '#' literal
            } else if (*pin == '#' && (p - pin) == 1) { // a fresh '#': start waiting
                *psav++ = *pin++;
                hashpos = psav - 1;
                continue;
            }
            if (cpp.char_class[(unsigned char)*pin] == IDENT) {
                int num = 0; // resolved 1-based formal number, or 0 if not a formal
                for (qf = pf; --qf >= formal;) {
                    if (formal_matches(*qf, pin, p)) {
                        num = qf - formal + 1;
                        break;
                    }
                }
                // GNU named varargs: bare __VA_ARGS__ also refers to the trailing-
                // args formal, which may carry a user-chosen name (#define P(a...)).
                if (num == 0 && variadic && formal_matches(va_args_name, pin, p))
                    num = va_num;
                if (num != 0) {
                    // a '##' operand keeps its raw actual (PASTE_MARK); a plain
                    // parameter substitutes its expanded actual (WARN_MARK)
                    char mark = paste_now ? (char)PASTE_MARK : (char)WARN_MARK;
                    // GNU ", ## __VA_ARGS__" comma elision: when the '##' left
                    // operand is a literal comma and the right operand is the
                    // variadic formal, drop the comma here; expand_macro re-emits
                    // it only when the variadic actual is non-empty.
                    if (paste_now && num == va_num && // num != 0 here, so va_num != 0
                        psav > body_start && psav[-1] == ',') {
                        --psav;
                        mark = (char)COMMA_PASTE_MARK;
                    }
                    *psav++ = num;
                    *psav++ = mark;
                    pin     = p;
                } else if (!variadic && formal_matches(va_args_name, pin, p)) {
                    // §6.10.3p5: __VA_ARGS__ is legal only in a variadic macro.
                    pperror("__VA_ARGS__ can only appear in a variadic macro");
                }
            } else if (*pin == '"' || *pin == '\'') { // inside quotation marks, too
                char quoc = *pin;
                for (*psav++ = *pin++; pin < p && *pin != quoc;) {
                    while (pin < p && !ISID(*pin))
                        *psav++ = *pin++;
                    cf = pin;
                    while (cf < p && ISID(*cf))
                        ++cf;
                    for (qf = pf; --qf >= formal;) {
                        if (formal_matches(*qf, pin, cf)) {
                            *psav++ = qf - formal + 1;
                            *psav++ = (char)WARN_MARK;
                            pin     = cf;
                            break;
                        }
                    }
                    while (pin < cf)
                        *psav++ = *pin++;
                }
            }
        } else if (cpp.char_class[(unsigned char)*pin] == IDENT &&
                   formal_matches(va_args_name, pin, p)) {
            // §6.10.3p5: __VA_ARGS__ must not appear in an object-like macro
            // (params == 0, so it is never variadic).
            pperror("__VA_ARGS__ can only appear in a variadic macro");
        }
        while (pin < p)
            *psav++ = *pin++;
    }
    if (hashpos != NULL) // '#' at end of a function-like body, no parameter follows
        pperror("'#' is not followed by a macro parameter");
    if (paste_pending) // '##' at end of a function-like body, no right operand follows
        pperror("'##' at end of macro replacement list");
    *psav++ = variadic ? (params | VA_FLAG) : params;
    *psav++ = '\0';
    if ((cf = oldval) != NULL) { // redefinition
        --cf;                    // skip no. of params, which may be zero
        while (*--cf)
            ;                              // go back to the beginning
        if (0 != strcmp(++cf, oldsavch)) { // redefinition different from old
            --cpp.line_no[cpp.inc_level];
            pperror("%s redefined", np->name);
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

//
// Find the null-terminated name "namep" in the symbol hash table and return its
// entry.  The name is hashed to a starting slot, then we probe backwards
// (wrapping around once) until we find the name or an empty slot.  "enterf"
// selects the behavior: >0 inserts the name if absent, DROP marks an existing
// entry deleted, 0 just looks.  The result is also cached in cpp.last_sym.
//
struct symtab *lookup(char *namep, int enterf)
{
    const char *np, *snp;
    int c, i;
    int around; // set once we have wrapped past slot 0, to detect a full table
    struct symtab *sp;

    // namep had better not be too long (currently, <=8 chars)
    np     = namep;
    around = 0;
    i      = cpp.hash_seed;
    while ((c = *np++))
        i += i + c;
    c = i; // c=i for usage on pdp11
    c %= SYMSIZ;
    if (c < 0)
        c += SYMSIZ;
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
                sp = &cpp.symbols[SYMSIZ - 1];
            }
        }
    }
    if (enterf > 0)
        sp->name = namep;
    return (cpp.last_sym = sp);
}

//
// Look up the token occupying the buffer span [p1, p2) -- the convenient form
// the scanner uses, since tokens in the buffer are not null-terminated and names
// are significant only to 8 characters.  We null-terminate temporarily, truncate
// to 8 chars, look it up, then restore the bytes.  If it turns out to be a
// defined macro (and we are not in a skipped branch) it is expanded right away
// via expand_macro, and cpp.scan_ptr is set to where scanning should resume.
//
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

//
// Stringize the raw argument text [a0,a1) into out[] as a string literal, per
// §6.10.3.2: drop leading/trailing white space, collapse each interior
// white-space run to a single space, and within a string- or character-literal
// spelling insert a '\' before every '"' and '\'.  Returns the byte count.
//
static int stringize(const char *a0, const char *a1, char *out)
{
    char *w       = out;
    const char *s = a0;
    int quote     = 0; // 0 outside a literal, else the opening quote character
    int pending   = 0; // a collapsed space is owed before the next emitted char

    *w++ = '"';
    while (s < a1 && (*s == ' ' || *s == '\t' || *s == '\n'))
        ++s; // drop leading white space (never inside a literal here)
    while (s < a1) {
        char ch = *s;
        if (!quote && (ch == ' ' || ch == '\t' || ch == '\n')) {
            pending = 1; // collapse the run; a lone space is emitted only if more follows
            ++s;
            continue;
        }
        if (pending) {
            *w++    = ' ';
            pending = 0;
        }
        if (quote) {
            if (ch == '\\') { // an escape sequence: backslash + the escaped char
                *w++ = '\\';
                *w++ = '\\';
                if (++s < a1) {
                    if (*s == '"' || *s == '\\')
                        *w++ = '\\';
                    *w++ = *s++;
                }
                continue;
            }
            if (ch == '"') { // '"' is escaped; it also closes a string literal
                *w++ = '\\';
                *w++ = '"';
                if (quote == '"')
                    quote = 0;
                ++s;
                continue;
            }
            if (ch == '\'') { // ''' is not escaped; it closes a char literal
                *w++ = '\'';
                if (quote == '\'')
                    quote = 0;
                ++s;
                continue;
            }
            *w++ = ch; // ordinary literal content (interior white space is significant)
            ++s;
            continue;
        }
        if (ch == '"' || ch == '\'') { // entering a literal spelling
            quote = ch;
            if (ch == '"')
                *w++ = '\\'; // the opening '"' is part of the spelling -> escaped
            *w++ = ch;
            ++s;
            continue;
        }
        *w++ = ch; // ordinary token char ('\' outside a literal is left unescaped)
        ++s;
    }
    *w++ = '"';
    return (int)(w - out);
}

//
// Expand the raw argument text [a0,a1) to its fully macro-expanded form and store
// it, NUL-terminated, at out[cap]; return out.  A normal parameter substitutes
// this *expanded* argument (§6.10.3.1p2), which is what lets the STR/XSTR
// stringize-through-indirection idiom yield the expanded value.  The engine is
// reused over an isolated buffer whose output is captured through a memory
// stream; every global the scanner touches is saved and restored around the run.
//
char *expand_text(const char *a0, const char *a1, char *out, int cap)
{
    char subarena[8 + 2 * BUFSIZ + 8];
    char *start = subarena + 8 + BUFSIZ; // mirror the real arena: BUFSIZ of pushback headroom
    long n      = a1 - a0;
    char *w     = out;

    if (n < 0)
        n = 0;
    // Too large to load with its sentinel without risking a refill from real
    // input: fall back to the raw (unexpanded) text.
    if (n > BUFSIZ - 8) {
        while (a0 < a1 && w < out + cap - 1)
            *w++ = *a0++;
        *w = '\0';
        return out;
    }

    // Save engine state.
    char *s_buf_start = cpp.buf_start, *s_buf_mid = cpp.buf_mid, *s_buf_end = cpp.buf_end;
    char *s_out = cpp.out_ptr, *s_tok = cpp.tok_ptr, *s_scan = cpp.scan_ptr;
    char *s_scan_tab = cpp.scan_tab, *s_mac = cpp.macro_name, *s_rb = cpp.recur_bound;
    FILE *s_of  = cpp.out_file;
    int s_false = cpp.false_level, s_paren = cpp.paren_level;
    int s_rd = cpp.recur_depth, s_rba = cpp.recur_bound_adj;
    int s_ptop = cpp.push_top, s_ftop = cpp.free_top;
    int s_line = cpp.line_no[cpp.inc_level];
    // The paint array is intentionally NOT cleared: a macro active in the outer
    // expansion stays painted during this argument prescan (§6.10.3.1).  Only the
    // top is saved/restored, so any region left open when the isolated scan stops
    // at its "\n#" sentinel is discarded instead of leaking.
    int s_ptaint = cpp.paint_top;

    char *mbuf  = NULL;
    size_t mlen = 0;
    FILE *mf    = open_memstream(&mbuf, &mlen);
    if (mf == NULL) { // capture unavailable: fall back to raw text
        while (a0 < a1 && w < out + cap - 1)
            *w++ = *a0++;
        *w = '\0';
        return out;
    }

    // Load  raw + "\n#"  into the isolated buffer.  The "\n#" makes scan_token
    // return (it looks like the start of a directive) so we regain control.
    memcpy(start, a0, (size_t)n);
    start[n]      = '\n';
    start[n + 1]  = '#';
    start[n + 2]  = '\0';
    cpp.buf_start = subarena + 8;
    cpp.buf_mid   = start;
    cpp.buf_end   = start + n + 2;
    cpp.out_ptr = cpp.tok_ptr = start;
    cpp.out_file              = mf;
    cpp.false_level           = 0;
    cpp.paren_level           = 0;
    cpp.recur_depth           = 0;
    cpp.recur_bound           = start;
    cpp.recur_bound_adj       = 0;
    SET_FAST_SCAN();
    scan_token(start);
    flush_output(); // push [out_ptr,tok_ptr) (the expanded text) into the memory stream
    fclose(mf);

    // Restore engine state.
    cpp.buf_start              = s_buf_start;
    cpp.buf_mid                = s_buf_mid;
    cpp.buf_end                = s_buf_end;
    cpp.out_ptr                = s_out;
    cpp.tok_ptr                = s_tok;
    cpp.scan_ptr               = s_scan;
    cpp.scan_tab               = s_scan_tab;
    cpp.macro_name             = s_mac;
    cpp.recur_bound            = s_rb;
    cpp.out_file               = s_of;
    cpp.false_level            = s_false;
    cpp.paren_level            = s_paren;
    cpp.recur_depth            = s_rd;
    cpp.recur_bound_adj        = s_rba;
    cpp.push_top               = s_ptop;
    cpp.free_top               = s_ftop;
    cpp.line_no[cpp.inc_level] = s_line;
    cpp.paint_top              = s_ptaint;

    // Copy the captured expansion (minus any trailing newline) into out.
    while (mlen > 0 && mbuf[mlen - 1] == '\n')
        --mlen;
    {
        size_t i;
        for (i = 0; i < mlen && w < out + cap - 1; i++)
            *w++ = mbuf[i];
    }
    *w = '\0';
    free(mbuf);
    return out;
}

//
// Handle the C11 _Pragma("...") unary operator (§6.10.9).  p points just past the
// _Pragma name; the preceding text is already flushed.  We read the parenthesized
// string-literal argument, destringize it (drop the surrounding quotes, turn \"
// into " and \\ into \), and emit it as a "#pragma ..." line to the output,
// leaving the surrounding tokens intact.
//
static char *pragma_operator(char *p)
{
    char text[BUFSIZ]; // destringized pragma text
    char *w        = text;
    const char *s = 0, *e = 0;

    SET_SLOW_SCAN();
    ++cpp.false_level; // suppress expansion and keep flush_output inert
    do
        p = skip_blanks(p);
    while (*cpp.tok_ptr == '\n');
    if (*cpp.tok_ptr != '(') {
        pperror("_Pragma: missing '('");
        goto done;
    }
    do
        p = skip_blanks(p);
    while (*cpp.tok_ptr == '\n');
    if (*cpp.tok_ptr != '"') {
        pperror("_Pragma: string literal expected");
        goto done;
    }
    // destringize [tok_ptr, p): drop the quotes, unescaping the escaped
    // backslash and double-quote spellings
    s = cpp.tok_ptr + 1; // past the opening quote
    e = p - 1;           // the closing quote
    while (s < e && w < text + sizeof(text) - 1) {
        if (*s == '\\' && s + 1 < e && (s[1] == '"' || s[1] == '\\'))
            ++s;
        *w++ = *s++;
    }
    do
        p = skip_blanks(p);
    while (*cpp.tok_ptr == '\n');
    if (*cpp.tok_ptr != ')')
        pperror("_Pragma: missing ')'");
done:
    *w = '\0';
    --cpp.false_level;
    SET_FAST_SCAN();
    cpp.out_ptr = cpp.tok_ptr = p;                 // drop the consumed _Pragma(...) region first
    fprintf(cpp.out_file, "\n#pragma %s\n", text); // then emit the directive line
    return (p);
}

//
// Expand a macro call.  sp is the macro; p points just past its name.  For a
// function-like macro we first scan the "(actual, actual, ...)" argument list.
// Then we push the stored body onto the front of the input (in reverse), and
// wherever the body has a parameter marker we push that argument's text instead
// -- so the generated text is simply rescanned by scan_token as if the user had
// typed it.  The built-ins __LINE__ and __FILE__ synthesize their body here.
//
// A recursion guard (recur_depth / recur_bound) stops a macro that expands to
// itself from looping forever, unless -R (opt_recurse) was given.  Returns the
// new scan pointer.
//
char *expand_macro(char *p, struct symtab *sp)
{
    char *ca, *vp;
    int params;
    char *actual[MAXFRM];    // actual[n] is the raw text of the nth actual (for '#')
    char *expanded[MAXFRM];  // expanded[n] is its macro-expanded text (for normal use)
    char acttxt[BUFSIZ];     // space for the raw actuals
    char exptxt[4 * BUFSIZ]; // space for the expanded actuals

    if (0 == (vp = sp->value))
        return (p);
    // §6.10.3.4: the name recurs inside its own (still-open) expansion.  Leave it
    // un-expanded ("blue paint"); the bytes in [tok_ptr, p) are flushed as text
    // and scanning resumes just past the name, so a function-like name here is not
    // treated as a call (its following '(...)' is emitted verbatim).
    if (macro_is_painted(sp))
        return (p);
    if ((p - cpp.recur_bound) <= cpp.recur_bound_adj) {
        if (++cpp.recur_depth > SYMSIZ && !cpp.opt_recurse) {
            pperror("%s: macro recursion", sp->name);
            return (p);
        }
    } else
        cpp.recur_depth = 0; // level decreased
    cpp.recur_bound     = p;
    cpp.recur_bound_adj = 0; // new target for decrease in level
    cpp.macro_name      = sp->name;
    flush_output();
    if (sp == cpp.sym_pragma_op) // _Pragma("...") operator (§6.10.9)
        return pragma_operator(p);
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
    // Function-like macro: scan the parenthesized actual arguments, recording a
    // pointer to each one in actual[].  Expansion is suppressed while we do this.
    if (0 != (params = *--vp & 0xFF)) { // definition calls for params
        char **pa;
        int variadic; // last formal is __VA_ARGS__: absorbs all trailing actuals
        int nformals; // number of formals (incl. __VA_ARGS__)
        ca = acttxt;
        pa = actual;
        if (params == 0xFF) {
            params   = 1; // #define foo() ...
            variadic = 0;
        } else if (params & VA_FLAG) {
            params &= ~VA_FLAG; // last formal is __VA_ARGS__
            variadic = 1;
        } else
            variadic = 0;
        nformals = params;
        SET_SLOW_SCAN();
        ++cpp.false_level; // no expansion during search for actuals
        cpp.paren_level = -1;
        do
            p = skip_blanks(p);
        while (*cpp.tok_ptr == '\n'); // skip \n too
        if (*cpp.tok_ptr != '(') {
            // Function-like macro name not followed by '(': not an invocation.
            // Restore scan state and emit the name verbatim.  The name bytes
            // may be gone from the buffer (refill_buffer relocates it) and
            // there is no blue-paint yet, so we must not push the name back (it
            // would re-expand); write it straight to the output instead.
            cpp.paren_level = 0;
            --cpp.false_level;
            SET_FAST_SCAN();
            fputs(sp->name, cpp.out_file);
            putc(' ', cpp.out_file); // separator so the next token can't fuse
            if (*cpp.tok_ptr == '\0')
                ++cpp.tok_ptr;         // step over refill_buffer's EOF sentinel
            cpp.out_ptr = cpp.tok_ptr; // drop the (stale) name region
            return (cpp.tok_ptr);      // resume at the peeked token
        }
        {
            cpp.call_line = cpp.line_no[cpp.inc_level];
            cpp.call_file = cpp.inc_file[cpp.inc_level];
            for (cpp.paren_level = 1; cpp.paren_level != 0;) {
                // The variadic argument (the last formal) absorbs the rest of the
                // list, so top-level commas do not end it (§6.10.3).
                int collecting_va = variadic && (pa - actual) == nformals - 1;
                *ca++             = '\0';
                for (;;) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = scan_token(p);
                    if (*cpp.tok_ptr == '(')
                        ++cpp.paren_level;
                    if (*cpp.tok_ptr == ')' && --cpp.paren_level == 0) {
                        --params;
                        break;
                    }
                    if (cpp.paren_level == 1 && *cpp.tok_ptr == ',' && !collecting_va) {
                        --params;
                        break;
                    }
                    while (cpp.tok_ptr < p)
                        *ca++ = *cpp.tok_ptr++;
                    if (ca > &acttxt[BUFSIZ])
                        pperror("%s: actuals too long", sp->name);
                }
                if (pa >= &actual[MAXFRM])
                    pperror("%s: argument mismatch", sp->name);
                else
                    *pa++ = ca;
            }
        }
        // A variadic macro may omit the trailing variadic argument entirely (GNU
        // extension); that leaves exactly the __VA_ARGS__ formal unfilled and is
        // not an error.  Any other count mismatch still is.
        if (params != 0 && !(variadic && params == 1))
            pperror("%s: argument mismatch", sp->name);
        while (--params >= 0)
            *pa++ = &""[1]; // null string for missing actuals
        --cpp.false_level;
        SET_FAST_SCAN();
        // Pre-expand each raw actual into expanded[] so a normal parameter
        // substitutes the macro-expanded argument (a '#'/'##' operand keeps the
        // raw actual[]).  expanded[n] points just past its text, which is
        // preceded by a '\0', so the back-to-front push reads it like actual[].
        {
            int nact = (int)(pa - actual), k;
            char *ep = exptxt;
            for (k = 0; k < nact; k++) {
                const char *a1 = actual[k], *a0 = a1;
                while (a0[-1] != '\0') // start of this raw actual's text
                    --a0;
                *ep++ = '\0';
                expand_text(a0, a1, ep, (int)(exptxt + sizeof(exptxt) - ep));
                ep += strlen(ep);
                expanded[k] = ep; // end pointer (leading '\0' stops the reader)
                ep++;
            }
        }
    }
    // Blue paint (§6.10.3.4): mark this macro active and push a region-end marker
    // first (so it becomes the last token of the pushed body).  Any recurrence of
    // the name while the region is open is left un-expanded; the marker un-paints
    // the macro when the scanner reaches it.  The per-macro-appears-once invariant
    // bounds paint_top by SYMSIZ, so the stack cannot overflow.
    cpp.paint_stack[cpp.paint_top++] = sp;
    if (AT_BUF_START(p)) {
        cpp.out_ptr = cpp.tok_ptr = p;
        p                         = spill_buffer(p);
    }
    *--p = (char)PAINT_END_MARK;
    // Push the body onto the front of the input, back to front, so it will be
    // rescanned.  A WARN_MARK means "insert actual argument N here" instead.
    for (;;) { // push definition onto front of input stack
        while (!ISWARN(*--vp)) {
            if (AT_BUF_START(p)) {
                cpp.out_ptr = cpp.tok_ptr = p;
                p                         = spill_buffer(p);
            }
            *--p = *vp;
        }
        if ((unsigned char)*vp == WARN_MARK) { // insert the expanded actual param
            ca = expanded[*--vp - 1];
            while (*--ca) {
                if (AT_BUF_START(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *ca;
            }
        } else if ((unsigned char)*vp == STRINGIZE_MARK) { // '#param': push the quoted raw actual
            char strbuf[2 * BUFSIZ + 4];
            const char *a1 = actual[*--vp - 1];
            const char *a0 = a1;
            const char *ce;
            while (a0[-1] != '\0') // walk back to the start of this actual's text
                --a0;
            ce = strbuf + stringize(a0, a1, strbuf);
            while (ce > strbuf) { // push the built string back-to-front
                if (AT_BUF_START(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *--ce;
            }
        } else if ((unsigned char)*vp == PASTE_MARK) { // '##' operand: push the RAW actual, unexpanded
            const char *a1 = actual[*--vp - 1];
            const char *a0 = a1;
            while (a0[-1] != '\0') // walk back to the start of this actual's text
                --a0;
            while (a1 > a0) { // push it back-to-front, adjacent to its neighbor
                if (AT_BUF_START(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *--a1;
            }
        } else if ((unsigned char)*vp == COMMA_PASTE_MARK) { // GNU ", ## __VA_ARGS__": raw actual,
            const char *a1 = actual[*--vp - 1]; // with the comma dropped when empty
            const char *a0 = a1;
            while (a0[-1] != '\0') // walk back to the start of this actual's text
                --a0;
            if (a1 > a0) { // non-empty: push the actual, then re-emit the comma
                while (a1 > a0) {
                    if (AT_BUF_START(p)) {
                        cpp.out_ptr = cpp.tok_ptr = p;
                        p                         = spill_buffer(p);
                    }
                    *--p = *--a1;
                }
                if (AT_BUF_START(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = ',';
            }
            // empty: push nothing -- the comma is elided.
        } else
            break;
    }
    cpp.out_ptr = cpp.tok_ptr = p;
    return (p);
}
