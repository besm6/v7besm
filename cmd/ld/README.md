# b6ld — linker for BESM-6 `a.out`

Combines BESM-6 object files and archives into an executable. The command language is
documented in [doc/Linker_Manual.md](../../doc/Linker_Manual.md); the design primer — what a
segment is here, what relocation means on a word machine, how the two passes divide the work
— is the block comment at the head of [`ld.c`](ld.c), and that is the file to read first.

The on-disk format is [`cross/besm6/b.out.h`](../../cross/besm6/b.out.h), serialized by
[`cmd/libaout`](../libaout). Build with `make`; the engine is covered by the GoogleTest suite
in [`test/`](test), which links it in-process and shells out to `b6as`/`b6ar` for fixtures.

## Source layout

| File | Responsibility |
| --- | --- |
| `ld.c` | The design primer, the global state, `error()`, `read_header()`, `assign_addresses()`, and `ld_link()`. |
| `pass1.c` | Pass 1: the constant pool (`load_constants`), the per-file scan, and command-line parsing. |
| `pass2.c` | Pass 2: re-read each file, build the local→global symbol map, emit the relocated segments. |
| `symtab.c` | The global symbol table: hashing, insertion, symbol relocation, the `N_FN` file symbols. |
| `library.c` | Opening an input, `-lNAME` search, archives plain and randomized, the `__.SYMDEF` sweep. |
| `reloc.c` | `relocate_halfword()` — the one routine that patches an address field — and its two callers. |
| `output.c` | The scratch buffer per segment, the header, and the concatenation at the end. |
| `main.c` | The thin command-line wrapper: usage, signal handlers, `ld_link()`. |
| `intern.h` | The engine state, the table capacities, and the size profile. |

## Building for the BESM-6

These same eight sources — plus the `cmd/libaout` files the linker calls — are built a
**second** time, by the `b6*` cross toolchain, into `build/rootfs/usr/bin/ld`: the linker that
runs on the machine, and with [`../as`](../as) and [`../cpp`](../cpp) the point of task **C9b**
in [../TODO.md](../TODO.md). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the build
machinery, and there is **no second copy of any source**: what differs is a size profile in
[`intern.h`](intern.h), keyed on the `besm6` macro `b6cpp` always predefines.

**It links the whole kernel, and it links itself.** Every image on this disk, plus
`build/kernel/unix`, comes out byte-for-byte identical to the host `b6ld`'s — see "Testing the
native build" below for the command.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `NSYM` (global symbols) | 2000 | 1024 | the measured peak is 374 |
| `NSYMPR` (per-file local map) | 1000 | 256 | the measured peak is 105 |
| `NCONST` (merged const pool) | 4096 | 2048 | **not** cut to the measurement — see below |
| `NCINDEX` (`newindex[]`) | 4096 | 1024 | one object's const words; the peak is 386 |
| `RANTABSZ` (`__.SYMDEF` entries) | 1000 | 512 | the measured peak is 331 |
| `NLIBDIR` (`-L` path) | 32 | 8 | no link here passes more than two |
| `LDBUFSIZ` (stdio buffer, bytes) | *(BUFSIZ)* | 1024 | twelve streams; see the heap, below |

**The sizes are measured, not guessed.** Linking this repo's own kernel, libc, `sh`, `sed`,
`cpp`, `as` and `ld`, the high-water marks are **387 pooled constants, 386 const words in one
object** (`besm6.o`, whose const segment is the BESM-6 interrupt vectors), **374 global
symbols, 105 local-symbol references and 331 ranlib entries**. The profile leaves two to five
times that.

`NCONST` is the one deliberately **not** cut to the measurement. It is what caps the merged
const segment, and `CONSTTOP` (`07777`) is the architectural ceiling above it, so a small value
here would be a limit on what the native linker *can* link rather than on what it has been
asked to link. Packing `struct constab` is what pays for keeping it large.

### Where the ceilings bind

1. **No struct may exceed 4,096 words.** A member is named by a 12-bit offset from a base
   register and there is no longer form, so `b6as` refuses the offset — *"short address out of
   range"*, reported against a line of generated assembly that does not read like the cause —
   and nothing downstream can rescue it. `struct linker` was **~50,400 words**: twelve times
   the ceiling, and half again the whole 32,768-word address space. The fix is not to shrink it
   but to take the eight big arrays *out*; they are at file scope in [`ld.c`](ld.c) now, where
   an index register reaches them at any size. [../README.md](../README.md) §6 is the general
   account and [../cpp/README.md](../cpp/README.md) the first worked example.

2. **`newindex[]` was sized by the whole link, not by one file.** It maps a file's const-word
   index to the pooled word it ended up at, and it was indexed from a running base covering
   every input at once — 16,384 words, more than half the address space, for a table whose
   largest single object needs 386. It is **per-file** now, rebuilt as each file is reached.
   Nothing was lost: both readers already refused an index outside the current file's window
   and said so, because a const word merged away is scattered through the pool and there is
   nothing to extrapolate past either edge. Pass 2 re-derives the map by calling
   `load_constants()` again — the same code over the same bytes with `ld.nconst` back at the
   file's base, so it comes out identical **by construction** rather than by agreement. What it
   buys is a bound that no longer grows with the number of files linked.

