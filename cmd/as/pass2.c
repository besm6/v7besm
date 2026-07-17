//
// Assembler for BESM-6.
// Between-pass bookkeeping plus the whole of pass 2: now that the segments are
// complete, assign their final addresses, relocate every reference, and write
// out the object file (header, const/text/data images, relocation records and
// the symbol table).
//
#include <stdio.h>

#include "as.h"

//
// Run between the two passes.  Pad the text/data/string segments to whole
// words, then walk the symbol table to compute its on-disk size (stlength) and
// its trailing alignment (stalign).  An undefined name becomes external unless
// -u was given (then it is an error).  When -x/-X drop local symbols, the kept
// symbols are renumbered through newindex[] so relocation records can be
// rewritten to match.  Each kept symbol occupies 2 header bytes + a value
// (W/2 bytes) + its name.
//
void finalize_symtab(void)
{
    int i, snum;

    align_segment(SCONST);
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
            newindex[i] = snum; // remember each symbol's new (compacted) index
        // Keep the symbol if we are not stripping, or it is external, or (-X)
        // it is not a local ".name".
        if (!as.xflags || (as.stab[i].n_type & N_EXT) ||
            (as.Xflag && as.stab[i].n_name[0] != '.')) {
            as.stlength += 2 + W / 2 + as.stab[i].n_len;
            snum++;
        }
    }
    as.stalign = W - as.stlength % W; // pad the symbol table out to a whole word
    as.stlength += as.stalign;
}

//
// Write the a.out header.  The five segment-size fields are byte counts (count[]
// is in half-words, so multiply by W/2).  The string segment is folded onto
// data.  a_entry is the word index of the first text word: it sits right after
// the header and the const segment.
//
void write_header(void)
{
    struct exec hdr;

    // A "#expr" reference reaches its word through a 12-bit short address field,
    // so the whole const segment has to fit below CONSTTOP.  The linker checks
    // the merged segment again; this catches a single object early.
    if (as.count[SCONST] && HDRSZ / W + as.count[SCONST] / 2 - 1 > CONSTTOP)
        fatal("const segment too large: last word at 0%lo, limit 0%o",
              HDRSZ / W + as.count[SCONST] / 2 - 1, CONSTTOP);

    hdr.a_magic = FMAGIC;
    hdr.a_const = as.count[SCONST] * (W / 2);
    hdr.a_text  = as.count[STEXT] * (W / 2);
    hdr.a_data  = (as.count[SDATA] + as.count[SSTRNG]) * (W / 2);
    hdr.a_bss   = as.count[SBSS] * (W / 2);
    hdr.a_syms  = as.stlength;
    hdr.a_entry = HDRSZ / W + as.count[SCONST] / 2; // == as.tbase
    hdr.a_flag  = 0;
    fputhdr(&hdr, stdout);
}

//
// Add base address `a` into the address field of half-word `h`, then return the
// patched half-word.  The relocation modifier bits (hr & RSHORT) say which
// field width to touch and how:
//   0      - full 15-bit address
//   RSHORT - 12-bit short address
//
static long relocate_field(long h, long a, int hr)
{
    switch (hr & RSHORT) {
    case RSHORT:
        a += short_addr_get(h);
        // A short address field reaches only [0..07777] and [070000..077777],
        // and this is the last place the assembler sees the address untruncated.
        // An address outside those ranges cannot be carried any further: the
        // field has no room for it, so masking it in would silently point the
        // instruction at an unrelated word and leave the linker nothing to
        // rescue.  Such code needs a long-address escape - "< sym >" or
        // "[ sym ]".  A "#expr" reference (RCONST) is exempt: it indexes the
        // const segment, which write_header already checks against CONSTTOP.
        // RABS never reaches relocate_field - pass 1 range-checks those itself,
        // so a signed stack offset such as `atx -5, 7` cannot trip this.
        if ((hr & REXT) != RCONST && !short_addr_fits(a))
            fatal("short address out of range: 0%lo", a);
        h = short_addr_put(h, a);
        break;
    case 0:
    default:
        a += h & 077777;
        h &= ~077777;
        h |= a & 077777;
        break;
    }
    return h;
}

