//
// BESM-6 a.out object / executable file format.
//
// This is the on-disk layout shared by the whole toolchain: the assembler
// (cmd/as) emits it, the linker and binutils (cmd/ld, plus ar/nm/size/...)
// read and write it, and the disassembler (cmd/disasm) reads it.
//
// A file consists of a fixed header, the segment images, optional
// relocation records, the symbol table and the string table.
//
// Because the BESM-6 is a 48-bit word machine, all sizes and offsets here
// are counted in bytes but are always a multiple of 6 (one word == 6
// bytes; see W in cmd/ld/ld.c). On disk every multi-byte quantity is
// stored big-endian (most significant byte first).
//
// Header (8 logical words):
//                      a_magic  magic number ("BESM" + 0407 / 0410)
//                      a_const  size of the const segment  )
//                      a_text   size of the text segment   ) in bytes,
//                      a_data   size of the data segment   ) multiple
//                      a_bss    size of the bss segment    ) of 6
//                      a_syms   size of the symbol table   )
//                      a_entry  entry-point address (word index)
//                      a_flag   flags (relocatable)
//
// The struct below has only 8 meaningful fields, and the header occupies
// HDRSZ == 48 bytes == 8 words. Each field is stored as 3 zero padding
// bytes followed by a 3-byte big-endian value, so that every field starts
// on a 6-byte (one-word) boundary and the segment data that follows begins
// at a clean word offset. See fgethdr()/fputhdr() in cmd/ld/ for the exact
// encoding.
//
// File layout (byte offsets), where the relocation sections are present
// only when the file is still relocatable, i.e. RELFLG is clear (see the
// RELFLG note below):
//
// header:              0
// const:               48
// text:                48 + constsize
// data:                48 + constsize + textsize
// const relocation:    48 + constsize + textsize + datasize
// text relocation:     48 + 2*constsize + textsize + datasize
// data relocation:     48 + 2*constsize + 2*textsize + datasize
// symbol table:        48 + 2*constsize + 2*textsize + 2*datasize
//
#include "besm6/types.h"

//
// a.out file header.
//
// a_magic distinguishes the file kind (see FMAGIC/NMAGIC).
// The four segment-size fields and a_syms are byte counts, each a multiple of 6.
// a_entry is the program entry point as a word address.
// a_flag carries the RELFLG bit.
//
struct exec {
    word_t a_magic; // magic number
    word_t a_const; // const segment size, bytes (multiple of 6)
    word_t a_text;  // text (code) segment size, bytes
    word_t a_data;  // initialized data segment size, bytes
    word_t a_bss;   // uninitialized data (bss) size, bytes
    word_t a_syms;  // symbol table size, bytes
    word_t a_entry; // entry point, word address
    word_t a_flag;  // flags: RELFLG
};

#define HDRSZ 48 // header size in bytes (8 words of 6 bytes)

//
// Symbol table entry.
//
// On disk a symbol is stored as: one byte name length, one byte type,
// a 3-byte big-endian value, then n_len name bytes (no trailing NUL).
// In memory n_name points at a separately allocated NUL-terminated copy.
// See fgetsym()/fputsym() in cmd/ld/.
//
struct nlist {
    word_t n_len;   // length of the name in bytes
    word_t n_type;  // symbol type (N_* values, with the N_EXT bit)
    word_t n_value; // symbol value (address or constant)
    char *n_name;   // pointer to the name
};

//
// Header flags (a_flag).
//
// Set: file is fully linked / non-relocatable, i.e. it has no relocation records.
// Clear: relocation records are still present (despite the flag's name).
//
#define RELFLG 1

//
// Magic numbers (a_magic). Only FMAGIC and NMAGIC are accepted on input
// (see BADMAG below).
//
#define FMAGIC 02044252323200407 // standard relocatable / impure executable
#define NMAGIC 02044252323200410 // read-only (pure) text segment

//
// Symbol types (n_type).
//
// The low five bits (N_TYPE) hold the type; N_EXT marks an external (global)
// symbol. The type names the segment the symbol belongs to.
//
#define N_EXT   040 // external (global) symbol bit
#define N_TYPE  037 // mask selecting the type bits
#define N_FN    037 // file name symbol (debug)
#define N_UNDF  00  // undefined / unresolved external reference
#define N_ABS   01  // absolute (address-independent) value
#define N_CONST 02  // const segment
#define N_TEXT  03  // text (code) segment
#define N_DATA  04  // initialized data segment
#define N_BSS   05  // uninitialized data (bss) segment
#define N_STRNG 07  // string constant (used by as)
#define N_COMM  010 // common block (becomes bss when linked)

//
// Relocation types.
//
// A relocation record's low bits name what the patched reference resolves
// against: RABS for an absolute value, RCONST/RTEXT/RDATA/RBSS for a
// segment-relative reference, or REXT for an external symbol. For REXT the
// symbol's index is packed into the upper bits of the record (see
// RGETIX/RPUTIX). REXT also doubles as a bit mask.
//
#define RABS   0   // absolute - no relocation
#define RCONST 010 // relative to the const segment
#define RTEXT  020 // relative to the text segment
#define RDATA  030 // relative to the data segment
#define RBSS   040 // relative to the bss segment
#define RSTRNG 060 // string constant (used by as)
#define REXT   070 // external symbol reference; also a bit mask

