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
        a = short_addr_get(t); // short address - 12-bit offset plus segment bit
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
        // The constant pool was de-duplicated; redirect to the pooled slot.  As in
        // relocate_cursym(), the map only covers the words this file contributed, so
        // a reference outside them cannot be followed.  `a' is a 15-bit field and so
        // never negative, but a - HDRSZ/W still can be, and it can run past the
        // end of the map.
        i = a - HDRSZ / W;
        if (i < 0 || i >= (int)(ld.filhdr.a_const / W))
            error(2, "const reference to 0%lo outside the file's const segment", a);
        else
            ad = ld.cbasaddr + newindex[i] - a;
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
            r |= REXT | RPUTIX(ld.nsym + (sp - symtab));
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
        // A short address field reaches [0..07777] and, through its segment bit,
        // [070000..077777] - the top eighth, where the u-area and the user stack
        // live.  Truncating a relocated reference that lands between the two
        // would leave the instruction quietly reading and writing an unrelated
        // address, so treat it as an error: such code needs a long-address
        // escape ("< sym >" / "[ sym ]", i.e. a utc/wtc ahead of the
        // instruction).  A field with nothing to relocate reaches this check
        // unchanged (`ad` = 0) and so re-encodes to itself, which is why a
        // negative stack offset such as `atx -5(7)` cannot false-positive: the
        // assembler already reduced it to 077773, an address in the top eighth.
        // Under -r the addresses are not final yet, so there is nothing to judge.
        if (!ld.rflag && !short_addr_fits(a + ad)) {
            if (name)
                error(1, "short address out of range: %s=0%lo", name, a + ad);
            else
                error(1, "short address out of range: 0%lo", a + ad);
        }
        t = short_addr_put(t, a + ad);
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
// Both loops below relocate a HALF-word at a time, as they must, but read and write
// a WHOLE word: CON_PACK() of two half-words is exactly what fgetw() returns and
// fputw() takes, and on an aligned stream that is one load and one store in place of
// six getc and six putc expansions (cmd/libaout/fastio.h).  Each output stream still
// receives its half-words in the same order, and so does the -t trace, since
// relocate_halfword() is still called on the high half first.
void relocate_constants(const struct local *lp)
{
    long rhi, thi, rlo, tlo;
    struct constab *p;
    const struct constab *c;

    p = &constab[ld.nconst];
    c = p + coptsize[ld.nfile];
    for (; p < c; p++) {
        relocate_halfword(lp, CON_HI(p->v), CON_HI(p->r), &thi, &rhi);
        relocate_halfword(lp, CON_LO(p->v), CON_LO(p->r), &tlo, &rlo);
        fputw(CON_PACK(thi, tlo), ld.coutb);
        if (ld.rflag)
            fputw(CON_PACK(rhi, rlo), ld.croutb);
    }
}

void relocate_segment(const struct local *lp, FILE *b1, FILE *b2, long len)
{
    long rhi, thi, rlo, tlo;

    len /= W; // whole words now, not half-words: every segment is word-aligned
    while (len--) {
        uword_t tw = fgetw(ld.text);
        uword_t rw = fgetw(ld.reloc);

        relocate_halfword(lp, CON_HI(tw), CON_HI(rw), &thi, &rhi);
        relocate_halfword(lp, CON_LO(tw), CON_LO(rw), &tlo, &rlo);
        fputw(CON_PACK(thi, tlo), b1);
        if (ld.rflag)
            fputw(CON_PACK(rhi, rlo), b2);
    }
}
