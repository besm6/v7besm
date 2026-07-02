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
// Handle a "#define" line: parse the macro name, the optional parameter list,
// and the replacement text, and store it in the symbol table.
//
// The stored body is not copied verbatim: wherever a parameter name appears it
// is replaced by a two-byte marker (a parameter number followed by WARN) so that
// expand_macro can later splice in the actual arguments without re-parsing.  A
// redefinition that differs from the previous one draws a warning; an identical
// one is silently accepted and its space reclaimed.
//
char *do_define(char *p)
{
    char *pin, *psav, *cf;
    char **pf, **qf;
    int b, c, params;
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
    // If the name is immediately followed by '(' this is a function-like macro:
    // collect the parameter names into formal[] / formtxt.
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
    body_start      = psav;
    // Copy the replacement text into the side buffer, token by token, replacing
    // each occurrence of a parameter name (even inside string/char literals)
    // with the marker "<param number><WARN>".
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
                    if (psav[-1] == warn_mark) // left operand is a parameter: keep it raw
                        psav[-1] = paste_mark;
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
                    *psav++ = stringize_mark;
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
                for (qf = pf; --qf >= formal;) {
                    if (formal_matches(*qf, pin, p)) {
                        *psav++ = qf - formal + 1;
                        // a '##' operand keeps its raw actual (paste_mark); a plain
                        // parameter substitutes its expanded actual (warn_mark)
                        *psav++ = paste_now ? paste_mark : WARN;
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
    if (hashpos != NULL) // '#' at end of a function-like body, no parameter follows
        pperror("'#' is not followed by a macro parameter");
    if (paste_pending) // '##' at end of a function-like body, no right operand follows
        pperror("'##' at end of macro replacement list");
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
static char *expand_text(const char *a0, const char *a1, char *out, int cap)
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
    set_fast_scan();
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
    // Function-like macro: scan the parenthesized actual arguments, recording a
    // pointer to each one in actual[].  Expansion is suppressed while we do this.
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
        if (*cpp.tok_ptr != '(') {
            // Function-like macro name not followed by '(': not an invocation.
            // Restore scan state and emit the name verbatim.  The name bytes
            // may be gone from the buffer (refill_buffer relocates it) and
            // there is no blue-paint yet, so we must not push the name back (it
            // would re-expand); write it straight to the output instead.
            cpp.paren_level = 0;
            --cpp.false_level;
            set_fast_scan();
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
    // Push the body onto the front of the input, back to front, so it will be
    // rescanned.  A WARN marker means "insert actual argument N here" instead.
    for (;;) { // push definition onto front of input stack
        while (!iswarn(*--vp)) {
            if (at_buf_start(p)) {
                cpp.out_ptr = cpp.tok_ptr = p;
                p                         = spill_buffer(p);
            }
            *--p = *vp;
        }
        if (*vp == warn_mark) { // insert the expanded actual param
            ca = expanded[*--vp - 1];
            while (*--ca) {
                if (at_buf_start(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *ca;
            }
        } else if (*vp == stringize_mark) { // '#param': push the quoted raw actual
            char strbuf[2 * BUFSIZ + 4];
            const char *a1 = actual[*--vp - 1];
            const char *a0 = a1;
            const char *ce;
            while (a0[-1] != '\0') // walk back to the start of this actual's text
                --a0;
            ce = strbuf + stringize(a0, a1, strbuf);
            while (ce > strbuf) { // push the built string back-to-front
                if (at_buf_start(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *--ce;
            }
        } else if (*vp == paste_mark) { // '##' operand: push the RAW actual, unexpanded
            const char *a1 = actual[*--vp - 1];
            const char *a0 = a1;
            while (a0[-1] != '\0') // walk back to the start of this actual's text
                --a0;
            while (a1 > a0) { // push it back-to-front, adjacent to its neighbor
                if (at_buf_start(p)) {
                    cpp.out_ptr = cpp.tok_ptr = p;
                    p                         = spill_buffer(p);
                }
                *--p = *--a1;
            }
        } else
            break;
    }
    cpp.out_ptr = cpp.tok_ptr = p;
    return (p);
}
