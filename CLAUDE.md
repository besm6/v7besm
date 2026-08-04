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

Plus **native BESM-6 programs** under `cmd/` — 92 of them plus one hard link on the image
(`init`, `getty`, `login`, `sh`, the file-management and account set, the filesystem tools,
two dozen text filters, `ed` and `novi`, `tar`, the kernel-inspection set, and ten of the
toolchain built a second time; six are setuid root) — staged into `build/rootfs/` for the root
image. `root.manifest` is the roster; `b6_prog()` calls are how each gets there.

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
make run        # run the daily suite via ctest (everything less the `weekly' label)
make weekly     # the tests that boot the kernel under SIMH -- opt-in, not a gate
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

**Ten programs are built twice** — `cmd/cpp`, `as`, `ld`, `nm`, `size`, `strip`, `disasm`,
`ar`, `ranlib` and `cc` — as the host tools `b6cpp`/`b6as`/… and, from the same sources, as
`build/rootfs/usr/bin/{cpp,as,ld,nm,size,strip,disasm,ar,ranlib,cc}` (`cmd/<x>/rootfs/`; tasks
C9a–C9e, self-hosting), plus `cmd/lorder`, a shell script `configure_file`d twice on the one
`nm` it names. **The machine assembles, links, archives and indexes its own programs, reads
back what it built, and drives the chain from one command**: the native `as` and `ld` reproduce
the host tools' objects and images byte for byte — the whole kernel and the toolchain itself
included — the native `ar` and `ranlib` build `libc.a`, archive and `__.SYMDEF` both, byte for
byte too, and `cc -o hello hello.s` on the image links against `/lib/crt0.o` and `/lib/libc.a`
and produces a runnable `a.out`. `ar`/`ranlib` are the only two that link the **full**
`B6_LIBAOUT_SOURCES` (the four file-descriptor routines `getarhdr`/`getint`/`putarhdr`/`putint`
exist for them and for nothing else); `cc` is the only one that links **no** `libaout` at all,
a driver reading no `a.out`.

**What `cc` cannot do is compile C**, and it says so rather than reporting a missing file:
`b6parse`, `b6lower` and `b6codegen` are the external c-compiler's and cannot be built for the
target. `-E`, `-c` on a `.s`/`.S`, and a link all work. Its three target changes are **all in
libc** — `strdup()`, `mkstemps()` and `atexit()`, the last declared in `<stdlib.h>` since these
headers were written and never implemented (`lib/libc/gen/`, and `cuexit.c` on why `exit()`
reaches both it and the stdio flush through a pointer). The image also carries
**`/lib/{crt0.o,libc.a,libruntime.a}` and the whole `/usr/include`** — staged by the top-level
`CMakeLists.txt`'s `B6_STAGE_*` lists, which `kernel/test/CMakeLists.txt` reuses so that a file
staged and not listed cannot slip past `root.img`.

The first three are the only places the ceilings actually bind, and each carries a BESM-6 size
profile keyed on the `besm6` macro `b6cpp` always predefines — `cmd/cpp/defs.h` (below the C11
§5.2.4.1 minima), `cmd/as/as.h`, `cmd/ld/intern.h`. Of the other seven only four changed
anything: `cmd/nm/nm.c`'s `QUANT` (a *heap* step `rootfs_<name>_size` cannot see — a `struct
nlist` is four words), `cmd/strip/strip.c`'s `BUFSZ` (a *stack* array, 8,192 bytes being a
third of the stack), `cmd/ranlib/ranlib.c`'s `TABSZ` — not an address-space cut but a match
to `b6ld`'s own `RANTABSZ`, so the machine cannot write an index its linker refuses to read —
and `cmd/cc/cc.c`'s, which is not a size profile at all but a **path** profile: `/usr/bin`,
`/usr/include` and `/lib` in place of `share/besm6` under `~/.local` or `/usr/local`, and no
`b6` prefix on a sub-tool's name. The `B6CPP`-style environment overrides are deliberately not
keyed, being how a test points either build at a tool off its search path.
`size`, `disasm` and `ar` are character-for-character the host build; `ar` is the one that says
how much of the difficulty was the three big ones, since it keeps all its state in a struct and
that struct is 190 words. Each README's "Building for the BESM-6" has the measurements. A
second native build of a host tool needs its own subdirectory, since `cmd/<x>` is added above
the `B6RUNTIME_LIB` guard where `b6_prog()` does not yet exist.

**Bulk I/O moves a word, not a byte, and the reason is the one fact this machine keeps
teaching**: six chars pack big-endian into a 48-bit word, and a word on disk is six
big-endian bytes, so the two are the same bit pattern. Where a stdio cursor sits on a word
boundary, one load or store does what six `getc`/`putc` expansions and their fat-pointer
helpers would. `lib/libc/stdio/{fread,fwrite}.c` and `cmd/libaout/{fgetw,fputw}.c` take that
path (see `cmd/libaout/fastio.h` for the guard, and `lib/libc/gen/qsort.c`, which had it
first); `fgeth`/`fputh` deliberately do **not**, a half-word leaving the cursor unaligned.
Two consequences that bite: **a stdio buffer size must be a whole number of words** or the
path dies after the first refill — `_ptr` advances six at a time, so its offset mod 6 is
invariant within a buffer (`LDBUFSIZ` was 1024, now 1026) — and **every relational lowers
to an out-of-line call** here (`b$ge`, `b$uge`, `b$lt`, …), even `x >= 0` in an `if`, so
loop shape is worth as much as the move itself.

Three traps the toolchain sources hit and nothing else has: **`b6lower` ignores designated
initializers** and initializes positionally, silently (`cmd/as/main.c`, `cmd/ld/ld.c` say so);
a **string literal cannot initialize a `char *` inside a struct initializer** at all; and there
is **no `int64_t`** — an `int` is 41 bits, an `unsigned` exactly 48, so only what really holds a
whole word becomes `uword_t` and everything narrower stays `word_t`. An *automatic* aggregate
initialised from runtime values (`unsigned char b[6] = { i >> 40, … }` in `cmd/libaout/putint.c`,
`char *av[]` in `cmd/ranlib/ranlib.c`) is **not** a fourth trap — C9d was the first task to
cross-compile one and it is correct — but it had no precedent before, so check a new one.

`mkstemp()`/`mkstemps()` are in this libc (`lib/libc/gen/mkstemp.c`, one object) and are **not
atomic**: there is no `O_CREAT` and no `O_EXCL` in this kernel, so each is `mktemp`'s name walk
plus `creat()` and a reopen. `cmd/ar` wanted the first and `cmd/cc` the second (its temporaries
carry a suffix that says which stage wrote them); `as`, `ld` and `strip` all use `tmpfile()`.
There is **no `posix_spawn()` and no `waitpid()`** — `<sys/wait.h>` has only the argument-less
`wait(2)`, and `cmd/cc`'s `run()` is `fork`/`execv`/`wait` in *both* builds rather than behind
an `#if besm6`.

