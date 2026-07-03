/*
 *      BESM-6 a.out object / executable file format.
 *
 *      This is the on-disk layout shared by the whole toolchain: the assembler
 *      (cmd/as) emits it, the linker and binutils (cmd/ld, plus ar/nm/size/...)
 *      read and write it, and the disassembler (cmd/disasm) reads it.
 *
 *      A file consists of a fixed header, the segment images, optional
 *      relocation records, the symbol table and the string table.
 *
 *      Because the BESM-6 is a 48-bit word machine, all sizes and offsets here
 *      are counted in bytes but are always a multiple of 6 (one word == 6
 *      bytes; see W in cmd/ld/ld.c). On disk every multi-byte quantity is
 *      stored big-endian (most significant byte first).
 *
 *      Header (9 logical words):
 *                              a_magic  magic number ("BESM" + 0407 / 0410)
 *                              a_const  size of the const segment   )
 *                              a_text   size of the text segment    )
 *                              a_data   size of the data segment    ) in bytes,
 *                              a_bss    size of the bss segment      ) multiple
 *                              a_abss   size of the abss segment    ) of 6
 *                              a_syms   size of the symbol table    )
 *                              a_entry  entry-point address (word index)
 *                              a_flag   flags (relocatable / const-in-data)
 *
 *      The struct below has only 9 meaningful fields, yet the header occupies
 *      HDRSZ == 54 bytes == 9 words. Each field is stored as 3 zero padding
 *      bytes followed by a 3-byte big-endian value, so that every field starts
 *      on a 6-byte (one-word) boundary and the segment data that follows begins
 *      at a clean word offset. See fgethdr()/fputhdr() in cmd/ld/ for the exact
 *      encoding.
 *
 *      File layout (byte offsets), where the relocation sections are present
 *      only when the file is still relocatable, i.e. RELFLG is clear (see the
 *      RELFLG note below):
 *
 *      header:                 0
 *      const:                  54
 *      text:                   54 + constsize
 *      data:                   54 + constsize + textsize
 *      const relocation:       54 + constsize + textsize + datasize
 *      text relocation:        54 + 2*constsize + textsize + datasize
 *      data relocation:        54 + 2*constsize + 2*textsize + datasize
 *      symbol table:           54 + 2*constsize + 2*textsize + 2*datasize
 *
 */
#include "besm6/arch.h"

/*
 * a.out file header.
 *
 * a_magic distinguishes the file kind (see FMAGIC/NMAGIC).
 * The five segment-size fields and a_syms are byte counts, each a multiple of 6.
 * a_entry is the program entry point as a word address.
 * a_flag carries the RELFLG / TCDFLG bits.
 */
struct exec {
    word_t  a_magic;            /* magic number */
    word_t  a_const;            /* const segment size, bytes (multiple of 6) */
    word_t  a_text;             /* text (code) segment size, bytes */
    word_t  a_data;             /* initialized data segment size, bytes */
    word_t  a_bss;              /* uninitialized data (bss) size, bytes */
    word_t  a_abss;             /* absolute bss size, bytes */
    word_t  a_syms;             /* symbol table size, bytes */
    word_t  a_entry;            /* entry point, word address */
    word_t  a_flag;             /* flags: RELFLG, TCDFLG */
};

/*
 * Symbol table entry.
 *
 * On disk a symbol is stored as: one byte name length, one byte type,
 * a 3-byte big-endian value, then n_len name bytes (no trailing NUL).
 * In memory n_name points at a separately allocated NUL-terminated copy.
 * See fgetsym()/fputsym() in cmd/ld/.
 */
struct nlist {
    word_t  n_len;      /* length of the name in bytes */
    word_t  n_type;     /* symbol type (N_* values, with the N_EXT bit) */
    word_t  n_value;    /* symbol value (address or constant) */
    char *  n_name;     /* pointer to the name */
};

/*
 * Header flags (a_flag).
 */