3. **`struct constab` packs.** Its four fields are 24-bit half-words read straight off the disk
   — the const word's two halves and their two relocation records — so each pair fits one
   48-bit word instead of four. At `NCONST` that is the difference between 4,096 words and
   8,192, and it is what lets `NCONST` stay generous.

4. **The heap, which `rootfs_ld_size` cannot see.** The linker holds **twelve** stdio streams
   open at once: two handles on the input file, the output, one scratch file per segment, one
   for the symbol table, and under `-r` one more per segment's relocation records. At the
   default `BUFSIZ` — 3,072 bytes, one disk block, **512 words** — that is 6,144 words of heap
   against the ~4,700 the image leaves. The program would link and then die on a real link with
   *"out of memory"*. `shrink_buffer()` ([`output.c`](output.c)) gives each stream
   `LDBUFSIZ` instead, and the twelve cost ~2,050 words. What it buys back is more `read(2)`/
   `write(2)` calls, which is the right trade when the alternative is not running at all.

5. **28,672 words of image**, which this fills to **23,951** — const 133, text 8,248, data 627,
   bss 14,943. The ~4,700 words left are not slack but the heap budget above.

### The stack is not a problem here

Unlike [`../as`](../as) and [`../cpp`](../cpp), the linker needed no bound of its own: **it has
no recursion at all**, and not one function holds a local array. The deepest chain is
`pass2` → `relocate_file` → `relocate_object` → `relocate_segment` → `relocate_halfword` →
`lookup_local`, six frames of scalars. Read out of `build/cmd/ld/rootfs/ld.dis`:

| | words |
| --- | ---: |
| `main` | 60 |
| `ld_link` | 29 |
| `pass1` | 144 |
| `scan_object` | 245 |
| `load_constants` | 72 |
| `assign_addresses` | 212 |
| `pass2` | 43 |
| `relocate_file` | 41 |
| `relocate_object` | 284 |
| `relocate_segment` | 11 |
| `relocate_halfword` | 82 |
| `lookup_local` | 28 |

The deepest path costs 60 + 29 + 43 + 41 + 284 + 11 + 82 + 28 = **578 words** of the 4,096.
This is worth stating because kernel task 39 assumed the
opposite: raising `USTKPAGE` would take 4,096 words *off* the image ceiling, which is exactly
what this program is short of. C9b is evidence against that change, not for it.

### Two things that were silently wrong

- **`FMAGIC` had no `U` suffix** and `a_magic` was a signed `word_t`
  ([`cross/besm6/b.out.h`](../../cross/besm6/b.out.h)). The constant is 47 bits and a native
  `int` is 41, so the native linker would have written a magic number that neither the kernel
  nor `b6sim` accepts. The kernel had already hit this and fixed it in its own copy
  ([`include/sys/param.h`](../../include/sys/param.h)); the two spellings are now
  character-identical, which they must be — `b6cpp` rejects a redefinition whose replacement
  text differs at all, and a native program can include both headers.
- **`b6lower` ignores designated initializers** and initializes *positionally*. `struct linker
  ld = { .basaddr = BADDR, ... }` put those values into `filhdr`'s first members. It compiles
  without a word. `ld_link()` assigns them now.

`mkstemp()` also does not exist in this libc; the scratch files are `tmpfile()`, which is
exactly the `fopen("w+")` + `unlink()` the old code spelled by hand — and that removed a real
latent bug on the way, a `char tfname[14]` initialized with the 14-character
`"/tmp/ldaXXXXXX"`, so the NUL was dropped and `mkstemp` read past the array.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_ld_link`, `_r`, `_x`, `_n`** — the host `b6ld` and the native `ld` over one pair of
  objects, the two images compared **live**, plain and then under `-r` (which opens all ten
  scratch streams), `-x` and `-n`. Two linkers built from one source must produce the same
  image byte for byte; a checked-in `a.out` could not express that, because the day
  `relocate_halfword()` changes a live comparison *requires* the change to land identically on
  both targets. The fixtures are assembled here rather than checked in as objects, and they
  duplicate `#expr` literals across the two files on purpose: merging those is what
  `load_constants()` and the `newindex[]` map are for.
- **`cmd_ld_*`** — ordinary `b6sim` cases with a checked-in `.expected`, which is the other
  half: they pin the diagnostics and the exit status, which two linkers wrong in the same way
  would not.

`rootfs_ld_size` (registered by `b6_prog()`) is the third: it holds the program under 28,672
words and its top symbol under 32,767.

The stronger check, by hand, is the real workload — **the kernel, and every program on this
disk, come out identical**:

```sh
cd build/kernel
b6ld besm6.o libunix.a -o k.host -L$B6LIB -lruntime
b6sim ../rootfs/usr/bin/ld besm6.o libunix.a -o k.native -L$B6LIB -lruntime
cmp k.host k.native
```