`build/rootfs/` is staged only, never installed. **`root.manifest`** at the tree top describes
the image; paths resolve against `b6fsutil`'s working directory (`build/kernel/test`). Modes
(`04755` setuid on `mkdir`/`mv`/`rmdir`/`newgrp`/`passwd`/`su`) and the one hard link (`/bin/[`
→ `/bin/test`) live there, not in `build/rootfs/`. `etc/` stages the static `group`, `motd`,
`passwd`, `rc`, `termcap`, `ttys`; `lib/test/` stages `usr/test/*`.

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
- **`opendir(3)` exists** (`<dirent.h>`, `lib/libc/gen/`, `lib/libc/man/directory.3`), added
  with `cmd/ls`, its first and so far only caller. A **new** port that walks a pathname uses it
  rather than hand-rolling `<sys/dir.h>` — a name read out of a directory is **not
  NUL-terminated**, which is what the library now knows for you. Eight existing programs still
  hand-roll it (`cmd/TODO.md` C24), and eight *others* deliberately never will: `fsck`, `mkfs`,
  `ncheck`, `dcheck`, `icheck`, `quot`, `df` and `pstat` read a `struct direct` out of a raw
  block off `/dev/rmd*` and want `<sys/dir.h>` exactly as it is.

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
  `swap`, `utils`, `edit`, `fsinfo`, `dd`, `mkfs`, `fsck`, `mount`, `console`, `filters`,
  `inspect`, `tar`, `accounts`, `toolchain`). `toolchain` is C9's closing claim and the
  only one that needs the *host* `b6cc`: the machine runs `cc -o hello hello.s` with
  nothing pinned — its own `/usr/bin` search, its own `/lib`, its own `/usr/include` — and
  the a.out it produces is compared byte for byte with the host build's. They hold
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
  a line in `kernel/test/libtest.sh`. **A native test that is not a libc test wants the
  other, cheaper shape**: `b6_prog(... SOURCES ...)` + `b6_progtest()` staged into
  `build/rootfs/test/`, which needs none of those four and can link sources `b6_libtest()`
  cannot (it compiles exactly one `.c`). `cmd/novi/test/` and `cmd/libaout/rootfs/` use it.
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
accumulator, result in the accumulator, errno in `r14`). It **serves the target's `etc/`, not
the build machine's**: the six static files are compiled into `cmd/sim/etcfiles.cpp` and matched
on the *literal* path, `/dev/kmem`-style (writes get `EROFS`, exec `ENOEXEC`), so a
`getpwuid(3)` under `b6sim` reads the same bytes the booted kernel would — otherwise every name
a test prints is a property of whoever is building. `cmd/sim/test/etc_test.cpp` is the drift
guard: a seventh file joining `etc/` and not the table fails a test rather than quietly reaching
the host.

**Two system calls are not v7's**, and both took the lowest free `sysent.c` row rather than
appending (`include/sys/syscall.h` says why, and carries a signature on every `SYS_*` line):

- **`kctl(2)`, row 49** (`<sys/kctl.h>`, `kernel/kctl.c`) — the kernel-variable interface.
  Every earlier Unix found a kernel variable with `nlist(3)` over `/unix`, and **there is no
  kernel image on this disk**: `root.manifest` names no `/unix`, the simulator loads one off
  the build host. So the kernel publishes a small hand-written table instead. `KCTL_GET`
  copies a *value* (no memory device, no privilege — `dmesg`, `iostat`, `vmstat`);
  `KCTL_STAT` hands back an *address* for a pointer-chaser, and `pstat` is the only program
  left on that ladder, `ps` having moved to `KCTL_PSINFO`, which makes the kernel do the walk.
  `KCTL_LIST` is the only way to see the table — nothing in user space holds a second copy.
  `KCTL_SET` is reserved and answers `EINVAL`.
- **`statfs(2)`, row 50** (`<sys/statfs.h>`) — where `df(1)` gets its numbers, so that an
  ordinary user can measure the store without reading the raw device.

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