#define RELFLG  1       /* set: file is fully linked / non-relocatable, i.e. it
                         * has no relocation records; clear: relocation records
                         * are still present (despite the flag's name) */
#define TCDFLG  2       /* const segment is folded into the data segment */

#define HDRSZ   54      /* header size in bytes (9 words of 6 bytes) */

/*
 * Magic numbers (a_magic). Only FMAGIC and NMAGIC are accepted on input
 * (see BADMAG below).
 */
#define FMAGIC  02044252323200407   /* standard relocatable / impure executable */
#define NMAGIC  02044252323200410   /* read-only (pure) text segment */

/*
 * Symbol types (n_type).
 *
 * The low five bits (N_TYPE) hold the type; N_EXT marks an external (global)
 * symbol. The type names the segment the symbol belongs to.
 */
#define N_EXT   040     /* external (global) symbol bit */
#define N_TYPE  037     /* mask selecting the type bits */
#define N_FN    037     /* file name symbol (debug) */
#define N_UNDF  00      /* undefined / unresolved external reference */
#define N_ABS   01      /* absolute (address-independent) value */
#define N_CONST 02      /* const segment */
#define N_TEXT  03      /* text (code) segment */
#define N_DATA  04      /* initialized data segment */
#define N_BSS   05      /* uninitialized data (bss) segment */
#define N_ABSS  06      /* absolute bss segment */
#define N_STRNG 07      /* string constant (used by as) */
#define N_COMM  010     /* common block (becomes bss when linked) */
#define N_ACOMM 011     /* absolute common (becomes abss when linked) */

/*
 * Relocation types.
 *
 * A relocation record's low bits name what the patched reference resolves
 * against: RABS for an absolute value, RCONST/RTEXT/RDATA/RBSS/RABSS for a
 * segment-relative reference, or REXT for an external symbol. For REXT the
 * symbol's index is packed into the upper bits of the record (see
 * RGETIX/RPUTIX). REXT also doubles as a bit mask.
 */
#define RABS    0       /* absolute - no relocation */
#define RCONST  010     /* relative to the const segment */
#define RTEXT   020     /* relative to the text segment */
#define RDATA   030     /* relative to the data segment */
#define RBSS    040     /* relative to the bss segment */
#define RABSS   050     /* relative to the abss segment */
#define RSTRNG  060     /* string constant (used by as) */
#define REXT    070     /* external symbol reference; also a bit mask */

/*
 * Address-field modifiers, stored in the low bits alongside the relocation
 * type. They tell the linker which width of the instruction's address field to
 * patch (see relocate_halfword() in cmd/ld/reloc.c).
 */
#define RSHORT  01      /* short address field; also a bit mask */

#define RGETIX(h)   ((h)>>6)            /* extract symbol index from a record */
#define RPUTIX(h)   ((long)(h)<<6)      /* pack symbol index into a record */

/*
 * Macros computing positions within the file from the header.
 */
#define N_TXTOFF(x) HDRSZ                                                /* offset of the segment images */
#define N_SYMOFF(x) (N_TXTOFF(x) + (x).a_const + (x).a_text + (x).a_data)/* offset of the symbol table */
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)                          /* offset of the string table */

#define BADMAG(x)   ((x).a_magic != FMAGIC && (x).a_magic != NMAGIC)
#define N_BADMAG    BADMAG

#ifndef KERNEL
#include <stdio.h>

/*
 * Big-endian I/O helpers for the format above (implemented in cmd/libaout):
 * fgeth/fputh read and write one 3-byte half-word (24 bits); fgetw/fputw one
 * full 6-byte word (48 bits); fgethdr/fputhdr a whole header; fgetsym/fputsym
 * one symbol table entry; fgetint an int stored as a full 6-byte word (value in
 * the low half-word).
 */
long fgeth(FILE *f);
void fputh(long h, FILE *f);
uword_t fgetw(FILE *f);
void fputw(uword_t w, FILE *f);
int  fgethdr(FILE *f, struct exec *h);
void fputhdr(const struct exec *h, FILE *f);
int fgetsym(FILE *text, struct nlist *sym);
void fputsym(const struct nlist *s, FILE *f);
int fgetint(FILE *f, int *i);

/*
 * File-descriptor counterparts used by ar/ranlib for in-place archive I/O.
 * They use the same 6-byte (two half-word) on-disk layout as fgetint().
 */
int getint(int f, uword_t *i);
int putint(int f, uword_t i);
#endif
