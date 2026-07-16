//
// Linker for BESM-6 a.out objects.
// Relocation: fix up the address fields of instructions and constants, both
// for absolute relocation and for external symbol references.
//
#include "intern.h"

//
// Translate a symbol's type (which segment it lives in) into the matching
// relocation-type code stored in a relocation record.  Used when a reference
// that was "to an external symbol" becomes "relative to that symbol's segment"
// once the symbol is known.
//
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
    case N_STRNG:
        return RDATA;
    case N_COMM:
        return RBSS;
    case N_FN:
        return 0;
    default:
        return 0;
    }
}

//
// The workhorse of relocation.  It patches one 24-bit half-word `t` (a machine
// instruction or a constant) using its relocation record `r`, returning the fixed
// half-word in *pt and the (possibly rewritten) record in *pr.
//
// It works in three steps:
//   1. Pull the address field out of `t`.  The record's low bits say how wide it
//      is - full word, 15-bit long, or 12-bit short.
//   2. Work out `ad`, the amount to add.  The record's REXT bits say what the
//      field refers to: a particular segment (add that segment's base), or an
//      external symbol (REXT).  For an external symbol we look it up; if it is
//      still undefined we just renumber the reference for the output, otherwise
//      we add the symbol's now-known address and record its segment.
//   3. Put (a + ad) back into the same field width.
//
void relocate_halfword(const struct local *lp, long t, long r, long *pt, long *pr)
{
    long a, ad;
    int i;
    const struct nlist *sp;
    const char *name = NULL; // the referenced symbol, when the field names one

    if (ld.trace > 2)
        printf("%08lx %08lx", t, r);

    // Step 1: extract the current address field from the instruction.
    switch ((int)r & RSHORT) {
    case RSHORT:
        a = t & 07777; // short address - 12 bits
        break;
    case 0:
    default:
        a = t & 077777; // full 15-bit address
        break;
    }

    // Step 2: decide how much to add (ad), based on what the field points at.
    ad = 0;
    switch ((int)r & REXT) {
    case RCONST:
        // The constant pool was de-duplicated; redirect to the pooled slot.
        i  = ld.newindex[a - HDRSZ / W + ld.cindex];
        ad = ld.cbasaddr + i - a;
        break;
    case RTEXT:
        ad = ld.ctrel; // add the text segment base
        break;
    case RDATA:
        ad = ld.cdrel; // add the data segment base
        break;
    case RBSS:
        ad = ld.cbrel; // add the bss segment base
        break;
    case REXT:
        // A reference to an external symbol, named by an index packed into the
        // record.  Map that index to the global symbol it stands for.
        sp = lookup_local(lp, (int)RGETIX(r));
        r &= RSHORT;
        if (sp->n_type == N_EXT + N_UNDF || sp->n_type == N_EXT + N_COMM) {
            // Still undefined: keep it external in the output, but renumber it to
            // its slot in the final global symbol table.
            r |= REXT | RPUTIX(ld.nsym + (sp - ld.symtab));
            break;
        }
        // Resolved: bake in the symbol's address and tag the record with the
        // segment the symbol ended up in.
        r |= reloc_type(sp->n_type);
        ad   = sp->n_value;
        name = sp->n_name;
        break;
    }

    // Step 3: write (a + ad) back into the same address field of `t`.
    switch ((int)r & RSHORT) {
    case RSHORT:
        // A short address field is only 12 bits.  Truncating a relocated
        // reference that no longer fits would leave the instruction quietly
        // reading and writing an unrelated low address, so treat it as an error:
        // such code needs a long-address escape ("< sym >" / "[ sym ]", i.e. a
        // utc/wtc ahead of the instruction).  Only relocated fields reach this
        // check with a nonzero `ad` - an RABS field falls through step 2 with
        // ad = 0, so a negative stack offset such as `atx -5(7)` (which encodes
        // as 07773 plus a sign flag) cannot false-positive.  Under -r the
        // addresses are not final yet, so there is nothing to judge.
        if (!ld.rflag && ((a + ad) & ~07777L)) {
            if (name)
                error(1, "short address out of range: %s=0%lo", name, a + ad);
            else
                error(1, "short address out of range: 0%lo", a + ad);
        }
        t &= ~07777;
        t |= (a + ad) & 07777;
        break;
    case 0:
    default:
        t &= ~077777;
        t |= (a + ad) & 077777;
        break;
    }

    if (ld.trace > 2)
        printf(" -> %08lx %08lx\n", t, r);

    *pt = t;
    *pr = r;
}

//
// Relocate this file's whole constant pool and write it to the const output
// buffer.  Each pooled constant is two half-words (h, h2) with their own
// relocation records (hr, hr2); both halves go through relocate_halfword.  When
// -r is in effect the updated records are written to the const relocation buffer.
//
void relocate_constants(const struct local *lp)
{
    long r, t;
    struct constab *p;
    const struct constab *c;

    p = &ld.constab[ld.nconst];
    c = p + ld.coptsize[ld.nfile];
    for (; p < c; p++) {
        relocate_halfword(lp, p->h, p->hr, &t, &r);
        fputh(t, ld.coutb);
        if (ld.rflag)
            fputh(r, ld.croutb);
        relocate_halfword(lp, p->h2, p->hr2, &t, &r);
        fputh(t, ld.coutb);
        if (ld.rflag)
            fputh(r, ld.croutb);
    }
}

void relocate_segment(const struct local *lp, FILE *b1, FILE *b2, long len)
{
    long r, t;

    len /= W / 2;
    while (len--) {
        t = fgeth(ld.text);
        r = fgeth(ld.reloc);
        relocate_halfword(lp, t, r, &t, &r);
        fputh(t, b1);
        if (ld.rflag)
            fputh(r, b2);
    }
}
