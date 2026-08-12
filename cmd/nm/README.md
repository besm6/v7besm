# b6nm — symbol table lister for BESM-6 `a.out`

Prints the symbol table of an object file, an executable or an archive: one line per symbol,
its value in octal, its class letter, its name. `nm [-goprun] file ...`, v7's flags exactly —
`-n` sorts by value, `-g` keeps externals, `-u` keeps undefined, `-r` reverses, `-p` does not
sort at all, `-o` puts the file name on every line.

The on-disk format is [`cross/besm6/b.out.h`](../../cross/besm6/b.out.h) (and
[`ar.h`](../../cross/besm6/ar.h) for an archive), read through
[`cmd/libaout`](../libaout). Build with `make`; the engine is covered by the GoogleTest suite
in [`test/`](test), which links it in-process.

## Source layout

| File | Responsibility |
| --- | --- |
| `nm.c` | The whole engine: flag parsing, the archive walk, `nm()` — which reads one symbol table, filters it, sorts it and prints it — and `compare()`. |
| `main.c` | The thin command-line wrapper: hands `argv` to `nm_run()` and forwards its exit code. |
| `nm.h` | `nm_run()`, the one entry point, so the tests can call it repeatedly in one process. |

The class letters are `l` const, `t` text, `d` data, `b` bss, `a` absolute, `c` common, `u`
undefined and `f` file, upper-cased when the symbol is external. `l` for const is this
machine's: the BESM-6 `a.out` has a fourth segment the PDP-11 had not.

## Building for the BESM-6

These same sources — plus the `cmd/libaout` files `nm` calls — are built a **second** time, by
the `b6*` cross toolchain, into `build/rootfs/usr/bin/nm`: task **C9c** in
[../README.md](../README.md), with [`../size`](../size), [`../strip`](../strip) and
[`../disasm`](../disasm). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the build
machinery, and there is **no second copy of any source**.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `QUANT` (symbol-table growth step) | 2048 | 256 | one `malloc` page; the measured peak is 623 symbols |

**One number, and it is a heap number** — which is the point, because `rootfs_nm_size` cannot
see the heap at all. `nm()` grows one `struct nlist` array by `QUANT` entries at a time and
`realloc()`s, and a `struct nlist` is **four words** here (three `word_t` and a `char *`). So
the host's 2048 asks for 8,192 words before the first symbol is stored, and the first growth
wants 16,384 more while the old 8,192 are still live — 24,576 words against a heap that is
what remains of 28,672 after the program itself. 256 entries is 1,024 words, one `malloc`
page exactly ([../README.md](../README.md) §6: measure the break, not the request).

**Measured, not guessed.** Over this repo's own output the high-water mark is **623 symbols**,
in `build/kernel/unix`; `/usr/bin/cpp`, `/usr/bin/as` and `/usr/bin/ld` are 508, 484 and 423,
and `/bin/sh` 491. In an *archive* it is far smaller — 139, in `libcurses.a(cr_tty.o)` — because
`nm()` frees the table between members as well as between files, so what is live at once is one
member's table and never the library's. 623 symbols reaches the third chunk: a 3,072-word
allocation over a 2,048-word one, and 5,120 words at the moment `realloc` holds both.

The program is **6,147 words** (95 const, 4,705 text, 288 data, 1,059 bss) with its top
relocatable symbol at 6,155 — against ceilings of 28,672 and 32,767.

### The stack

No recursion, and no function holds an array. The deepest chain is `main` → `nm_run` (108
words) → `nm` (146) → `printf` (3) → `_doprnt` (**281**), which is **538 words** of the 4,096;
the sort branch, `qsort` (11) → `compare` (15), is shallower. `_doprnt` being half the total is
[../README.md](../README.md) §6's rule about what a program prints with, in one line.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_nm_obj`, `_num`, `_rev`, `_nosort`, `_globl`, `_undef`, `_prepend`, `_files`,
  `_archive`, `_archnum`** — the host `b6nm` and the native `nm` over one fixture, the two
  listings diffed **live**: every sort order, both filters, both output shapes, and the same
  objects again inside an archive, which is the only way to reach `fgetarhdr()` and the
  per-member loop. Two `nm`s built from one source must print the same table; a checked-in
  `.expected` could not express that, since the day a class letter changes a live diff
  *requires* the change to land identically on both targets.
- **`cmd_nm_badflag`, `_noargs`, `_notfound`** — ordinary `b6sim` cases with a checked-in
  `.expected`, which is the other half: they pin the diagnostics and the exit status, which two
  `nm`s wrong in the same way would not.

**No fixture may contain two symbols with the same sort key.** `qsort()` is not stable and the
two builds link different ones — the host's libc and [`lib/libc/gen/qsort.c`](../../lib/libc/gen/qsort.c) —
so a repeated name, or a repeated value under `-n`, comes out in one order here and another
there. Real objects do have such ties (`kernel/unix` has seven `_str0`s); listing one by hand
is a fine check and is not a case for the suite.

`rootfs_nm_size` (registered by `b6_prog()`) is the third kind: it holds the program under the
two address-space ceilings. What it cannot hold is `QUANT`, which is why that number is
measured above rather than merely chosen.

The check by hand is the real workload — the machine reading the tools that built it:

```sh
cd build
b6sim rootfs/usr/bin/nm -n kernel/unix | wc -l      # 623, and no `out of memory'
b6sim rootfs/usr/bin/nm lib/libc/libc.a | wc -l     # 1,862 over 188 members
```
