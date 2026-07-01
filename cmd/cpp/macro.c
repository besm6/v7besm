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
