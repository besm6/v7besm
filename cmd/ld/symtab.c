//
// Linker for BESM-6 a.out objects.
// Symbol table: hashing, lookup/insertion, symbol relocation, and the file
// symbols emitted into the output.
//
#include <stdlib.h>
#include <string.h>

#include "intern.h"

void symreloc(void)
{
    int i;

    switch (cursym.n_type) {
    case N_CONST:
    case N_EXT + N_CONST:
        i              = cindex + cursym.n_value - HDRSZ / W;
        cursym.n_value = newindex[i];
        return;

    case N_TEXT:
    case N_EXT + N_TEXT:
        cursym.n_value += ctrel;
        return;

    case N_DATA:
    case N_EXT + N_DATA:
        cursym.n_value += cdrel;
        return;

    case N_BSS:
    case N_EXT + N_BSS:
        cursym.n_value += cbrel;
        return;

    case N_ABSS:
    case N_EXT + N_ABSS:
        cursym.n_value += carel;
        return;

    case N_EXT + N_UNDF:
    case N_EXT + N_COMM:
    case N_EXT + N_ACOMM:
        return;
    }
    if (cursym.n_type & N_EXT)
        cursym.n_type = N_EXT + N_ABS;
}

int enter(struct nlist **hp)
{
    struct nlist *sp;

    if (!*hp) {
        if (symindex >= NSYM)
            error(2, "symbol table overflow");
        symhash[symindex] = hp;
        *hp = lastsym = sp = &symtab[symindex++];
        sp->n_len          = cursym.n_len;
        sp->n_name         = cursym.n_name;
        sp->n_type         = cursym.n_type;
        sp->n_value        = cursym.n_value;
        return 1;
    } else {
        lastsym = *hp;
        return 0;
    }
}

struct nlist **lookup(void)
{
    int i;
    char *cp;
    struct nlist **hp;

    i = 0;
    for (cp = cursym.n_name; *cp; i = (i << 1) + *cp++)
        ;
    for (hp = &hshtab[(i & 077777) % NSYM + 2]; *hp != 0;) {
        const char *cp1 = (*hp)->n_name;
        int clash       = 0;
        for (cp = cursym.n_name; *cp;)
            if (*cp++ != *cp1++) {
                clash = 1;
                break;
            }
        if (clash) {
            if (++hp >= &hshtab[NSYM + 2])
                hp = hshtab;
        } else
            break;
    }
    return hp;
}

struct nlist **slookup(char *s)
{
    cursym.n_len   = strlen(s) + 1;
    cursym.n_name  = s;
    cursym.n_type  = N_EXT + N_UNDF;
    cursym.n_value = 0;
    return lookup();
}

void ldrsym(struct nlist *sp, long val, int type)
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

struct nlist *lookloc(const struct local *lp, int sn)
{
    const struct local *clp;

    for (clp = local; clp < lp; clp++)
        if (clp->locindex == sn)
            return clp->locsymbol;
    if (trace) {
        fprintf(stderr, "*** %d ***\n", sn);
        for (clp = local; clp < lp; clp++)
            fprintf(stderr, "%ld, ", clp->locindex);
        fprintf(stderr, "\n");
    }
    error(2, "bad symbol reference");
    /* NOTREACHED */
    return 0;
}

int mkfsym(char *s, int wflag)
{
    char *p;

    if (sflag || xflag)
        return 0;
    for (p = s; *p;)
        if (*p++ == '/')
            s = p;
    if (!wflag)
        return p - s + 6;
    cursym.n_len  = p - s;
    cursym.n_name = malloc(cursym.n_len + 1);
    if (!cursym.n_name)
        error(2, "out of memory");
    for (p = cursym.n_name; *s; p++, s++)
        *p = *s;
    cursym.n_type  = N_FN;
    cursym.n_value = torigin;
    fputsym(&cursym, soutb);
    free(cursym.n_name);
    return cursym.n_len + 6;
}
