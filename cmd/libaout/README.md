# libaout — BESM-6 a.out object/archive serialization

A small static library (`aout`) of helper routines that serialize and deserialize
object files, executables and archives in the BESM-6 `a.out` format. It is shared by the
whole toolchain — the assembler (`cmd/as`), the linker and the binutils
(`cmd/ld`, plus `ar`/`nm`/`size`/`strip`/`ranlib`). The `aout` target here is built with
the *host* C compiler and runs on the host.

**These sources are also cross-compiled and run on the BESM-6 itself.** The native
`/usr/bin/as` and `/usr/bin/ld` (task **C9b** in [../TODO.md](../TODO.md)), the read-only
binutils (**C9c**) and `/usr/bin/{ar,ranlib}` (**C9d**) link them directly — there is no
native archive — so [`sources.cmake`](sources.cmake) names the
list once for both worlds. It names two lists: everything, and the subset without the
four file-descriptor routines, which only `ar`/`ranlib` call and which would otherwise
be dead weight on a program that has 28,672 words to live in. Since C9d both lists are
live on the target: `cmd/{ar,ranlib}/rootfs` take the whole of it, the other six the
subset. What makes the same source
serve both is `word_t`/`uword_t` in [`../../cross/besm6/types.h`](../../cross/besm6/types.h)
— `int64_t`/`uint64_t` for the host, `int`/`unsigned` under the `besm6` predefine, where
a word is 48 bits and an `int` is 41.

The on-disk structures (`struct exec`, `struct nlist`, `struct ar_hdr`, `struct ranlib`)
and the format-level constants are declared in the cross headers:

- [`cross/besm6/b.out.h`](../../cross/besm6/b.out.h) — `exec` header, `nlist` symbol,
  magic numbers, symbol/relocation types.
- [`cross/besm6/ar.h`](../../cross/besm6/ar.h) — `ar_hdr` archive member header.
- [`cross/besm6/ranlib.h`](../../cross/besm6/ranlib.h) — `ranlib` archive symbol index.

## On-disk encoding

The BESM-6 is a 48-bit word machine. The serialization conventions follow from that:

- **Word** = 48 bits = **6 bytes**. **Half-word** = 24 bits = **3 bytes**. Every
  multi-byte quantity is stored **big-endian** (most significant byte first), natural
  for the BESM-6.
- The primitive unit moved by the library is the half-word: `fgeth` reads one and
  `fputh` writes one (3 bytes, most significant byte first).
- A full **word** is stored as two half-words (6 bytes), high half-word first, so the
  six bytes form a plain big-endian 48-bit number: `fgetw` reads one and `fputw` writes
  one (`uword_t`, low 48 bits).
- An `int`/`long` field is stored as **two half-words (6 bytes == one word)**. Only the
  low (second) half-word carries the value; the high (first) half-word is written as
  zero and ignored on read (`fgetint`, `getint`, `putint`).
