# ls — Unix v7 `ls(1)` for the BESM-6

`/bin/ls`, compiled by the `b6*` toolchain and staged as **`build/rootfs/bin/ls`**. Like
[`cmd/init/`](../init/), [`cmd/sh/`](../sh/) and its three neighbours, this is a `cmd/`
subdirectory that is **not a host tool** — nothing here runs on the build machine.

It is the largest of the four commands task 24 added (**7,678 words of the 28,672**), the one
that pulls in most of libc — stdio and `doprnt`, `malloc`, `qsort`, `ctime`, `strcmp` — and the
one where the machine reaches furthest into a v7 source after the shell itself. This file is
the account of that; [`cmd/sh/README.md`](../sh/README.md) is still the one to read first,
since the hazards it names come round again here.

## Building

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/rootfs/bin/ls among everything else
make run        # runs its size check (ctest `rootfs_ls_size', label `rootfs') with the rest
```

[`CMakeLists.txt`](CMakeLists.txt) is one `b6_prog()` call. No `CFLAGS`: unlike `cmd/sh` there
is no private header to put a `-I.` in front of.

## A fourth fat-pointer hazard: `<` does not order two `char *`

`cmd/sh/README.md` lists three ways a v7 source goes wrong on this machine — a flag packed
into bit 0 of a pointer, a bit mask used to round to a word, and a cast to a node pointer that
floors rather than rounds. Bounding a copy loop in `makename()` turned up a fourth, and it is
the sharpest of them because the code looks completely ordinary:

> **A relational operator between two `char *` values gives the wrong answer.** A fat pointer
> carries its byte offset in bits 47–45 and its word address in bits 15–1, and the offset
> *decrements* as the pointer advances (`b$pinc`,
> [`doc/Besm6_Runtime_Library.md`](../../doc/Besm6_Runtime_Library.md)). There is no relational
> helper — `<` compiles to an integer comparison of the whole word — so the offset field
> dominates the address field and the ordering comes out scrambled and inverted within a word.
> `p < end` on a buffer cursor is silently, unpredictably wrong.

v7's `ls` never does it: every `p < &tab[N]` in the file is over an array of *word*-sized
objects, whose pointers are thin word addresses and compare correctly. So the fix here was to
write `makename()`'s bound with explicit indices rather than introduce the first one — the same
answer [`kernel/prim.c`](../../kernel/prim.c) reached for the clists. **Subtraction is fine**
(`b$pdiff` exists and decodes both operands); it is ordering that has no helper.

## What else the port changed

### Silent-output changes — the ones worth reviewing separately from the C11 pass

* **`%D` prints the two characters `%D`.** v7 wrote `%D` for a long. This libc's
  [`doprnt.c`](../../lib/libc/stdio/doprnt.c) does not know that conversion, and an unknown
  conversion is **echoed verbatim and consumes no argument** — so `printf("total %D\n", tblocks)`
  would print `total %D` *and* desynchronize every later conversion in the same format. Both
  occurrences are `%d`. (`l`/`h`/`L`/`j`/`z`/`t` are parsed and ignored, since a `long` **is** an
  `int` here, so `%7ld` was harmless; it is `%7d` anyway, because there is no `long` to mean.)
* **`ISARG` is `S_IFREG`.** Both are `0100000`. v7 packed its own flag into `lflags` beside
  `statb.st_mode & ~S_IFMT` and got away with it because `lflags` was a 16-bit `short`, so the
  complement cleared the whole `0170000` type field and nothing above it existed. Here
  `st_mode` is a 41-bit `int` and `~S_IFMT` keeps every bit from 16 upward — the two would be
  the same bit. `lflags` is masked to `07777` instead; everything the `m1`…`m9` tables examine
  is `≤ 04000`, so nothing is lost and the collision cannot come back however wide `st_mode`
  becomes.
* **A block is `BSIZE` = 3072 bytes.** `nblock()` was `(size + 511) >> 9`, and there is no
  `BSHIFT`/`BMASK` to replace the shift with — 3072 is not a power of two, and
  `sys/param.h` says so outright. It is a divide now. **`ls -s` and the `total` line report in
  1024-byte blocks**, three per filesystem block: `nblock()` and `tblocks` still hold
  filesystem blocks and the multiply by `KBPB` is at the two `printf`s, which is the rule
  [../README.md](../README.md) §4 states for all four programs that report a block count. Two
  consequences: every number is a multiple of three, and it is *half* a PDP-11's rather than a
  sixth. (It counted the filesystem block until task C4a moved all four to one unit.)
* **`DIRSIZ` is 18, not 14**, and `struct direct` is four words with a full-word `d_ino`. So
  `lname[15]` is `lname[DIRSIZ + 1]` and `%.14s` is `%s`.

### Two v7 bugs the port had to fix rather than carry

* **`lname` was never NUL-terminated.** `readdir()` copied `DIRSIZ` characters and stopped; v7
  got away with it because `printf("%.14s")` capped the print — but `compar()` calls `strcmp()`
  on that array, which runs to a NUL that need not be there. Terminated now, which is what the
  `+ 1` is for.
* **`getname()` copied a login name into `tbuf[16]` with no bound.** It takes the size now.
  Its `c = '0'` on a colon looks like a typo and is **not**: it makes the digit accumulator's
  `c - '0'` a harmless `+0` for the colon that *enters* field 2, so the digits can be summed
  without a separate test. There is a comment saying so, or it will be "fixed".

### The union, and why it is still there

`struct lbuf` overlays `char lname[DIRSIZ + 1]` with `char *namep`, which on this machine are
wholly incompatible representations: writing six characters fills the byte fields of the
union's first word, and reading that word back as a fat pointer yields an arbitrary marker bit,
byte offset and address. Reading the wrong arm is silent garbage, not a fault.

It is kept — it saves three words an entry — because the discipline is airtight and was traced
before trusting it: `main()` is the **only** writer of `namep` and sets `ISARG` on the next
line; `listdir()` is the only writer of `lname` and its entries never carry `ISARG`; and both
readers, `pentry()` and `compar()`, test the flag first. The struct carries that argument as a
comment. Do not add a third accessor without checking the flag.

### C11, and the mechanical rest

`b6parse` is strict C11, so: prototypes and explicit return types everywhere, `static` on all
sixteen file-scope objects and all nine functions, no untyped `register t;`.

* `compar()` takes `(const void *, const void *)` and casts inside. The call sites needed
  nothing: `flist` is an array of word-sized pointers at byte #0 with `es == 6`, which is the
  case [`qsort.c`](../../lib/libc/gen/qsort.c) exchanges a word at a time.
* `extern char *malloc();` inside `gstat()` is deleted — it conflicts with `<stdlib.h>`. The
  cast of `malloc`'s `void *` to a struct pointer **floors**, dropping the byte offset, which is
  exactly right: [`malloc.c`](../../lib/libc/gen/malloc.c) guarantees every block starts at
  byte #0 of a word, so there is nothing to round.
* The anonymous `struct { char dminor, dmajor; };` inside `pentry()` is a C11 §6.7p2 constraint
  violation — no declarator, no tag, not an enum — and was **dead in v7 too**, a fossil of a
  PDP-11 `ls` that took `st_rdev` apart by hand instead of using `major()`/`minor()`. Deleted.
* `argv = &dotp - 1;` formed a pointer before the start of an object. It *works* here — a
  `char **` is thin, so it is a plain word decrement — but it is undefined and it is the kind of
  thing that stops working in silence. It is a two-element array now.
* `readdir()` and `select()` were file-local. Neither collides today: this tree has no
  `<dirent.h>` and no BSD `select`. Both are renamed anyway — `listdir()` and `selbit()` —
  because both are names a later header will want.
* The `m1`…`m9` permission tables are `const`, which moves ~51 words out of `data`.

### What is **not** changed, and was checked

`ctime()`'s `cp+4` / `cp+20` slicing is correct as v7 wrote it:
[`ctime.c`](../../lib/libc/gen/ctime.c) builds the canonical 26-character
`"Day Mon dd hh:mm:ss yyyy\n"`, and those offsets depend on the column positions, not on the
century patching v7 did in the same buffer.

`NFILES` stays 1024. At 12 words a `struct lbuf` plus a `malloc` header, the heap gives out at
roughly 1,500 entries in a 28-page address space — so 1024 is, by luck, the right order of
magnitude for this machine, and both limits (`ls: too many files`, `ls: out of memory`) are
graceful.

## Testing

Only the size check, and deliberately: **`ls` cannot be tested under `b6sim`.** It reads a
directory with `fread()` and parses `struct direct` out of it, and `b6sim` maps `read()` onto
the host, where a directory descriptor refuses to be read at all. That is the same limitation
[`cmd/sh/README.md`](../sh/README.md) records for `expand.c`'s globbing, and it clears the day
the command runs on the real kernel under SIMH, which it now does: `ls /bin` is one of the
stages of [`kernel/test/console`](../../kernel/test/console.ini)'s dialogue, and the six names it
prints are the assertion.

`ls.1` is the v7 manual page, corrected in place. It never stated a block size at all — a gap,
since v7's was 512 and this one is not — so task C4a gave it the `BLOCKS ARE 1024 BYTES` section
its three siblings have.
