//
// Assembler for BESM-6.
// Intermediate processing, second pass and output generation.
//
#include <stdio.h>

#include "as.h"

void middle(void)
{
    short i, snum;

    align(STEXT);
    align(SDATA);
    align(SSTRNG);
    stlength = 0;
    for (snum = 0, i = 0; i < stabfree; i++) {
        // if uflag is not set,
        // an undefined name is treated as external
        if (stab[i].n_type == N_UNDF) {
            if (uflag)
                uerror("name undefined", stab[i].n_name);
            else
                stab[i].n_type |= N_EXT;
        }
        if (xflags)
            newindex[i] = snum;
        if (!xflags || (stab[i].n_type & N_EXT) || (Xflag && stab[i].n_name[0] != 'L')) {
            stlength += 2 + W / 2 + stab[i].n_len;
            snum++;
        }
    }
    stalign = W - stlength % W;
    stlength += stalign;
}

void makeheader(void)
{
    struct exec hdr;

    hdr.a_magic = FMAGIC;
    hdr.a_const = (long)nconst * W;
    hdr.a_text  = count[STEXT] * (W / 2);
    hdr.a_data  = (count[SDATA] + count[SSTRNG]) * (W / 2);
    hdr.a_bss   = count[SBSS] * (W / 2);
    hdr.a_abss  = 0;                       /* no absolute-bss segment in as */
    hdr.a_syms  = stlength;
    hdr.a_entry = HDRSZ / W + count[SCONST] / (W / 2);
    hdr.a_flag  = 0;
    fputhdr(&hdr, stdout);
}

static long adjust(long h, long a, int hr)
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

static long makehalf(long h, long hr)
{
    short i;

    switch ((int)hr & REXT) {
    case RABS:
        break;
    case RCONST:
        h = adjust(h, cbase, (int)hr);
        break;
    case RTEXT:
        h = adjust(h, tbase, (int)hr);
        break;
    case RDATA:
        h = adjust(h, dbase, (int)hr);
        break;
    case RSTRNG:
        h = adjust(h, adbase, (int)hr);
        break;
    case RBSS:
        h = adjust(h, bbase, (int)hr);
        break;
    case REXT:
        i = RGETIX(hr);
        if (stab[i].n_type != N_EXT + N_UNDF && stab[i].n_type != N_EXT + N_COMM &&
            stab[i].n_type != N_EXT + N_ACOMM)
            h = adjust(h, stab[i].n_value, (int)hr);
        break;
    }
    return h;
}

void pass2(void)
{
    short i;
    long h;

    cbase  = HDRSZ / W;
    tbase  = cbase + nconst;
    dbase  = tbase + count[STEXT] / 2;
    adbase = dbase + count[SDATA] / 2;
    bbase  = adbase + count[SSTRNG] / 2;

    // process the symbol table
    for (i = 0; i < stabfree; i++) {
        h = stab[i].n_value;
        switch (stab[i].n_type & N_TYPE) {
        case N_UNDF:
        case N_ABS:
            break;
        case N_CONST:
            h = adjust(h, cbase, 0);
            break;
        case N_TEXT:
            h = adjust(h, tbase, 0);
            break;
        case N_DATA:
            h = adjust(h, dbase, 0);
            break;
        case N_STRNG:
            h = adjust(h, adbase, 0);
            stab[i].n_type += N_DATA - N_STRNG;
            break;
        case N_BSS:
            h = adjust(h, bbase, 0);
            break;
        }
        stab[i].n_value = h;
    }
    // process the constant segment
    for (i = 0; i < nconst; i++) {
        fputh(makehalf(constab[i].h2, constab[i].hr2), stdout);
        fputh(constab[i].h, stdout);
    }
    for (segm = STEXT; segm < SBSS; segm++) {
        rewind(sfile[segm]);
        rewind(rfile[segm]);
        h = count[segm];
        while (h--)
            fputh(makehalf(fgeth(sfile[segm]), fgeth(rfile[segm])), stdout);
    }
}

// convert symbol type to relocation type
static int typerel(int t)
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

static long relhalf(long hr)
{
    short i;

    switch ((int)hr & REXT) {
    case RSTRNG:
        hr = RDATA | (hr & RSHORT);
        break;
    case REXT:
        i = RGETIX(hr);
        if (stab[i].n_type == N_EXT + N_UNDF || stab[i].n_type == N_EXT + N_COMM ||
            stab[i].n_type == N_EXT + N_ACOMM) {
            // reindexing
            if (xflags)
                hr = (hr & (RSHORT | REXT)) | RPUTIX(newindex[i]);
        } else
            hr = (hr & RSHORT) | typerel((int)stab[i].n_type);
        break;
    }
    return hr;
}

void makereloc(void)
{
    short i;

    for (i = 0; i < nconst; i++) {
        fputh(relhalf(constab[i].hr2), stdout);
        fputh(0L, stdout);
    }
    for (segm = STEXT; segm < SBSS; segm++) {
        long len = count[segm];

        rewind(rfile[segm]);
        while (len--)
            fputh(relhalf(fgeth(rfile[segm])), stdout);
    }
}

void makesymtab(void)
{
    short i;

    for (i = 0; i < stabfree; i++)
        if (!xflags || (stab[i].n_type & N_EXT) || (Xflag && stab[i].n_name[0] != 'L'))
            fputsym(&stab[i], stdout);
    while (stalign--)
        putchar(0);
}
