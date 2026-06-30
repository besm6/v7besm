//
// Linker for BESM-6 a.out objects.
// Symbol table: hashing, lookup/insertion, symbol relocation, and the file
// symbols emitted into the output.
//
#include <stdlib.h>
#include <string.h>

#include "intern.h"

void relocate_cursym(void)
{
    int i;

    switch (ld.cursym.n_type) {
    case N_CONST:
    case N_EXT + N_CONST:
        i                 = ld.cindex + ld.cursym.n_value - HDRSZ / W;
        ld.cursym.n_value = ld.newindex[i];
        return;

    case N_TEXT:
    case N_EXT + N_TEXT:
        ld.cursym.n_value += ld.ctrel;
        return;

    case N_DATA:
    case N_EXT + N_DATA:
        ld.cursym.n_value += ld.cdrel;
        return;

    case N_BSS:
    case N_EXT + N_BSS:
        ld.cursym.n_value += ld.cbrel;
        return;

    case N_ABSS:
    case N_EXT + N_ABSS:
        ld.cursym.n_value += ld.carel;
        return;

    case N_EXT + N_UNDF:
    case N_EXT + N_COMM:
    case N_EXT + N_ACOMM:
        return;
    }
    if (ld.cursym.n_type & N_EXT)
        ld.cursym.n_type = N_EXT + N_ABS;
}

int enter_symbol(struct nlist **hp)
{
    struct nlist *sp;

    if (!*hp) {
        if (ld.symindex >= NSYM)
            error(2, "symbol table overflow");
        ld.symhash[ld.symindex] = hp;
        *hp = ld.lastsym = sp = &ld.symtab[ld.symindex++];
        sp->n_len             = ld.cursym.n_len;
        sp->n_name            = ld.cursym.n_name;
        sp->n_type            = ld.cursym.n_type;
        sp->n_value           = ld.cursym.n_value;
        return 1;
    } else {
        ld.lastsym = *hp;
        return 0;
    }
}

struct nlist **lookup_symbol(void)
{
    int i;
    char *cp;
    struct nlist **hp;

    i = 0;
    for (cp = ld.cursym.n_name; *cp; i = (i << 1) + *cp++)
        ;
    for (hp = &ld.hshtab[(i & 077777) % NSYM + 2]; *hp != 0;) {
        const char *cp1 = (*hp)->n_name;
        int clash       = 0;
        for (cp = ld.cursym.n_name; *cp;)
            if (*cp++ != *cp1++) {
                clash = 1;
                break;
            }
        if (clash) {
            if (++hp >= &ld.hshtab[NSYM + 2])
                hp = ld.hshtab;
        } else
            break;
    }
    return hp;
}

struct nlist **lookup_name(char *s)
{
    ld.cursym.n_len   = strlen(s) + 1;
    ld.cursym.n_name  = s;
    ld.cursym.n_type  = N_EXT + N_UNDF;
    ld.cursym.n_value = 0;
    return lookup_symbol();
}

void define_symbol(struct nlist *sp, long val, int type)
{
    if (sp == 0)
        return;
    if (sp->n_type != N_EXT + N_UNDF) {
        printf("%s: ", sp->n_name);
        error(1, "name redefined");
        return;
    }
    sp->n_type  = type;
    sp->n_value = val;
}

struct nlist *lookup_local(const struct local *lp, int sn)
{
    const struct local *clp;

    for (clp = ld.local; clp < lp; clp++)
        if (clp->locindex == sn)
            return clp->locsymbol;
    if (ld.trace) {
        fprintf(stderr, "*** %d ***\n", sn);
        for (clp = ld.local; clp < lp; clp++)
            fprintf(stderr, "%ld, ", clp->locindex);
        fprintf(stderr, "\n");
    }
    error(2, "bad symbol reference");
    // NOTREACHED
    return 0;
}

int make_file_symbol(char *s, int wflag)
{
    char *p;

    if (ld.sflag || ld.xflag)
        return 0;
    for (p = s; *p;)
        if (*p++ == '/')
            s = p;
    if (!wflag)
        return p - s + 6;
    ld.cursym.n_len  = p - s;
    ld.cursym.n_name = malloc(ld.cursym.n_len + 1);
    if (!ld.cursym.n_name)
        error(2, "out of memory");
    for (p = ld.cursym.n_name; *s; p++, s++)
        *p = *s;
    ld.cursym.n_type  = N_FN;
    ld.cursym.n_value = ld.torigin;
    fputsym(&ld.cursym, ld.soutb);
    free(ld.cursym.n_name);
    return ld.cursym.n_len + 6;
}
