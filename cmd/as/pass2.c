//
// Assembler for BESM-6.
// Intermediate processing, second pass and output generation.
//
#include <stdio.h>

#include "as.h"

void finalize_symtab(void)
{
    int i, snum;

    align_segment(STEXT);
    align_segment(SDATA);
    align_segment(SSTRNG);
    as.stlength = 0;
    for (snum = 0, i = 0; i < as.stabfree; i++) {
        // if uflag is not set,
        // an undefined name is treated as external
        if (as.stab[i].n_type == N_UNDF) {
            if (as.uflag)
                fatal("name undefined", as.stab[i].n_name);
            else
                as.stab[i].n_type |= N_EXT;
        }
        if (as.xflags)
            newindex[i] = snum;
        if (!as.xflags || (as.stab[i].n_type & N_EXT) || (as.Xflag && as.stab[i].n_name[0] != 'L')) {
            as.stlength += 2 + W / 2 + as.stab[i].n_len;
            snum++;
        }
    }
    as.stalign = W - as.stlength % W;
    as.stlength += as.stalign;
}

void write_header(void)
{
    struct exec hdr;

    hdr.a_magic = FMAGIC;
    hdr.a_const = (long)as.nconst * W;
    hdr.a_text  = as.count[STEXT] * (W / 2);
    hdr.a_data  = (as.count[SDATA] + as.count[SSTRNG]) * (W / 2);
    hdr.a_bss   = as.count[SBSS] * (W / 2);
    hdr.a_abss  = 0; /* no absolute-bss segment in as */
    hdr.a_syms  = as.stlength;
    hdr.a_entry = HDRSZ / W + as.count[SCONST] / (W / 2);
    hdr.a_flag  = 0;
    fputhdr(&hdr, stdout);
}

static long relocate_field(long h, long a, int hr)
{
    switch (hr & RSHORT) {
    case 0:
        a += h & 0777777777;
        h &= ~0777777777;
        h |= a & 0777777777;
        break;
    case RSHORT:
        a += h & 07777;
        h &= ~07777;
        h |= a & 07777;
        break;
    case RSHIFT:
        a >>= 12;
        goto rlong;
    case RTRUNC:
        a &= 07777;
    case RLONG:
    rlong:
        a += h & 077777;
        h &= ~077777;
        h |= a & 077777;
        break;
    }
    return h;
}

static long relocate_halfword(long h, long hr)
{
    int i;

    switch ((int)hr & REXT) {
    case RABS:
        break;
    case RCONST:
        h = relocate_field(h, as.cbase, (int)hr);
        break;
    case RTEXT:
        h = relocate_field(h, as.tbase, (int)hr);
        break;
    case RDATA:
        h = relocate_field(h, as.dbase, (int)hr);
        break;
    case RSTRNG:
        h = relocate_field(h, as.adbase, (int)hr);
        break;
    case RBSS:
        h = relocate_field(h, as.bbase, (int)hr);
        break;
    case REXT:
        i = RGETIX(hr);
        if (as.stab[i].n_type != N_EXT + N_UNDF && as.stab[i].n_type != N_EXT + N_COMM &&
            as.stab[i].n_type != N_EXT + N_ACOMM)
            h = relocate_field(h, as.stab[i].n_value, (int)hr);
        break;
    }
    return h;
}

void emit_segments(void)
{
    int i;
    long h;

    as.cbase  = HDRSZ / W;
    as.tbase  = as.cbase + as.nconst;
    as.dbase  = as.tbase + as.count[STEXT] / 2;
    as.adbase = as.dbase + as.count[SDATA] / 2;
    as.bbase  = as.adbase + as.count[SSTRNG] / 2;

    // process the symbol table
    for (i = 0; i < as.stabfree; i++) {
        h = as.stab[i].n_value;
        switch (as.stab[i].n_type & N_TYPE) {
        case N_UNDF:
        case N_ABS:
            break;
        case N_CONST:
            h = relocate_field(h, as.cbase, 0);
            break;
        case N_TEXT:
            h = relocate_field(h, as.tbase, 0);
            break;
        case N_DATA:
            h = relocate_field(h, as.dbase, 0);
            break;
        case N_STRNG:
            h = relocate_field(h, as.adbase, 0);
            as.stab[i].n_type += N_DATA - N_STRNG;
            break;
        case N_BSS:
            h = relocate_field(h, as.bbase, 0);
            break;
        }
        as.stab[i].n_value = h;
    }
    // process the constant segment
    for (i = 0; i < as.nconst; i++) {
        fputh(relocate_halfword(as.constab[i].h2, as.constab[i].hr2), stdout);
        fputh(as.constab[i].h, stdout);
    }
    for (as.segm = STEXT; as.segm < SBSS; as.segm++) {
        rewind(as.sfile[as.segm]);
        rewind(as.rfile[as.segm]);
        h = as.count[as.segm];
        while (h--)
            fputh(relocate_halfword(fgeth(as.sfile[as.segm]), fgeth(as.rfile[as.segm])), stdout);
    }
}

// convert symbol type to relocation type
static int type_to_reloc(int t)
{
    switch (t & N_TYPE) {
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
    case N_UNDF:
    case N_COMM:
    case N_ACOMM:
    case N_FN:
    default:
        return 0;
    }
}

static long rewrite_reloc(long hr)
{
    int i;

    switch ((int)hr & REXT) {
    case RSTRNG:
        hr = RDATA | (hr & RSHORT);
        break;
    case REXT:
        i = RGETIX(hr);
        if (as.stab[i].n_type == N_EXT + N_UNDF || as.stab[i].n_type == N_EXT + N_COMM ||
            as.stab[i].n_type == N_EXT + N_ACOMM) {
            // reindexing
            if (as.xflags)
                hr = (hr & (RSHORT | REXT)) | RPUTIX(newindex[i]);
        } else
            hr = (hr & RSHORT) | type_to_reloc((int)as.stab[i].n_type);
        break;
    }
    return hr;
}

void write_reloc(void)
{
    int i;

    for (i = 0; i < as.nconst; i++) {
        fputh(rewrite_reloc(as.constab[i].hr2), stdout);
        fputh(0L, stdout);
    }
    for (as.segm = STEXT; as.segm < SBSS; as.segm++) {
        long len = as.count[as.segm];

        rewind(as.rfile[as.segm]);
        while (len--)
            fputh(rewrite_reloc(fgeth(as.rfile[as.segm])), stdout);
    }
}

void write_symtab(void)
{
    int i;

    for (i = 0; i < as.stabfree; i++)
        if (!as.xflags || (as.stab[i].n_type & N_EXT) || (as.Xflag && as.stab[i].n_name[0] != 'L'))
            fputsym(&as.stab[i], stdout);
    while (as.stalign--)
        putchar(0);
}