- The **exec header** stores its **8** logical fields each as a zero padding half-word
  followed by the value half-word, so each field begins on a 6-byte word boundary; the
  whole header is `HDRSZ == 48` bytes (8 words). See `fgethdr`/`fputhdr`.
  `a_magic` is the one field whose high half-word is *not* padding: it carries the `BES`
  of the `BESM` tag, which is why `fhdr_test.cpp`'s `PadBytesAreZero` skips field 0.
  (This file used to say nine fields and 54 bytes; `<sys/param.h>`'s `BADDR 8` and
  `fhdr_test.cpp`'s golden bytes are the authority. Task C5f corrected it.)
- The **archive member header** is a variable-size, fully word-aligned record: a 1-byte
  name length (up to `ARMAXNAME == 255`), that many name bytes, zero padding up to a whole
  word, then one full 48-bit word each for date, uid, gid, mode and size (for uid/gid/mode
  the value is the low half-word, preceded by a discarded high half-word). Its on-disk size
  is `arhdrsz(&h) == roundup(1 + strlen(ar_name), 6) + 5·6`, always a multiple of 6. In
  memory `ar_name` is malloc'd and NUL-terminated (caller-owned, like `ran_name`). See
  `arhdrsz`/`fgetarhdr`/`getarhdr`/`putarhdr`.
- A **symbol** is: 1 length byte, 1 type byte, a half-word value, then `n_len` name
  bytes with no trailing NUL. In memory `n_name` points at a separately malloc'd,
  NUL-terminated copy. See `fgetsym`/`fputsym`.
- A **ranlib entry** is: 1 length byte, a half-word archive offset, then `ran_len` name
  bytes. `ran_name` is malloc'd and NUL-terminated on read. See `fgetran`/`fputran`.

## Two I/O flavors

Most routines work on a buffered `FILE *` stream (the `f…` family) and are used for
sequential reading/writing of objects. A few work directly on a Unix file descriptor
(`getint`, `putint`, `getarhdr`, `putarhdr`) so that `ar`/`ranlib` can rewrite archive
members in place. Both flavors share the same byte-level layout described above.

## Function reference

### Primitive half-word / word I/O

#### `long fgeth(FILE *f)` — [`fgeth.c`](fgeth.c)

Read one 24-bit half-word (3 big-endian bytes) and return it as a `long`. Building
block for all the stream readers.

#### `void fputh(long h, FILE *f)` — [`fputh.c`](fputh.c)

Write the low 24 bits of `h` as one half-word (3 big-endian bytes). Building block for
all the stream writers.

#### `uword_t fgetw(FILE *f)` — [`fgetw.c`](fgetw.c)

Read one full 48-bit word as two big-endian half-words (6 bytes), high half-word first,
and return it as a `uword_t`.

#### `void fputw(uword_t w, FILE *f)` — [`fputw.c`](fputw.c)

Write the low 48 bits of `w` as two big-endian half-words (6 bytes), high half-word
first.

### Exec (object/executable) header

#### `int fgethdr(FILE *text, struct exec *h)` — [`fgethdr.c`](fgethdr.c)

Read a 48-byte exec header into `*h`, decoding each of the 8 fields from a discarded
padding half-word plus a value half-word. Always returns 1. It does **not** validate the
magic; callers use `N_BADMAG`.

#### `void fputhdr(const struct exec *filhdr, FILE *coutb)` — [`fputhdr.c`](fputhdr.c)

Write an exec header, the inverse of `fgethdr`: each field as a zero padding half-word
followed by the value half-word. (This line used to state the two halves the other way
round.)

### Integers

#### `int fgetint(FILE *f, int *i)` — [`fgetint.c`](fgetint.c)

Read one int from a stream as two half-words (6 bytes); the value is the low half-word,
the high half-word is ignored. Always returns 1.

#### `int getint(int f, uword_t *i)` — [`getint.c`](getint.c)

File-descriptor counterpart of `fgetint`, but reads the **whole** 48-bit word rather
than just the low half: it is what recognizes `ARMAG` and steps an archive. Reads 6
bytes via `read(2)`. Returns 1 on success, 0 on a short read.

#### `int putint(int f, uword_t i)` — [`putint.c`](putint.c)

Write one word via `write(2)` as 6 bytes, high half-word first. Returns 1 on success,
0 on a short write.

### Symbol table

#### `int fgetsym(FILE *text, struct nlist *sym)` — [`fgetsym.c`](fgetsym.c)

Read one symbol entry (length byte, type byte, half-word value, name bytes) into `*sym`,
allocating and NUL-terminating `sym->n_name`. Returns the on-disk entry size in bytes
(`n_len + 5`) on success, 1 for an empty entry / EOF (zero-length name), or 0 on out of
memory.

#### `void fputsym(const struct nlist *s, FILE *file)` — [`fputsym.c`](fputsym.c)

Write one symbol entry, the inverse of `fgetsym`: length byte, type byte, half-word
value, then the name bytes (no trailing NUL).

### Ranlib (archive symbol index)

#### `int fgetran(FILE *text, struct ranlib *sym)` — [`fgetran.c`](fgetran.c)

Read one ranlib entry (length byte, half-word archive offset, name bytes) into `*sym`,
allocating and NUL-terminating `sym->ran_name`. Returns 1 on success, 0 on EOF
(zero-length entry), -1 on out of memory.

#### `void fputran(const struct ranlib *s, FILE *file)` — [`fputran.c`](fputran.c)

Write one ranlib entry, the inverse of `fgetran`: length byte, half-word offset, then the
name bytes (no trailing NUL).

### Archive member header

#### `int arhdrsz(const struct ar_hdr *h)` — [`arhdrsz.c`](arhdrsz.c)

Return the on-disk byte size of the member header for the name in `*h`:
`roundup(1 + strlen(h->ar_name), 6) + 5·6`, always a multiple of 6. Readers use it to step
from one member to the next (`ar_size + arhdrsz(&h)`).

#### `int fgetarhdr(FILE *f, struct ar_hdr *h)` — [`fgetarhdr.c`](fgetarhdr.c)

Read one variable-size archive member header from a stream into `*h`, allocating a
NUL-terminated `h->ar_name` (caller must `free()` it). Returns 1 on success, 0 on EOF, -1
on out of memory.

#### `int getarhdr(int f, struct ar_hdr *h)` — [`getarhdr.c`](getarhdr.c)

File-descriptor counterpart of `fgetarhdr`. Reads the length byte, then the rest of the
record, with `read(2)` and decodes it, allocating `h->ar_name` (caller must `free()` it).
Returns 1 on success, 0 on a short read.

#### `int putarhdr(int f, const struct ar_hdr *h)` — [`putarhdr.c`](putarhdr.c)

Encode an archive member header (length byte + name + word padding + 5 metadata words) and
write it via `write(2)`. Only reads `h->ar_name` (may be a borrowed string). Returns 1 on
success, 0 on a short write.

## Tests

GoogleTest unit tests live in [`test/`](test):

- [`test/half_test.cpp`](test/half_test.cpp) — the `fputh`/`fgeth` half-word primitives,
  including the big-endian byte layout and 24-bit truncation.
- [`test/word_test.cpp`](test/word_test.cpp) — the `fputw`/`fgetw` full-word primitives,
  including the big-endian byte layout and 48-bit truncation.
- [`test/fhdr_test.cpp`](test/fhdr_test.cpp) — round-trips and raw byte layout for
  `fputhdr`/`fgethdr`.
- [`test/sym_test.cpp`](test/sym_test.cpp) — `fputsym`/`fgetsym`, covering the encoded
  size, NUL-termination, the empty-entry terminator and sequential reads.
- [`test/ar_test.cpp`](test/ar_test.cpp) — the int and archive/ranlib helpers, including
  the full-word (48-bit) date/size fields and EOF handling.

They are built as the `libaout_test` target (see [`test/CMakeLists.txt`](test/CMakeLists.txt))
and run via CTest.
