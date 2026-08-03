# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

A port of **Unix v7 to the BESM-6**, a Soviet 48-bit-word mainframe. Three halves:

- **`kernel/` + `include/`** — the v7 kernel (from Robert Nordier's v7/x86 port; see
  `COPYRIGHT`), cross-built by this repo's toolchain and **booting under SIMH**: it mounts
  `root3072.disk`, execs the real `/etc/init`, runs getty/login on both Consul typewriters,
  and gives multi-user shells. It swaps, does `/dev/mem`, and the image fscks clean.
- **`cmd/`** — the BESM-6 toolchain (host tools): compiler driver, assembler, linker,
  binutils, preprocessor, disassembler, and `b6sim`, a user-level a.out simulator.
- **`lib/`** — cross-built `libc.a`, `libm.a`, `libtermcap.a`, `libcurses.a`, `crt0.o`.

Plus **native BESM-6 programs** under `cmd/` (`sh`, `ed`, `fsck`, `ls`, … — see the
`b6_prog()` calls) staged into `build/rootfs/` for the root image.

**The narrative lives in the per-directory READMEs, and that is where to look before
touching anything**: `kernel/README.md` (memory model, hardware rules), `kernel/TODO.md`,
`cmd/README.md` (the porting recipe for v7 userland), `cmd/TODO.md`, `include/README.md`,
`lib/lib*/README.md`, and a `README.md` in most `cmd/<prog>/` directories.

External, not in this repo:

- C cross-compiler: https://github.com/besm6/c-compiler/ — supplies `libruntime.a` (the `b$*`
  helpers) and the ten freestanding headers (`stddef.h`, `stdarg.h`, …, `besm6.h`).
- SIMH full-machine emulator: https://github.com/besm6/simh/tree/master/BESM6/ — **the target
  the kernel boots on**. See `doc/Simh_Simulator.md`, `doc/Besm6_Peripherals.md`.

## Building

**One CMake project** behind a thin top-level `Makefile`; `kernel/` and `kernel/test/`
Makefiles are wrappers over the same `build/` tree. From the repo root:

```sh
make            # configure into build/ and build everything
make test       # build unit tests without running them
make run        # run all tests via ctest
make install    # install b6* tools, include/, and the archives into ~/.local
make clean; make debug; make    # reconfigure as Debug (default RelWithDebInfo)
```

**Do not** invoke `cc`/`clang` by hand or run `cmake --build` directly — always go through
the top-level `make` targets. Requires CMake + a C++17 host compiler; GoogleTest is fetched
at configure time, cppcheck runs when installed, everything builds `-Wall -Werror -Wshadow`.

