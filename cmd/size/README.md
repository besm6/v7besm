# b6size — segment sizes of a BESM-6 `a.out`

Prints the four segment sizes of an object file or executable and their sum, in decimal and in
octal: `size [-w] file ...`. Without `-w` the columns are **bytes**, as v7's are; with it they
are **words**, which is the unit everything else on this machine counts in and the one the
address-space ceilings are written in.

```
const   text    data    bss     dec     oct
95      4705    288     1059    6147    14003   build/rootfs/usr/bin/nm
```

There are five columns where v7 had three. The BESM-6 `a.out`
([`cross/besm6/b.out.h`](../../cross/besm6/b.out.h)) has a **const** segment the PDP-11 had
not — this machine's read-only literals live there — and it is `a_const + a_text + a_data +
a_bss` that `scripts/check-size.sh` compares against 28,672 for every program on the disk. That
is what `-w` exists for.

Build with `make`; the engine is covered by the GoogleTest suite in [`test/`](test), which
links it in-process.

## Source layout

| File | Responsibility |
| --- | --- |
| `size.c` | The whole engine: flag parsing, and `size()`, which reads one header and prints one row. |
| `main.c` | The thin command-line wrapper: hands `argv` to `size_run()` and forwards its exit code. |
| `size.h` | `size_run()`, the one entry point, so the tests can call it repeatedly in one process. |

## Building for the BESM-6

These same sources — plus the `cmd/libaout` files `size` calls — are built a **second** time, by
the `b6*` cross toolchain, into `build/rootfs/usr/bin/size`: task **C9c** in
[../TODO.md](../TODO.md), with [`../nm`](../nm), [`../strip`](../strip) and
[`../disasm`](../disasm). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the build
machinery, and there is **no second copy of any source**.

**There is no size profile, and that is worth saying rather than leaving to be noticed.** This
is the only program of the four — and the only one of the seven the self-hosting task has put
on the disk so far — whose sources are *character for character* what the host tool is built
from, with not one `#ifdef besm6` anywhere. It reads a header, prints six numbers and holds
nothing: no table, no buffer of its own, no recursion, and a heap that is one stdio stream. It
was done first for exactly that reason, as the proof that the build machinery works before any
program with a measurement in it was attempted.

The program is **5,068 words** (89 const, 3,739 text, 205 data, 1,035 bss) with its top
relocatable symbol at 5,076 — against ceilings of 28,672 and 32,767. It is the smallest thing
the toolchain has put on this disk, and about a fifth of it is its own arithmetic: the rest is
`stdio`, which [../README.md](../README.md) §6 says is what dominates a small program.

### The stack

`main` → `size_run` (39 words) → `size` (59) → `printf` (3) → `_doprnt` (**281**) is **382
words** of the 4,096, three quarters of it in the one routine that formats a number.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_size_bytes`, `_words`** — the host `b6size` and the native `size` over one object,
  the two listings diffed **live**, in bytes and then in words. The fixture's four segments are
  deliberately all different — 3, 4, 5 and 6 words — so that a transposed column cannot pass and
  neither can an unread one. Two `size`s built from one source must print the same row; a
  checked-in `.expected` could not express that, since the day `a_bss` is computed differently
  a live diff *requires* the change to land identically on both targets.
- **`cmd_size_badflag`, `_noargs`, `_notfound`** — ordinary `b6sim` cases with a checked-in
  `.expected`, which is the other half: they pin the output, the diagnostics and the exit
  status, which two `size`s wrong in the same way would not. `_notfound` is the one that pins
  something easy to get wrong by accident: a file that is not there is reported and the exit
  status is still **0**, v7's behaviour.

`rootfs_size_size` (registered by `b6_prog()`, and not a typo) is the third: it holds the
program under 28,672 words and its top symbol under 32,767 — using, by then, the host `b6size`
to measure the native one.