//
// Relocate one half-word `h` given its relocation record `hr`: look at what the
// reference is relative to (the REXT bits) and add the matching segment base
// (or the symbol's value, for an external reference that is now defined).  An
// absolute reference, or one to a still-undefined/common symbol, is left
// unchanged for the linker to finish.
//
static long relocate_halfword(long h, long hr)
{
    int i;

    switch ((int)hr & REXT) {
    case RABS:
        break; // absolute: nothing to add
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
        // External symbol: if it is now defined here, fill in its value;
        // otherwise leave it for the linker.
        i = RGETIX(hr);
        if (as.stab[i].n_type != N_EXT + N_UNDF && as.stab[i].n_type != N_EXT + N_COMM)
            h = relocate_field(h, as.stab[i].n_value, (int)hr);
        break;
    }
    return h;
}

//
// Pass 2 proper.  First lay the segments end to end and record each one's base
// (word) address: header, then const, text, data, string, bss.  Then relocate
// and emit everything that has an image on disk:
//   * every symbol's value gets its segment base added (string symbols are
//     reclassified as data, since strings fold onto data);
//   * the const/text/data/string segment images are re-read from their temp
//     files, each half-word relocated through relocate_halfword(), and written
//     out.
//
void emit_segments(void)
{
    int i;
    long h;

    as.cbase  = HDRSZ / W;
    as.tbase  = as.cbase + as.count[SCONST] / 2;
    as.dbase  = as.tbase + as.count[STEXT] / 2;
    as.adbase = as.dbase + as.count[SDATA] / 2;
    as.bbase  = as.adbase + as.count[SSTRNG] / 2;

    // process the symbol table: turn each symbol's segment-relative value into
    // a final address by adding the segment base.
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
            as.stab[i].n_type += N_DATA - N_STRNG; // strings live in the data segment
            break;
        case N_BSS:
            h = relocate_field(h, as.bbase, 0);
            break;
        }
        as.stab[i].n_value = h;
    }
    // re-read each segment that has an image from its temp file and emit it,
    // relocating every half-word as it goes.
    for (as.segm = SCONST; as.segm < SBSS; as.segm++) {
        rewind(as.sfile[as.segm]);
        rewind(as.rfile[as.segm]);
        h = as.count[as.segm];
        while (h--)
            fputh(relocate_halfword(fgeth(as.sfile[as.segm]), fgeth(as.rfile[as.segm])), stdout);
    }
}

//
// Map a symbol type (which segment it belongs to) to the relocation type a
// reference to it should carry.  Used when an external reference turns out to
// be defined locally, so it becomes a plain segment-relative relocation.
//
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
    case N_FN:
    default:
        return 0;
    }
}

//
// Transform one relocation record for the output relocation stream.  Two
// fix-ups happen here:
//   * RSTRNG references become RDATA, because the string segment was folded
//     onto the data segment;
//   * an external reference that turned out to be defined locally becomes a
//     plain segment-relative relocation; one still undefined keeps its REXT
//     form but, when -x/-X compacted the symbol table, is renumbered through
//     newindex[] to the symbol's new position.
//
static long rewrite_reloc(long hr)
{
    int i;

    switch ((int)hr & REXT) {
    case RSTRNG:
        hr = RDATA | (hr & RSHORT);
        break;
    case REXT:
        i = RGETIX(hr);
        if (as.stab[i].n_type == N_EXT + N_UNDF || as.stab[i].n_type == N_EXT + N_COMM) {
            // still external: renumber the symbol index if symbols were dropped
            if (as.xflags)
                hr = (hr & (RSHORT | REXT)) | RPUTIX(newindex[i]);
        } else
            // now defined locally: become a segment-relative relocation
            hr = (hr & RSHORT) | type_to_reloc((int)as.stab[i].n_type);
        break;
    }
    return hr;
}

//
// Write the relocation records, one per emitted half-word, in the same order as
// the segment images: const, then text/data/string, each re-read from its
// relocation temp file.
//
void write_reloc(void)
{
    for (as.segm = SCONST; as.segm < SBSS; as.segm++) {
        long len = as.count[as.segm];

        rewind(as.rfile[as.segm]);
        while (len--)
            fputh(rewrite_reloc(fgeth(as.rfile[as.segm])), stdout);
    }
}

//
// Write the symbol table, then the alignment padding computed in
// finalize_symtab().  Only the symbols kept under -x/-X are emitted (the same
// keep test as there).
//
void write_symtab(void)
{
    int i;

    for (i = 0; i < as.stabfree; i++)
        if (!as.xflags || (as.stab[i].n_type & N_EXT) || (as.Xflag && as.stab[i].n_name[0] != '.'))
            fputsym(&as.stab[i], stdout);
    while (as.stalign--)
        putchar(0);
}