//
// Address-field modifiers, stored in the low bits alongside the relocation
// type. They tell the linker which width of the instruction's address field to
// patch (see relocate_halfword() in cmd/ld/reloc.c).
//
#define RSHORT 01 // short address field; also a bit mask

//
// The short (Format 1) address field. It is a 12-bit offset in bits 12-1 plus
// the segment bit S in bit 19, which contributes 070000 to the effective
// address:
//
//      EA = (M[reg] + offset + S*070000 + C) mod 0100000
//
// So the field addresses exactly [0..07777] and [070000..077777] -- equivalently
// a 13-bit signed value in [-010000, +07777]. The upper eighth is where the
// kernel's u-area (076000) and the user stack base (070000) live; the middle of
// the address space (010000..067777) is unreachable and needs a "< sym >" escape
// (a utc ahead of the instruction). See doc/Besm6_Instruction_Set.md.
//
// Short opcodes occupy bits 18-13 (opcode << 12, at most 0770000), so they never
// collide with the segment bit. In a long (Format 2) instruction bit 19 belongs
// to the opcode instead -- only a field carrying RSHORT may be run through the
// short_addr_* helpers below.
//
#define SHORTSEGBIT 01000000L // bit 19 (S): adds SHORTSEG to the effective address
#define SHORTSEG    070000L   // what the segment bit contributes
#define SHORTOFF    07777L    // the 12-bit offset field

//
// Highest word address the const segment may occupy. The "#expr" operator
// reaches its word through a short address field, and the const segment starts
// at word 8 and grows up, so it lives at the bottom of memory where the segment
// bit cannot help it: no const word may sit above 07777. Both the assembler and
// the linker refuse to lay one out past this line.
//
#define CONSTTOP 07777 // last word address available to the const segment

//
// Const-segment word marker, valid only on the *high-half* relocation record of
// a word in the const segment. It says the word is an anonymous literal, put
// there by the assembler's "#expr" operator, so the linker may merge it with an
// identical word or drop it (load_constants() in cmd/ld/pass1.c). Words placed
// in the segment by a ".const" directive never carry it: they hold ordered data
// or code, and moving one relative to its neighbours would corrupt the program.
//
// A const word's high half never needs a relocation of its own (the address
// field of a 48-bit value lives in the low half), so the record is free to
// carry this flag. Bit 04 is still unused. Note that rewrite_reloc() in
// cmd/as/pass2.c and relocate_halfword() in cmd/ld/reloc.c preserve unknown
// bits on RABS and segment-relative records but strip them when rewriting an
// REXT or RSTRNG record -- a path no const word's high half ever takes.
//
#define RMERGE 02 // const word: anonymous literal, the linker may merge it

#define RGETIX(h) ((h) >> 6)       // extract symbol index from a record
#define RPUTIX(h) ((long)(h) << 6) // pack symbol index into a record

//
// Macros computing positions within the file from the header.
//
#define N_TXTOFF(x) HDRSZ // offset of the segment images
#define N_SYMOFF(x) \
    (N_TXTOFF(x) + (x).a_const + (x).a_text + (x).a_data) // offset of the symbol table
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)            // offset of the string table
#define N_BADMAG(x) ((x).a_magic != FMAGIC && (x).a_magic != NMAGIC)

#ifndef KERNEL
#include <stdio.h>

//
// Big-endian I/O helpers for the format above (implemented in cmd/libaout):
// fgeth/fputh read and write one 3-byte half-word (24 bits); fgetw/fputw one
// full 6-byte word (48 bits); fgethdr/fputhdr a whole header; fgetsym/fputsym
// one symbol table entry; fgetint an int stored as a full 6-byte word (value in
// the low half-word).
//
long fgeth(FILE *f);
void fputh(long h, FILE *f);
uword_t fgetw(FILE *f);
void fputw(uword_t w, FILE *f);
int fgethdr(FILE *f, struct exec *h);
void fputhdr(const struct exec *h, FILE *f);
int fgetsym(FILE *text, struct nlist *sym);
void fputsym(const struct nlist *s, FILE *f);
int fgetint(FILE *f, int *i);

//
// The short address field codec (implemented in cmd/libaout/shortaddr.c), shared
// by the assembler and the linker so both agree on what a Format 1 instruction
// can reach. Only apply these to a field the relocation record marks RSHORT.
//
// short_addr_get  -- decode the 15-bit effective address held in `insn`
// short_addr_fits -- true if `a` is representable; `a` is a plain 15-bit address,
//                    already reduced modulo 0100000 by its caller
// short_addr_put  -- put `a` back into `insn`, setting the segment bit as needed
//
long short_addr_get(long insn);
int short_addr_fits(long a);
long short_addr_put(long insn, long a);

//
// File-descriptor counterparts used by ar/ranlib for in-place archive I/O.
// They use the same 6-byte (two half-word) on-disk layout as fgetint().
//
int getint(int f, uword_t *i);
int putint(int f, uword_t i);
#endif
