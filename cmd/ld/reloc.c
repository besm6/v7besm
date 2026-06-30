//
// Linker for BESM-6 a.out objects.
// Relocation: fix up the address fields of instructions and constants, both
// for absolute relocation and for external symbol references.
//
#include "intern.h"

int reloc_type(int stype)
{
    switch (stype & N_TYPE) {
    case N_UNDF:
        return 0;
    case N_ABS:
        return RABS;
    case N_CONST:
        return RCONST;
    case N_TEXT:
        return RTEXT;
    case N_DATA:
        return RDATA;
    case N_BSS:
        return RBSS;
    case N_ABSS:
        return RABSS;
    case N_STRNG:
        return RDATA;
    case N_COMM:
        return RBSS;
    case N_ACOMM:
        return RABSS;
    case N_FN:
        return 0;
    default:
        return 0;
    }
}

void relocate_halfword(const struct local *lp, long t, long r, long *pt, long *pr)
{
    long a, ad;
    int i;
    const struct nlist *sp;

    if (trace > 2)
        printf("%08lx %08lx", t, r);

    /* extract address from command */

    switch ((int)r & RSHORT) {
    case 0:
        a = t & 0777777777;
        break;
    case RLONG:
        a = t & 077777; /* long address - 15 bits */
        break;
    case RTRUNC:
        a = t & 07777; /* truncated short address - 12 bits */
        break;
    case RSHORT:
        a = t & 07777;
        break;
    case RSHIFT:
        a = t & 077777;
        a <<= 12;
        break;
    default:
        a = 0;
        break;
    }

    /* compute address shift `ad' */
    /* update relocation word */

    ad = 0;
    switch ((int)r & REXT) {
    case RCONST:
        i  = newindex[a - HDRSZ / W + cindex];
        ad = cbasaddr + i - a;
        break;
    case RTEXT:
        ad = ctrel;
        break;
    case RDATA:
        ad = cdrel;
        break;
    case RBSS:
        ad = cbrel;
        break;
    case RABSS:
        ad = carel;
        break;
    case REXT:
        sp = lookup_local(lp, (int)RGETIX(r));
        r &= RSHORT;
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM ||
            sp->n_type == N_EXT + N_ACOMM) {
            r |= REXT | RPUTIX(nsym + (sp - symtab));
            break;
        }
        r |= reloc_type(sp->n_type);
        ad = sp->n_value;
        break;
    }

    /* add updated address to command */

    switch ((int)r & RSHORT) {
    case 0:
        t &= ~0777777777;
        t |= (a + ad) & 0777777777;
        break;
    case RSHORT:
        t &= ~07777;
        t |= (a + ad) & 07777;
        break;
    case RLONG:
        t &= ~077777;
        t |= (a + ad) & 077777;
        break;
    case RSHIFT:
        t &= ~077777;
        t |= (a + ad) >> 12 & 077777;
        break;
    case RTRUNC:
        t &= ~07777;
        t |= (a + (ad & 07777)) & 07777;
        break;
    }

    if (trace > 2)
        printf(" -> %08lx %08lx\n", t, r);

    *pt = t;
    *pr = r;
}

void relocate_constants(const struct local *lp)
{
    long r, t;
    struct constab *p;
    const struct constab *c;

    p = &constab[nconst];
    c = p + coptsize[nfile];
    for (; p < c; p++) {
        relocate_halfword(lp, p->h, p->hr, &t, &r);
        fputh(t, coutb);
        if (rflag)
            fputh(r, croutb);
        relocate_halfword(lp, p->h2, p->hr2, &t, &r);
        fputh(t, coutb);
        if (rflag)
            fputh(r, croutb);
    }
}

void relocate_segment(const struct local *lp, FILE *b1, FILE *b2, long len)
{
    long r, t;

    len /= W / 2;
    while (len--) {
        t = fgeth(text);
        r = fgeth(reloc);
        relocate_halfword(lp, t, r, &t, &r);
        fputh(t, b1);
        if (rflag)
            fputh(r, b2);
    }
}
