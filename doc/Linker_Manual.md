# BESM-6 Unix Linker Manual

This manual describes `b6ld`, the BESM-6 linker in `cmd/ld/`. A linker takes the object files
produced by the assembler (`b6as`), plus optional libraries, and stitches them into one
runnable program: it concatenates the matching segments of every input and then fixes up all
the addresses so that, once everything sits at its final location, every reference points where
it should. This document is both a tutorial (start at [§1](#1-overview)) and a complete
reference for the command line, the linking model, and the `a.out` object/executable format it
reads and writes. It does **not** re-document the assembler syntax, the instruction set, or the
ABI; for those, see:

- [Assembler_Manual.md](Assembler_Manual.md) — the assembly language `b6ld`'s inputs are
  written in, and the directives (`.comm`, `.strng`, …) that shape the object file.
- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — opcodes, registers, and the 24-bit
  instruction format whose address fields relocation patches.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the procedure-call ABI.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — how C scalars are laid out in
  a 48-bit word.

Everything below is derived from the linker sources themselves: `main.c` (command-line front
end), `ld.c` (engine framework, header reading, address assignment, error reporting), `pass1.c`
(first pass and option parsing), `pass2.c` (second pass), `symtab.c` (symbol table), `library.c`
(archives), `reloc.c` (relocation), `output.c` (output assembly), and the shared headers
`intern.h`/`ld.h`. The on-disk format is defined in
[`cross/besm6/b.out.h`](../cross/besm6/b.out.h), [`cross/besm6/ar.h`](../cross/besm6/ar.h) and
[`cross/besm6/ranlib.h`](../cross/besm6/ranlib.h); its byte-level serialization lives in
[`cmd/libaout/`](../cmd/libaout/).

> **Octal convention.** Octal is the native notation for BESM-6, and this manual writes
> addresses, sizes, type codes and masks in octal (a leading `0`, as in the C sources:
> `0100000`, `077777777`, `040`). Decimal is used only for human-scale counts (table
> capacities, byte lengths of fixed structures).

---

## 1. Overview

The BESM-6 is a **48-bit, word-addressed** machine. The addressable unit is one 48-bit word;
there are no sub-word load/store instructions. One word is **6 bytes** — the constant `W` that
pervades the linker — so every size and every file offset the linker deals with is a whole
multiple of 6. A machine instruction is a **24-bit half-word**, and two of them pack into one
word. An "address" in the linker is therefore a **word index**, not a byte pointer; to convert,
divide a byte offset by `W` to get a word address, or multiply by `W` to go back.

The whole machine address space is a flat **15 bits**: `0100000` words (32768 words). No single
segment may grow past that limit, and the linker checks for it as it accumulates sizes
(`add_size()` in [ld.c](../cmd/ld/ld.c)).

Every object file is divided into **four segments**, and the linker glues each kind end-to-end
across all the inputs (see [§3](#3-the-four-segments)):

| Segment | Holds | In the file? |
|---------|-------|--------------|
| const | read-only constants / the literal pool | yes |
| text | the program code | yes |
| data | pre-initialized variables | yes |
| bss | variables that start out zero | no — size only |

The linker does its work in **two passes** over the inputs, with a layout step in between:

1. **Pass 1** ([pass1.c](../cmd/ld/pass1.c)) reads every file just to measure the segments,
   merge the constant pool, and build the global symbol table. Along the way it parses the
   command line and decides which library members are needed.
2. **`assign_addresses()`** ([ld.c](../cmd/ld/ld.c)) lays the segments out in memory, reserves
   space for common symbols, and gives every symbol its final address.
3. **Pass 2** ([pass2.c](../cmd/ld/pass2.c)) reads every file again, copies each segment's bytes
   into the output while relocating every address field, and writes the header, segments and
   symbol table.

All engine state lives in a single structure, `struct linker ld` (declared in
[intern.h](../cmd/ld/intern.h) and defined in [ld.c](../cmd/ld/ld.c)); the engine has no other
globals. The public entry point is `ld_link(argc, argv)` ([ld.h](../cmd/ld/ld.h)), which the
thin `main()` and the unit tests both call.

---

## 2. Invoking the linker

```
b6ld [-xXsSrndtCk] [-lname] [-D num] [-T addr] [-u name] [-e name] [-o file] file...
```

With no arguments `b6ld` prints its usage line and exits with status 4. Arguments are processed
left to right; anything that is not an option is a file to link (an object file or an archive),
and files are combined in the order given. The default output name is `a.out` — the image is
actually built under the scratch name `l.out` and then linked to the final name at the end, so
that an interrupted or failed link never overwrites a good `a.out` (see
[§9](#9-output-modes-and-file-assembly)).

### 2.1 Options

Every flag is parsed in `pass1()` ([pass1.c](../cmd/ld/pass1.c)). Single letters may be bundled
(`-rs`); the four value-taking options `-o`/`-u`/`-e`/`-D` consume the **following** argument,
while `-T` and `-l` take the rest of the **same** argument.

| Option | Argument | Effect |
|--------|----------|--------|
| `-o file` | next arg | Set the output file name (default `a.out`). |
| `-u name` | next arg | *Use*: enter `name` as an undefined external, so it is treated as referenced. Forces a library member that defines it to be pulled in even if nothing else needs it. |
| `-e name` | next arg | *Entry*: like `-u`, and additionally make `name` the program entry point (`a_entry`). The symbol must resolve into the text segment, or it is an error. |
| `-D num` | next arg | Reserve a data segment of at least `num` words. Must not be smaller than the data already accumulated. Pass 2 emits the extra words as zeros. |
| `-T addr` | rest of arg | Set the base load address (`ld.basaddr`, default `BADDR`). Parsed with `strtoul`, so `0`-prefixed octal and `0x` hex are accepted. |
| `-L dir` | next arg or rest of arg | Append `dir` to the library search path used by `-l`. Accepts both `-L dir` and the glued `-Ldir`. May be given more than once; directories are searched in the order listed. The search path is empty by default. See [§7.3](#73-library-search). |
| `-l name` | rest of arg | Link the library `libname.a`, looked up in the `-L` search path. Bare `-l` means `-la`. See [§7](#7-archives-and-libraries). |
| `-x` | — | Discard all local (non-global) symbols from the output. |
| `-X` | — | Discard local symbols whose name begins with `.` (the `LOCSYM` character — compiler temporaries). |
| `-S` | — | Strip absolute and debug symbols (everything that is not a segment-relative local or a global). |
| `-r` | — | Retain relocation records and produce a relocatable output that can be linked again; do **not** define common symbols (unless `-d` is also given). |
| `-s` | — | Discard **all** symbols. Implies `-x`. |
| `-n` | — | Pure procedure: mark the text segment read-only (`NMAGIC`) and page-align the data segment. |
| `-d` | — | Define common symbols even under `-r`. |
| `-t` | — | Trace progress. Repeating it (`-t -t`, `-t -t -t`) raises the verbosity: file names, then per-segment biases and section markers, then every half-word before/after relocation. |

### 2.2 Interactions between options

Several flags override one another; the precedence is fixed in `assign_addresses()` and
`setup_output()`:

- **`-r` wins over layout and stripping flags.** If the output stays relocatable — either
  because `-r` was given, or because a genuine symbol is left undefined and the linker forces
  `-r` on — then `-n` and `-s` are cleared. A relocatable file keeps its
  const/text/data in the canonical order and keeps its symbols so it can be re-linked.
- **`-r` suppresses common definitions** unless `-d` is present; `-d` alone (no `-r`) is a
  no-op because commons are defined anyway.
- **`-s` implies `-x`** (both set `ld.xflag`).
- **`-n`** triggers 1024-word page alignment of the data segment and selects `NMAGIC`.

### 2.3 Exit status

The exit code is the highest error severity seen (`ld.errlev`; see
[§10](#10-diagnostics)). On a clean run it is 0 and the output is made executable
(`chmod 0777 & ~umask`). The no-argument usage message exits 4. A fatal error aborts
immediately and leaves no output.

---

## 3. The four segments

Each input object declares four segment sizes in its header (see [§8.2](#82-the-header-struct-exec)).
The linker concatenates all the inputs' segments of the same kind, in the order the files appear
on the command line:

- **const** — the read-only literal pool. Constants are **de-duplicated** across the whole
  program: an identical non-relocatable constant used by several files is stored once (see
  [§3.1](#31-the-constant-pool)). Const is written to the output file.
- **text** — machine code, 24-bit half-words two per word. Written to the file; made read-only
  by `-n`.
- **data** — initialized variables. Written to the file.
- **bss** — zero-initialized variables. Occupies **no space** in the file; only its total size
  is recorded, and it is the loader's job to clear it. Common symbols land here (see
  [§5.4](#54-common-symbols)).

The segments are stacked const → text → data → bss (with the common area
interleaved; see [§4.2](#42-memory-layout)).

### 3.1 The constant pool

`load_constants()` ([pass1.c](../cmd/ld/pass1.c)) reads each file's const segment one entry at a
time. Every constant is two half-words of value (`h`, `h2`) plus two half-words of relocation
(`hr`, `hr2`) — a `struct constab`. Only a **non-relocatable** constant (`hr == hr2 == 0`) can be
shared, because a relocatable one means something different in every file. For each such constant
the linker scans the pool built so far for an identical earlier copy and, if found, reuses it.

As it merges, it fills `ld.newindex[]`, mapping each of this file's constant slots to the pooled
slot it ended up in. Both symbol relocation (`relocate_cursym()`) and half-word relocation
(`relocate_halfword()`) consult that map to repoint every constant reference at its pooled copy.
The per-file post-dedup size is remembered in `ld.coptsize[]` so pass 2 can walk the same
constants again.

---

## 4. The linking model

### 4.1 The two passes

**Pass 1** (`pass1()` → `scan_file()` → `scan_object()`):

1. `read_header()` reads and validates the 8-word header (see
   [§8.2](#82-the-header-struct-exec)) and precomputes this file's three relocation *biases*
   (`ctrel`, `cdrel`, `cbrel`; see [§6.2](#62-relocation-biases)).
2. `load_constants()` merges the const pool.
3. The file's symbol table is read entry by entry into `ld.cursym`. Local symbols are counted
   (for the output symbol-table size) unless they are being discarded. External symbols are
   relocated (`relocate_cursym()`) and entered/merged into the global table (see
   [§5.3](#53-symbol-resolution)).
4. The file's segment sizes are folded into the running totals `csize`/`tsize`/`dsize`/`bsize`/
   `asize` — but for a **library member**, only if it actually defined a needed symbol; otherwise
   everything it added is rolled back and the member is skipped (see
   [§7](#7-archives-and-libraries)).

**`assign_addresses()`** runs once, between the passes:

1. Look up the five built-in boundary symbols `_econst`/`_etext`/`_edata`/`_ebss`/`_end`.
2. Decide whether the output stays relocatable: if any genuine symbol is still undefined (the
   five boundary symbols don't count), force `-r` on and clear `-d`.
3. Lay out common symbols: replace each common's requested size with the offset of the slot it
   is granted, growing the bss-common area (`cmsize`). Skipped under `-r` without `-d`.
4. Choose each segment's base address (its *origin*) and stack the segments (see
   [§4.2](#42-memory-layout)).
5. Walk every global symbol, turning its segment-relative value into a final address by adding
   that segment's origin; commons become ordinary bss symbols here. Report undefined
   symbols (unless `-r`) and flag any value that overflows the 24-bit value field
   (`n_value & ~077777777`), the width of a half-word field.
6. Total up the symbol-table size (`ssize`).

**Pass 2** (`pass2()` → `relocate_file()` → `relocate_object()`) re-reads every file in the same
order, rebuilds the local-number → global-symbol map, and copies the const, text and data
segments to the output buffers, relocating each address field (see [§6](#6-relocation)). It emits
local symbols as it goes.

### 4.2 Memory layout

After pass 1, `assign_addresses()` fixes the origin of each segment. In the normal layout each
segment starts where the previous one ended:

```
  corigin ─▶ const
  torigin ─▶ text
  dorigin ─▶ data
            bss commons        (cmorigin)
  borigin ─▶ bss
```

The base address of const (`corigin`) is `ld.basaddr`, which defaults to `BADDR` (= `HDRSZ / W`
= 8, leaving words `0…7` free) and is overridden by `-T`. The `-n` option bends this layout:

- **`-n`** (`nflag`) rounds `dorigin` up to the next 1024-word page boundary via the `ALIGN()`
  macro, so the read-only text ends on a clean page.

The bss commons sit right after data (`cmorigin = dorigin + dsize/W`), then the files' own bss
(`borigin`).

---

## 5. Symbols and resolution

### 5.1 Symbol types

A symbol is a named location with a **type** (which segment it lives in) and a **value** (its
address, or, for a common, its size). Types are defined in
[b.out.h](../cross/besm6/b.out.h). The low five bits (`N_TYPE = 037`) hold the type; the `N_EXT`
bit (`040`) marks an **external** (global) symbol.

| Type | Octal | Meaning |
|------|-------|---------|
| `N_UNDF` | `00` | undefined / unresolved external reference |
| `N_ABS` | `01` | absolute (address-independent) value |
| `N_CONST` | `02` | in the const segment |
| `N_TEXT` | `03` | in the text segment |
| `N_DATA` | `04` | in the data segment |
| `N_BSS` | `05` | in the bss segment |
| `N_STRNG` | `07` | string constant (emitted by `b6as`; treated as data) |
| `N_COMM` | `010` | common block — becomes bss when linked |
| `N_FN` | `037` | file-name symbol (debug annotation) |

For example a global function symbol has type `N_EXT + N_TEXT` (`043`).

### 5.2 Global vs. local symbols; the hash table

A **local** symbol is private to the file that defines it — it is not shared or resolved against
anything, so pass 1 only counts the bytes it will occupy in the output (and pass 2 copies it out,
subject to the `-x/-X/-S/-s` stripping flags). Local symbols named starting with `.` (`LOCSYM`)
are the compiler's temporaries and can be dropped with `-X`.

A **global** (external) symbol is shared. All globals live in one open-addressing hash table:
`ld.symtab[NSYM]` holds the entries and `ld.hshtab[NSYM+2]` is the hash index over them
(`NSYM = 2000`). `lookup_symbol()` ([symtab.c](../cmd/ld/symtab.c)) folds the name into a hash
(`i = (i << 1) + *cp` per character), starts at bucket `(i & 077777) % NSYM + 2` (the first two
buckets are reserved), and probes forward one slot at a time — wrapping at the end — until it
finds the name or an empty slot. `enter_symbol()` copies `ld.cursym` into the next free `symtab`
entry on a first sighting, or reports a hit for the caller to merge.

### 5.3 Symbol resolution

When pass 1 meets an external symbol that already exists (`scan_object()`), it merges the two
occurrences:

- A bare reference (`N_EXT + N_UNDF`) adds nothing — it is just another use.
- If the existing entry is still unresolved (undefined or common) and the new one is **also
  common**, keep the entry common but grow it to the larger requested size.
- If the existing entry is unresolved and the new one is a **real definition** (or the existing
  one was undefined), adopt the new type and value — this counts as a definition (important for
  deciding whether to keep a library member).
- If the existing entry is already defined and the new occurrence is a different definition, that
  is a `name redefined` error (checked again in pass 2 against the value pass 1 chose).

Any symbol still `N_EXT + N_UNDF` after `assign_addresses()` is reported under an `Undefined:`
banner (unless the link is relocatable), and forces `-r` on so the file can be linked again
later.

### 5.4 Common symbols

A common symbol is a *tentative* definition — think of a C global declared without an
initializer (`.comm` in the assembler). Several files may each request one; the linker
reserves a **single** slot sized to the **largest** request. While unresolved, a common's
`n_value` holds the requested size (in words). In `assign_addresses()` each common's value is
replaced by the offset of the slot it is granted within the common area, the area grows by the
requested size, and the symbol's type is rewritten to `N_EXT + N_BSS`. Under `-r` without `-d`,
commons are left undefined for a later link instead.

### 5.5 Boundary symbols and the entry point

The linker defines five **boundary symbols** that programs use to find the end of each segment
(they are given their values in `assign_addresses()` via `define_symbol()`):

| Symbol | Value |
|--------|-------|
| `_econst` | first word past the const segment |
| `_etext` | first word past the text segment |
| `_edata` | first word past the data segment |
| `_ebss` | first word past the bss segment |
| `_end` | first word past everything |

Because they are defined by the linker, a leftover reference to one of them does **not** count as
an undefined symbol when deciding whether to force `-r`.

The **entry point** written into the header (`a_entry`) is the `-e` symbol's address if one was
given (it must be in text, or resolve as undefined for a later link), otherwise the start of the
text segment (`torigin`).

### 5.6 File-name symbols

`make_file_symbol()` ([symtab.c](../cmd/ld/symtab.c)) emits a type-`N_FN` symbol carrying the
base name of each input file (directory prefixes stripped), so a debugger can tell which file the
following symbols came from. In pass 1 it merely returns the byte size such a symbol *would*
occupy (to budget `ssize`); in pass 2 it actually writes the symbol. It returns 0 — emitting
nothing — when symbols are being discarded (`-s`/`-x`).

---

## 6. Relocation

Each object is assembled as if its own segments started at address 0. Once the linker has
concatenated everybody and chosen a final base for each segment, every address baked into the
code or data is wrong by exactly that base. Each input carries **relocation records** — one 24-bit
half-word per patchable address field — saying what the field refers to and how wide it is; the
linker adds the right amount to make it correct.

### 6.1 Relocation types

A relocation record's low bits name what the field resolves against
([b.out.h](../cross/besm6/b.out.h)):

| Code | Octal | Field is relative to |
|------|-------|----------------------|
| `RABS` | `0` | nothing — an absolute value, no relocation |
| `RCONST` | `010` | the const segment |
| `RTEXT` | `020` | the text segment |
| `RDATA` | `030` | the data segment |
| `RBSS` | `040` | the bss segment |
| `RSTRNG` | `060` | string constant (assembler use; treated as data) |
| `REXT` | `070` | an external symbol; also serves as a bit mask |

For an `REXT` record the referenced symbol's index is packed into the upper bits, retrieved with
`RGETIX(h) = h >> 6` and stored with `RPUTIX(h) = h << 6`. One address-field **width** modifier
shares the low bits:

- `RSHORT` (`01`) — the field is a **12-bit short** address (`t & 07777`); without it the field
  is a **15-bit full** address (`t & 077777`).

`reloc_type()` ([reloc.c](../cmd/ld/reloc.c)) maps a resolved symbol's type back to the matching
relocation code (`N_TEXT → RTEXT`, `N_COMM → RBSS`, and so on) when an external reference becomes
segment-relative.

### 6.2 Relocation biases

`read_header()` seeds three biases per file — `ctrel`, `cdrel`, `cbrel` — one per
relocatable segment. Inside an object, an address counts from 0 within its own segment, but the
assembler stored it biased by `BADDR` and by the sizes of the segments that precede it. The
biases are the amount to add to turn an in-file address back into a clean offset within its own
segment (they start negative, subtracting the built-in offset). During pass 1, `scan_object()`
advances them by the segments accumulated from earlier files; during pass 2, `relocate_object()`
adds each segment's final origin (`torigin`, `dorigin`, `borigin`), so a bias becomes
the full amount to add to every field pointing into that segment.

### 6.3 Patching a half-word

`relocate_halfword()` ([reloc.c](../cmd/ld/reloc.c)) is the workhorse. It patches one 24-bit
half-word `t` using its record `r`, in three steps:

1. **Extract** the address field — 12 bits if `RSHORT`, else 15 bits.
2. **Compute the addend `ad`** from the record's `REXT` bits:
   - `RCONST` — redirect to the de-duplicated pool slot via `ld.newindex[]`.
   - `RTEXT`/`RDATA`/`RBSS` — add that segment's bias.
   - `REXT` — look up the external symbol by its packed index. If it is still undefined/common,
     keep the field external in the output but renumber it to the symbol's slot in the final
     global table (`ld.nsym + (sp - symtab)`). Otherwise bake in the symbol's now-known address
     and tag the record with the segment the symbol ended up in.
3. **Write** `a + ad` back into the same field width.

`relocate_constants()` runs every pooled constant (both half-words, with their `hr`/`hr2`
records) through this, and `relocate_segment()` does the same streaming through the text and data
segments. When `-r` is in effect, the rewritten records are written to the corresponding
relocation buffer so the output remains relocatable.

---

## 7. Archives and libraries

An **archive** (`.a` file, built by `b6ar`) is a bundle of object files, recognized by its magic
first word `ARMAG` (`0177545`). `open_input()` ([library.c](../cmd/ld/library.c)) opens each
input twice — `ld.text` for segment/symbol data and `ld.reloc` for relocation records, since the
two passes read from different positions — and classifies it:

| Return | Kind |
|--------|------|
| 0 | a plain object file |
| 1 | an ordinary archive (scan members one by one) |
| 2 | a randomized archive (has a `__.SYMDEF` table of contents) |
| 3 | a randomized archive whose table of contents is out of date (warn, then scan as ordinary) |

### 7.1 Selective loading

The point of a library is that only the members you actually need get linked in. In library mode
(`scan_object()` with `libflg` set, reached via `scan_member()`), a member is scanned as usual,
but it is **kept only if it defined a currently-needed symbol** (`ndef > 0`). If it defined
nothing needed, every symbol and constant it added is rolled back and its offset is not recorded.
When a member *is* kept, its byte offset is remembered in `ld.liblist[]` so pass 2 can revisit
exactly the same members, in the same order (`relocate_file()` replays the recorded offsets up to
a `-1` end marker).

### 7.2 Randomized archives

A randomized archive (one that `b6ranlib` has processed) begins with a special `__.SYMDEF`
member: a table of contents pairing every exported symbol name with the offset of the member that
defines it (`struct ranlib`; see [§8.7](#87-archive-and-ranlib-structures)). `read_ranlib()` loads
it into `ld.rantab[]`. `load_ranlib_members()` then makes one sweep: for every table entry whose
symbol is currently undefined, it pulls in the defining member. Because loading a member can
satisfy some references but introduce new ones, `scan_file()` calls `load_ranlib_members()`
repeatedly until a sweep loads nothing more — a fixed point. The table of contents is trusted
only if it is not older than the archive itself (an `st_mtime` check); a stale one produces the
`out of date (warning)` diagnostic and the archive is scanned member-by-member instead.

### 7.3 Library search

`-lname` is expanded in `open_input()` to `libname.a` and looked up in each `-L` directory in
turn: for directory `dir`, the path `dir/libname.a` is tried, and the first one that opens
wins. The search path is **empty by default** — supply one or more `-L dir` options (either
`-L dir` or the glued `-Ldir`) to point the linker at a library directory. If the path is
empty or nothing matched, `libname.a` is
tried in the current directory as a last resort. A bare `-l` is treated as `-la`.

---

## 8. The a.out file format

This section is the authoritative on-disk reference; the definitions live in
[`cross/besm6/b.out.h`](../cross/besm6/b.out.h) (and the archive headers in
[`ar.h`](../cross/besm6/ar.h) / [`ranlib.h`](../cross/besm6/ranlib.h)), and their byte-level
encoding in [`cmd/libaout/`](../cmd/libaout/). The same format is emitted by the assembler, read
and written by the linker and binutils, and read by the disassembler.

> **Byte order.** Every multi-byte quantity is stored **big-endian** (most significant byte
> first), which is natural for the BESM-6 — see the serialization code in `cmd/libaout` and its
> `README.md`.

### 8.1 File map

A file is a fixed header, the segment images, optional relocation records, the symbol table and
the string table. Byte offsets, given the header's segment sizes:

```
  header:            0
  const:             48
  text:              48 +   constsize
  data:              48 +   constsize +   textsize
  const relocation:  48 +   constsize +   textsize +   datasize   ┐
  text relocation:   48 + 2*constsize +   textsize +   datasize   │ present only while
  data relocation:   48 + 2*constsize + 2*textsize +   datasize   ┘ RELFLG is clear
  symbol table:      48 + 2*constsize + 2*textsize + 2*datasize
  string table:      symbol table + a_syms
```

The relocation sections exist only while the file is still relocatable — that is, while `RELFLG`
is **clear** (an object straight from the assembler, or a `-r` output). Beware the flag's name:
despite being called `RELFLG`, a *set* bit marks a file with **no** relocation (see
[§8.3](#83-magic-numbers-and-flags)). Three convenience macros compute these positions:

```c
N_TXTOFF(x)  =  HDRSZ                                       /* start of the segment images */
N_SYMOFF(x)  =  N_TXTOFF(x) + a_const + a_text + a_data     /* start of the symbol table   */
N_STROFF(x)  =  N_SYMOFF(x) + a_syms                        /* start of the string table   */
```

### 8.2 The header (`struct exec`)

```c
struct exec {
    word_t  a_magic;    /* magic number */
    word_t  a_const;    /* const segment size, bytes (multiple of 6) */
    word_t  a_text;     /* text (code) segment size, bytes */
    word_t  a_data;     /* initialized data segment size, bytes */
    word_t  a_bss;      /* uninitialized data (bss) size, bytes */
    word_t  a_syms;     /* symbol table size, bytes */
    word_t  a_entry;    /* entry point, word address */
    word_t  a_flag;     /* flags: RELFLG */
};
```

The four segment sizes and `a_syms` are **byte counts, each a multiple of 6**; `read_header()`
rejects a file whose const/text/data/bss size is not. `a_entry` is a **word address**.
`HDRSZ` is **48 bytes = 8 words**: although the struct has 8 logical fields, each is written as a
full 6-byte word — a zero padding half-word followed by the value in the low half-word — so every
field, and the const segment that follows, starts on a clean word boundary. `fputhdr()`/
`fgethdr()` ([cmd/libaout](../cmd/libaout/)) do this, calling `fputw`/`fgetw` eight times.

### 8.3 Magic numbers and flags

`a_magic` is the string `"BESM"` combined with a Unix-style variant code in the low bits:

| Constant | Octal | Meaning | Chosen by |
|----------|-------|---------|-----------|
| `FMAGIC` | `02044252323200407` | standard relocatable / impure executable | default |
| `NMAGIC` | `02044252323200410` | read-only (pure) text segment | `-n` |

`FMAGIC` and `NMAGIC` are the only magic numbers; both are accepted on **input** —
`BADMAG(x)` is true for anything else. `setup_output()` picks the magic from `-n` and
sets two `a_flag` bits:

| Flag | Octal | Meaning |
|------|-------|---------|
| `RELFLG` | `1` | **set** means the file is fully linked / non-relocatable (no relocation records); **clear** means it still contains relocation records |

Mind the name: a **set** `RELFLG` marks a *non-relocatable* file (no relocation records), not a
relocatable one. `setup_output()` **sets** it when `-r` is *not* in effect (a finished
executable, no relocation records) and **clears** it under `-r`. Consistently, a plain assembler
object carries relocation and so has `RELFLG` clear, and the linker's `not relocatable` error (in
`scan_object()`) rejects an input that arrives with `RELFLG` already set — there is no relocation
information left to link it with.

### 8.4 Symbol table entries (`struct nlist`)

In memory a symbol is:

```c
struct nlist {
    word_t  n_len;      /* length of the name in bytes */
    word_t  n_type;     /* symbol type (N_* values, with the N_EXT bit) */
    word_t  n_value;    /* symbol value (address or constant) */
    char *  n_name;     /* pointer to a separately allocated NUL-terminated name */
};
```

On disk (`fputsym`/`fgetsym`) an entry is packed tightly, **not** word-padded:

```
  +--------+--------+------------------+-----------------------+
  | n_len  | n_type |  n_value (3 B)   |  name (n_len bytes)   |
  | 1 byte | 1 byte |  big-endian      |  no trailing NUL      |
  +--------+--------+------------------+-----------------------+
```

so one entry is `n_len + 5` bytes. The symbol table is a run of such entries terminated by a
single **zero length byte** (a name length of 0, which `fgetsym` reports as end-of-table). In the
output, `finish_output()` writes the local symbols, then every global, then the terminating zero
byte, then pads with zeros up to a whole word (`ssize` rounded to a multiple of `W`).

### 8.5 Relocation records

A relocation record is one **24-bit half-word**, stored (big-endian) in the relocation section
that parallels its segment — one record per half-word of const/text/data. Its layout mirrors
[§6.1](#61-relocation-types): the low bits carry the relocation type and the `RSHORT` width
modifier, and for an `REXT` record the referenced symbol's index is packed into the upper bits
(`RGETIX`/`RPUTIX`). A full 6-byte data word therefore has two relocation half-words, one per
instruction/value half.

### 8.6 Word encoding and the libaout helpers

All I/O goes through [`cmd/libaout/`](../cmd/libaout/):

| Helper | Reads/writes | Encoding |
|--------|--------------|----------|
| `fgeth` / `fputh` | one 24-bit half-word | 3 big-endian bytes |
| `fgetw` / `fputw` | one 48-bit word | two big-endian half-words, high half first |
| `fgethdr` / `fputhdr` | a `struct exec` header | 8 words |
| `fgetsym` / `fputsym` | one symbol entry | 1+1+3 bytes + name |
| `fgetran` / `fputran` | one ranlib entry | 1+3 bytes + name |
| `fgetarhdr` | one archive member header | see [§8.7](#87-archive-and-ranlib-structures) |
| `fgetint` | an `int` stored as a full word | value in the low half-word, high half discarded |

`fgetint` reflects a convention worth noting: a plain integer (such as the archive magic word) is
stored as a whole 6-byte word, with the value in the low 24-bit half-word and the high half-word
zero. The file-descriptor counterparts `getint`/`putint` and `getarhdr`/`putarhdr` (used by
`b6ar`/`b6ranlib` for in-place archive editing) use the identical big-endian 6-byte layout via
`read(2)`/`write(2)`.

### 8.7 Archive and ranlib structures

An archive begins with the magic word `ARMAG` (`0177545`) and is a sequence of members, each
preceded by a variable-size, word-aligned header ([`ar.h`](../cross/besm6/ar.h)):

```c
#define ARMAG     0177545
#define ARMAXNAME 255       /* longest member name, in bytes */

struct ar_hdr {
    char   *ar_name;        /* malloc'd, NUL-terminated (caller-owned) */
    word_t  ar_date;        /* full word */
    word_t  ar_uid;         /* full word (value in low half) */
    word_t  ar_gid;         /* full word (value in low half) */
    word_t  ar_mode;        /* full word (value in low half) */
    word_t  ar_size;        /* full word: member size in bytes */
};

int arhdrsz(const struct ar_hdr *h);   /* on-disk header size for h->ar_name */
```

The member name is length-prefixed on disk — a 1-byte length (up to `ARMAXNAME`) followed by that
many name bytes, zero-padded so the length byte plus name occupy a whole number of words — and then
the five metadata words. `ar_date`/`ar_size` carry full 48-bit values, while `ar_uid`/`ar_gid`/
`ar_mode` keep their value in the low half-word. In memory `ar_name` is a malloc'd, NUL-terminated
string that `fgetarhdr()`/`getarhdr()` allocate and the caller must `free()`. The whole header is
`arhdrsz(&h) = roundup(1 + strlen(ar_name), 6) + 5·6` bytes — always a multiple of 6 — so the next
member's header follows `ar_size + arhdrsz(&h)` bytes on.

The table of contents of a randomized archive is a `__.SYMDEF` member whose payload is a run of
ranlib entries ([`ranlib.h`](../cross/besm6/ranlib.h)):

```c
struct ranlib {
    word_t  ran_len;    /* 1 byte  - name length */
    word_t  ran_off;    /* 3 bytes - byte offset of the defining member */
    char *  ran_name;   /* the exported symbol name */
};
```

On disk (`fgetran`/`fputran`) that is a 1-byte length, a 3-byte big-endian offset, then
`ran_len` name bytes; a zero length byte ends the table.

---

## 9. Output modes and file assembly

`setup_output()` opens the real output plus a scratch buffer for each segment (and, under `-r`,
for each segment's relocation), fills in the header, and writes it. Each segment is written to its
own temporary file during pass 2, and `finish_output()` concatenates them into the final image.

The four output shapes are:

- **Impure executable** (default, `FMAGIC`) — const, text, data all writable and contiguous.
- **Pure text** (`-n`, `NMAGIC`) — text is read-only; the data segment is page-aligned so the
  text page can be shared / write-protected.
- **Relocatable** (`-r`, `RELFLG` cleared) — keeps the relocation records and (with `-d`)
  leaves commons undefined, so the file can be linked again.

Under `-n`, `finish_output()` pads the text buffer with zero words up to the
next page boundary before concatenating. The segments are then appended in the header's order —
const, text, data — followed, under `-r`, by the
relocation buffers in the same order, and finally the symbol table. The image is built under the
scratch name `l.out`; if no `-o` was given it is then hard-linked to `a.out`. On success the
output is made executable (`chmod 0777 & ~umask`) unless `-r` was given.

The temporary files themselves are created via `mkstemp("/tmp/ldaXXXXX")` and immediately
`unlink`ed, so they have no directory entry but remain usable until closed — and are cleaned up
even if the link is interrupted (the `SIGINT`/`SIGTERM` handler in [main.c](../cmd/ld/main.c)
calls `cleanup_and_exit()`).

---

## 10. Diagnostics

Messages come from `error(n, fmt, …)` ([ld.c](../cmd/ld/ld.c)). The severity `n` is:

- **0 — warning**: printed, but `errlev` stays 0.
- **1 — error**: remembered (`errlev = 1`) and the run continues, so one link reports every
  problem instead of stopping at the first.
- **2 — fatal**: printed, then the linker aborts immediately via `cleanup_and_exit()`.

The prefix `ld: ` is printed once per run, and the current input file name is shown when known.
The exit status is the highest severity seen.

| Message | Severity | Cause |
|---------|:--------:|-------|
| `bad format` | 2 | header could not be read |
| `bad magic` | 2 | `a_magic` is not `FMAGIC` on input |
| `bad length of const`/`text`/`data`/`bss` | 2 | that segment size is not a multiple of 6 |
| `not relocatable` | 1 | input has `RELFLG` set (already linked / no relocation records) |
| `const`/`text`/`data`/`bss`/`symbol table segment overflow` | 1 | a running total passed `0100000` words |
| `constant table overflow` | 2 | more than `NCONST` (512) pooled constants |
| `Undefined:` + a list of names | 1 | genuine symbols left unresolved (non-relocatable link) |
| `long address: name=0…` | 1 | a symbol value exceeded the 24-bit value field (`~077777777`) |
| `name redefined` | 1 | a global defined incompatibly in two inputs |
| `entry out of text` | 1 | the `-e` symbol did not resolve into the text segment |
| `bad symbol reference` | 2 | a relocation named a local symbol number that does not exist |
| `symbol table overflow` | 2 | more than `NSYM` (2000) globals |
| `local symbol table overflow` | 2 | more than `NSYMPR` (1000) local references in one file |
| `library table overflow` | 2 | more than `LLSIZE` (256) recorded archive members |
| `ranlib buffer overflow` | 2 | a `__.SYMDEF` table larger than `RANTABSZ` (1000) |
| `internal error: symbol not found` | 2 | a global vanished between the two passes |
| `cannot open` / `cannot create output file` / `cannot create temporary file` | 2 | file-system failure |
| `-o`/`-u`/`-e`/`-D: argument missed` | 2 | a value-taking option had no argument |
| `-D: too small` | 2 | `-D` size below the data already accumulated |
| `unknown flag` | 2 | an unrecognized option letter |
| `out of date (warning)` | 0 | a randomized archive's table of contents is stale |
| `out of memory` | 2 | allocation failure while reading symbols |
| `unexpected EOF` | 1 | an archive did not start with `ARMAG` |

---

## 11. Worked example

Assemble two source files and a library, then link them into an executable:

```sh
b6as -o hello.o hello.s        # produce two relocatable objects
b6as -o util.o  util.s
b6ld -o hello hello.o util.o -lc
```

What the linker does, step by step:

1. **Pass 1** reads `hello.o`, then `util.o`, accumulating the const/text/data/bss sizes,
   merging their constant pools, and entering their globals. `hello.o`'s reference to `printf`
   is entered as undefined.
2. `-lc` expands to `/usr/local/lib/microbesm/libc.a`. Because it is a randomized archive, its
   `__.SYMDEF` table is consulted: the member defining `printf` is pulled in (satisfying the
   reference), which may itself reference `write`, pulling in another member, and so on until no
   new undefined symbols remain.
3. **`assign_addresses()`** defines `_econst`…`_end`, reserves slots for any common symbols, and
   fixes the segment origins starting at `BADDR` — const, then text, then data, then the bss
   commons and bss. Every global gets its final address; nothing is left undefined, so
   the link stays a finished (non-relocatable) executable.
4. **Pass 2** re-reads the same files in order, copies const/text/data into per-segment buffers
   while relocating every address field, and emits the local and file-name symbols.
5. **`finish_output()`** concatenates header + const + text + data + symbol table into `l.out`
   and, because `-o hello` was given, renames the result to `hello` and marks it executable.

Useful variants:

```sh
b6ld -r -o part.o a.o b.o      # incremental link: keep relocation, leave commons undefined
b6ld -n -o pure a.o b.o        # read-only (pure) text, page-aligned data  (NMAGIC)
b6ld -s -o small a.o b.o       # strip all symbols from the output
b6ld -e start -o img a.o       # make `start` the entry point
```

Add `-t` (once, twice, or three times) to any of these to trace the files, per-segment biases,
and individual half-word fixups.

---

## 12. Source pointers

The authoritative implementation lives in [`cmd/ld/`](../cmd/ld/):

| File | Responsibility |
|------|----------------|
| `main.c` | command-line front end; signal handlers |
| `ld.c` | engine framework, `read_header`, `assign_addresses`, `error` |
| `pass1.c` | pass 1 and option parsing |
| `pass2.c` | pass 2 (relocate and emit) |
| `symtab.c` | symbol hash table, lookup/insert, symbol relocation, file symbols |
| `library.c` | archives and randomized (`__.SYMDEF`) libraries |
| `reloc.c` | relocation record interpretation and half-word patching |
| `output.c` | output buffers, header, final concatenation |
| `intern.h` / `ld.h` | engine state and the public entry point |

The object/executable format is defined in [`cross/besm6/b.out.h`](../cross/besm6/b.out.h),
[`cross/besm6/ar.h`](../cross/besm6/ar.h) and [`cross/besm6/ranlib.h`](../cross/besm6/ranlib.h),
and serialized by [`cmd/libaout/`](../cmd/libaout/). The assembler that produces the linker's
inputs is documented in [Assembler_Manual.md](Assembler_Manual.md).