Tools install `b6`-prefixed: `b6cc`, `b6as`, `b6ld`, `b6cpp`, `b6disasm`, `b6sim`, `b6fsutil`,
plus `b6ar`/`b6nm`/`b6size`/`b6strip`/`b6ranlib`/`b6lorder`. `include/` installs to
`<prefix>/share/besm6/include`, the one system header tree (hosted half ours, freestanding
half the compiler's). `kernel/` + `lib/` + the native programs are guarded on `libruntime.a`
being installed; without it only the `cmd/` host tools build.

Link order is a contract — **`b6ld` scans each archive once, in order**:
`crt0.o … -lcurses -ltermcap -lc -lruntime`. The kernel takes `-lruntime` alone (own `printf`
in `kernel/prf.c`, no libc).

### Native BESM-6 programs

One `b6_prog()` call in `scripts/BesmCross.cmake` per program:

```cmake
b6_prog(init DEST etc/init SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/init.c)
```

It stages the program into `build/rootfs/` and registers a `rootfs_<name>_size` ctest that
guards the two **user** address-space ceilings: `const+text+data+bss` ≤ **28,672 words** (32
pages less the 4-page stack at `070000`), and no relocatable symbol above word **32,767** (a
15-bit pointer's reach). Both failures are otherwise silent.

Two ceilings it does **not** guard, and both bind in practice: **no struct may exceed 4,096
words** (a member is a 12-bit offset from a base register — move the big arrays to file scope),
and the **4,096-word stack**, where a long function costs 1.5–2 words per source line of
temporaries before any array. `cmd/README.md` §6 is the account and `cmd/cpp` the worked example.

`cmd/cpp` is the one program built **twice**: as the host tool `b6cpp` and, from the same
sources, as `build/rootfs/usr/bin/cpp` (`cmd/cpp/rootfs/`, task C9a — the first step of C9,
self-hosting). It is the only place the ceilings actually bound a program: its BESM-6 size
profile in `cmd/cpp/defs.h` is deliberately below the C11 §5.2.4.1 minima, keyed on the `besm6`
macro `b6cpp` always predefines. `cmd/cpp/README.md`, "Building for the BESM-6", has the
measurements. A second native build of a host tool needs its own subdirectory, since `cmd/<x>`
is added above the `B6RUNTIME_LIB` guard where `b6_prog()` does not yet exist.

`build/rootfs/` is staged only, never installed. **`root.manifest`** at the tree top describes
the image; paths resolve against `b6fsutil`'s working directory (`build/kernel/test`). Modes
(`04755` setuid on `mkdir`/`mv`/`rmdir`) and the one hard link (`/bin/[` → `/bin/test`) live
there, not in `build/rootfs/`. `etc/` stages the static `group`, `motd`, `passwd`, `rc`,
`termcap`, `ttys`; `lib/test/` stages `usr/test/*`.

**Porting v7 userland**: read `cmd/README.md` (the eleven-point recipe), then
`cmd/sh/README.md` and `cmd/ls/README.md`. `b6parse` is **strict C11** — no implicit `int`,
no K&R parameter lists — but the mechanical part is not what bites. What bites is that v7
assumes `int` and `char *` are the same thing:

- a flag packed into bit 0 of a pointer, a mask rounding to a word assuming `BYTESPERWORD`,
  and pointer casts that *floor* rather than round.
- **`<` between two `char *` orders them correctly** — the compiler lowers it through
  `b$pdiff`. It did not before 2026-06-17, and much of this tree was ported under the old
  rule; `cmd/README.md` §2 is the account and it is history now, not a hazard.
- `long` is one word; `BSIZE` is 3072 bytes but tools report 1024-byte blocks; `DIRSIZ` 18.

### Kernel

```sh
cd kernel && make          # produces `unix`, unix.nm, unix.dis
make run                   # boot under SIMH (besm6 unix.ini)
```

Built with the **in-tree** tool targets, so a rebuilt `b6as` relinks with no `make install`.
`make` prints `b6size -w unix`: the image **must end below `054000`** (`KEND`). Everything is
archived into one `libunix.a` (`b6ranlib`'s index resolves driver→core back-references in one
scan). **`besm6.o` must come first in `OBJ`** so its const contribution pins the
interrupt/extracode vectors at their fixed addresses. `brz.s` and `syscall.c` are separate
files because `kernel/test/` links them directly.

Header dependencies are **coarse, not computed** (`b6cc`/`b6cpp` have no `-M`): every object
depends on every `include/sys/*.h`. Adding a **new header file** needs a re-configure, since
`file(GLOB)` runs at configure time.

### Tests

**Kernel tests run on real SIMH** (`cd kernel/test && make test` — that target rebuilds
`root.img` first; plain `ctest -L kernel` tests a stale image). Each is a standalone BESM-6
program linking kernel objects against a hand-built environment plus a `.ini` that loads it,
runs it, and asserts on machine state; `b6sim` cannot substitute. `mmutest` is the one to
copy. Sources are compiled *into* `kernel/test/` via `b6_find_src()`/`b6_test_obj()` with
`-DKERNEL`, into per-program object dirs (shared outputs would race under `make -j`).

- **Run every MMU test with `set mmu cache`** — the БРЗ hazards are invisible otherwise.
- Several tests boot the whole kernel (`login`, `multi`, `session`, `files`, `libtest`,
  `swap`, `utils`, `edit`, `fsinfo`, `dd`, `mkfs`, `fsck`, `mount`, `console`). They hold
  one resource lock, so they run one at a time — about seventy seconds of serial wall clock,
  the critical path of the whole suite. **They are labelled `weekly` and are not in the
  daily suite**: `make run` and `cd kernel/test && make test` exclude them (`-LE weekly`),
  and `make weekly` (top level or in `kernel/`) is what runs them. **It is opt-in and is
  not a pre-commit gate** — do not run it as acceptance criteria; `make run` and `cd
  kernel/test && make test` are what a change is accepted on, and the latter rebuilds
  `root.img` first. `boot` is the exception, left in the daily suite as a one-second smoke
  test that the kernel still reaches a shell prompt.
- `console` and `edit` are **DISABLED** on top of that — simulator flakiness,
  `kernel/TODO.md` task 35 — so `make weekly` skips them too; do not run them on your own
  initiative.
- **The libc suite runs twice on purpose**: under `b6sim` (label `lib`) and off the image
  under the booted kernel (label `kernel`), diffed against the *same* `.expected`. Disagreement
  means one harness is wrong. `b6_libtest()`'s `SIMONLY`/`IMAGEONLY` mark the exceptions.
  Adding a program = one `b6_libtest()` call + `lib/test/progs.cmake` + `root.manifest` +
  a line in `kernel/test/libtest.sh`.
- ctest labels: `kernel` (SIMH), `lib` (b6sim), `rootfs` (size checks), `sh`, and `weekly`
  — a *second* label on the kernel tests that boot, so `-L kernel` still names the whole
  SIMH suite and `-LE weekly` takes the slow half out of it.
- Every `cmd/` tool has a GoogleTest suite under `cmd/<tool>/test/`; `cmd/cpp/test/` is a full
  C11 (N1570) conformance suite built on the `PreprocessorTest` fixture.
- **A lone unexpected failure is usually the harness, not the change.** The suite runs in
  parallel and both simulators are timing-sensitive, so a case that fails under `make run` and
  passes on its own was a flake. Re-run it alone, then move on — do not spend the turn on it.

## Architecture notes

**BESM-6 is word-addressed.** The addressable unit is one 48-bit word; no sub-word
load/store. `CHAR_BIT == 8` but six chars pack into a word, so **`sizeof(int) == 6`** and
addresses are word indices. Bits number right-to-left from 1. Numbers are octal. No IEEE 754.

**Kernel memory model** (full account in `kernel/README.md` — read it before touching
anything memory-related, and keep it current):

- The kernel **runs unmapped** (БлП = БлЗ = 1), so a kernel address *is* physical; kernel
  image + u-area + buffer cache must fit the low 32 pages, since supervisor instruction fetch
  is never mapped.
- Two fixed physical areas, absolute symbols rather than bss (devices transfer to *physical*
  addresses): the **u-area, two pages at `074000`**, and **`buffers[NBUF][BSIZE]` at
  `054000`–`074000`**. Hence `KEND == BUFBASE == UBASE - NBUF*BSIZEW`. `NBUF` (16) must stay
  above `NMOUNT` (8): every mount pins one buffer until it is unmounted.
- **РП always holds the current process's map**, so a trap switches nothing. The shadow map
  is `u.u_upt[8]` — the hardware registers cannot be read back. `sureg()` (`kernel/utab.c`)
  loads the space in twelve `рег`s.
- **Drain the БРЗ write cache before every РП write** — `drainbrz()` in `kernel/brz.s`. It
  **cannot be written in C** (the nine stores must be consecutive; `b6cc` spills the pointer
  through a frame slot). Verified by disassembly; don't re-litigate. A device also reads
  memory rather than the write cache, so drivers must drain before a write exchange.

**SIMH device model** (details in `doc/Besm6_Peripherals.md`, `doc/Memory_Mapping.md`):
no I/O address space and no channel programs — devices are reached by two supervisor
instructions naming a register via the effective address with data in the accumulator:
`033 «увв»` (`ext`) for peripherals, `002 «рег»` (`mod`) for CPU registers; one address bit
selects read vs write (`04000` / `0200`). Devices answer through ГРП (48-bit) and ПРП
(24-bit, delivered via `GRP_SLAVE`); some ГРП bits are wired and clear only at the device.
Mass storage exchanges in zones of `8 + 1024` words, service words landing at a fixed low
address per controller. The MMU is eight write-only registers (РП `002 020`–`027`, РЗ
`002 030`–`033`), 32 pages of 1 Kword over 512 Kwords.

**Object format**: BESM-6 `a.out` in `cross/besm6/b.out.h` (`FMAGIC`/`NMAGIC`, separate
const/text/data/bss sizes), serialized by `cmd/libaout` with a **6-byte word** (`W == 6`, two
3-byte big-endian half-words, high first). Archive headers: `cross/besm6/ar.h`, `ARHDRSZ ==
60`. `b6as` uses AT&T syntax with Madlen mnemonics; `b6disasm` prints the same by default
(`-b` for the Cyrillic BEMSH dialect).

**`b6cc` is a driver**, not a compiler: `b6cpp` → `b6parse` → `b6lower` → `b6codegen` →
`b6as` → `b6ld`, the middle three from the external c-compiler. `-E`/`-S`/`-c` as usual;
`-O`/`-g` are no-ops. **`b6sim`** loads one a.out, interprets it, and traps `$77 N` to run
v7 syscall `N` on the host (numbers from `kernel/sysent.c`; args below `r15`, last in the
accumulator, result in the accumulator, errno in `r14`).

**`include/`** is the v7 header tree, C11-ified. **Every header stands alone and includes what
it uses**, so include lists are sets, not sequences. Two traps: `<sys/param.h>` must stay
`#define`-only (`kernel/*.S` includes it, and there is no `__ASSEMBLER__`); and **`b6cpp`
rejects a macro redefinition whose replacement text is not character-identical**, so two
headers defining a name must agree character for character, alignment included.

**Read `doc/` before touching codegen, the assembler, or anything ABI-related** — these are
authoritative and kept current:
`Besm6_Instruction_Set.md` (opcodes, registers), `Besm6_Calling_Conventions.md` (args in
direct order, last in the accumulator, r14 = negative arg count, r13 = return address),
`Besm6_Data_Representation.md`, `Besm6_Runtime_Library.md` (the `b$*` helper convention),
`Intrinsics.md` (the twelve `<besm6.h>` intrinsics that let `kernel/dev/` be written in C —
read before writing any driver), `Besm6_Peripherals.md`, `Memory_Mapping.md`,
`Unix_Context_Switch.md` (the four gates, the 21-word trap frame, `save()`/`resume()`),
`Unix_V7_System_Calls.md`, `Kernel_Assembly_Routines.md`, `Dubna_Context_Switch.md`,
`Assembler_Manual.md`, `Linker_Manual.md`, `Archiver_Manual.md`, `File_Magic.md`,
`Aout_Simulator.md`, `Simh_Simulator.md`.

## Conventions

- clang-format (`.clang-format` at repo root), but **not over `include/`** — re-spacing a
  `#define` changes what `b6cpp` compares on redefinition. The tree was formatted with a
  clang-format older than 22, so re-running it produces some alignment churn. `TypeNames:`
  lists the `<sys/types.h>` scalars, without which `(caddr_t)&x` is misread as a bitwise and.
- Comments and identifiers are frequently in **Russian**; match the surrounding language.
- Build artifacts (`*.o`, `*.a`, `*.i`, `*.ast`, `*.yaml`) are git-ignored.
- `scripts/vscode-besm6/` is a grammar-only VSCode extension; its mnemonic tables are
  transcribed from `cmd/as/tables.c` and `cmd/disasm/dis.c`, so adding a mnemonic there means
  adding it here too.
