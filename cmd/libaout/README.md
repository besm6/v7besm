# libaout — BESM-6 a.out object/archive serialization

A small static library (`aout`) of helper routines that serialize and deserialize
object files, executables and archives in the BESM-6 `a.out` format. It is shared by the
whole toolchain — the assembler (`cmd/as`), the linker and the binutils
(`cmd/ld`, plus `ar`/`nm`/`size`/`strip`/`ranlib`). The library is built with the *host*
C compiler: it runs on the host and manipulates BESM-6 objects, so it never executes on
the target machine itself.

The on-disk structures (`struct exec`, `struct nlist`, `struct ar_hdr`, `struct ranlib`)
and the format-level constants are declared in the cross headers:

- [`cross/besm6/b.out.h`](../../cross/besm6/b.out.h) — `exec` header, `nlist` symbol,
  magic numbers, symbol/relocation types.
- [`cross/besm6/ar.h`](../../cross/besm6/ar.h) — `ar_hdr` archive member header.
- [`cross/besm6/ranlib.h`](../../cross/besm6/ranlib.h) — `ranlib` archive symbol index.

## On-disk encoding

The BESM-6 is a 48-bit word machine. The serialization conventions follow from that:

- **Word** = 48 bits = **6 bytes**. **Half-word** = 24 bits = **3 bytes**. Every
  multi-byte quantity is stored **little-endian**.
- The primitive unit moved by the library is the half-word: `fgeth` reads one and
  `fputh` writes one (3 bytes).
- An `int`/`long` field is stored as **two half-words (6 bytes == one word)**. Only the
  low half-word carries the value; the high half-word is written as zero and ignored on
  read (`fgetint`, `getint`, `putint`).
- The **exec header** stores its 9 logical fields each as a value half-word followed by
  a zero padding half-word, so each field begins on a 6-byte word boundary; the whole
  header is `HDRSZ == 54` bytes (9 words). See `fgethdr`/`fputhdr`.
- The **archive member header** is a 46-byte record: 14 name bytes, 2 zero bytes, a
  two-half-word date, a half-word each for uid/gid/mode (each followed by a discarded
  high half-word), and a two-half-word size. See `fgetarhdr`/`getarhdr`/`putarhdr`.
  (Note: `ARHDRSZ == 56` in `ar.h` is the in-memory `struct ar_hdr` size, distinct from
  this 46-byte on-disk record.)
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

### Primitive half-word I/O

#### `long fgeth(FILE *f)` — [`fgeth.c`](fgeth.c)

Read one 24-bit half-word (3 little-endian bytes) and return it as a `long`. Building
block for all the stream readers.

#### `void fputh(long h, FILE *f)` — [`fputh.c`](fputh.c)

Write the low 24 bits of `h` as one half-word (3 little-endian bytes). Building block for
all the stream writers.

### Exec (object/executable) header

#### `int fgethdr(FILE *text, struct exec *h)` — [`fgethdr.c`](fgethdr.c)

Read a 54-byte exec header into `*h`, decoding each of the 9 fields from a value
half-word plus a discarded padding half-word. Always returns 1.

#### `void fputhdr(const struct exec *filhdr, FILE *coutb)` — [`fputhdr.c`](fputhdr.c)

Write an exec header, the inverse of `fgethdr`: each field as a value half-word followed
by a zero padding half-word.

### Integers

#### `int fgetint(FILE *f, int *i)` — [`fgetint.c`](fgetint.c)

Read one int from a stream as two half-words (6 bytes); the value is the low half-word,
the high half-word is ignored. Always returns 1.

#### `int getint(int f, int *i)` — [`getint.c`](getint.c)

File-descriptor counterpart of `fgetint`. Reads 6 bytes via `read(2)` and stores the low
half-word in `*i`. Returns 1 on success, 0 on a short read.

#### `int putint(int f, int i)` — [`putint.c`](putint.c)

Write one int via `write(2)` as 6 bytes: the value in the low half-word, the high
half-word zero. Returns 1 on success, 0 on a short write.

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

#### `int fgetarhdr(FILE *f, struct ar_hdr *h)` — [`fgetarhdr.c`](fgetarhdr.c)

Read one 46-byte archive member header from a stream into `*h`. Returns 1 on success, 0
on EOF or when a non-zero byte appears where padding is expected.

#### `int getarhdr(int f, struct ar_hdr *h)` — [`getarhdr.c`](getarhdr.c)

File-descriptor counterpart of `fgetarhdr`. Reads the 46-byte record with a single
`read(2)` and decodes it. Returns 1 on success, 0 on a short read.

#### `int putarhdr(int f, const struct ar_hdr *h)` — [`putarhdr.c`](putarhdr.c)

Encode an archive member header into a 46-byte buffer and write it via `write(2)`.
Returns 1 on success, 0 on a short write.

## Tests

GoogleTest unit tests live in [`test/`](test):

- [`test/fhdr_test.cpp`](test/fhdr_test.cpp) — round-trips for `fputhdr`/`fgethdr`.
- [`test/ar_test.cpp`](test/ar_test.cpp) — round-trips for the archive-header helpers.

They are built as the `libaout_test` target (see [`test/CMakeLists.txt`](test/CMakeLists.txt))
and run via CTest.
