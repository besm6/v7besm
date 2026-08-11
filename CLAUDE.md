# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

A port of **Unix v7 to the BESM-6**, a Soviet 48-bit-word mainframe.

- **`kernel/` + `include/`** — the v7 kernel (from Robert Nordier's v7/x86 port; see
  `COPYRIGHT`), cross-built here and **booting under SIMH** to multi-user shells.
- **`cmd/`** — the toolchain as host tools, including `b6sim`, a user-level a.out simulator,
  plus **99 native BESM-6 programs** staged into `build/rootfs/` for the root image — the count
  is `b6_prog()` calls under `cmd/` whose `DEST` is not `test/…`, and nothing but that rule.
- **`lib/`** — cross-built `libc.a`, `libm.a`, `libtermcap.a`, `libcurses.a`, `crt0.o`.

**The narrative lives in the per-directory READMEs, and that is where to look before touching
anything**: `kernel/README.md` and `doc/Besm6_Kernel_Reference.md` (memory model, hardware
rules), `cmd/README.md` (the porting
recipe), `cmd/TODO.md`, and most `cmd/<prog>/`. Two things are external: the C
cross-compiler <https://github.com/besm6/c-compiler/>, supplying `libruntime.a`'s `b$*` helpers
and the ten freestanding headers, and SIMH <https://github.com/besm6/simh/tree/master/BESM6/>.

## Building

**One CMake project** behind a thin top-level `Makefile`; `kernel/` and `kernel/test/`
Makefiles are wrappers over the same `build/` tree. From the repo root:

```sh
make            # configure into build/ and build everything
make test       # build unit tests without running them
make run        # run the whole suite via ctest — every label, kernel SIMH tests included
make install    # install b6* tools, include/, and the archives into ~/.local
make clean; make debug; make    # reconfigure as Debug (default RelWithDebInfo)
```

**Do not** invoke `cc`/`clang` by hand or run `cmake --build` directly — always go through the
top-level `make` targets. Everything builds `-Wall -Werror -Wshadow`. Tools install
`b6`-prefixed (`b6cc`, `b6as`, `b6ld`, `b6cpp`, `b6sim`, `b6fsutil` and the binutils);
`include/` goes to `<prefix>/share/besm6/include`. `kernel/`, `lib/` and the native programs
are guarded on `libruntime.a` being installed.

Link order is a contract — **`b6ld` scans each archive once, in order**:
`crt0.o … -lcurses -ltermcap -lc -lruntime`. The kernel takes `-lruntime` alone (own `printf`
in `kernel/prf.c`, no libc). `b6_prog(... LIBS ...)` **emits that order rather than the
caller's**.

### Native BESM-6 programs

One `b6_prog()` call in `scripts/BesmCross.cmake` per program:

```cmake
b6_prog(init DEST etc/init SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/init.c)
```

It stages the program into `build/rootfs/` and registers a `rootfs_<name>_size` ctest guarding
the two **user** address-space ceilings, whose failures are otherwise silent:
`const+text+data+bss` ≤ **28,672 words** (32 pages less the 4-page stack at `070000`), and no
relocatable symbol above word **32,767** (a 15-bit pointer's reach). Two more ceilings it
cannot guard, and both bind in practice: **no struct may exceed 4,096 words** (a member is a
12-bit offset from a base register — move the big arrays to file scope), and the **4,096-word
stack**, where a long function costs 1.5–2 words per source line before any array.
`cmd/README.md` §6 is the account. **The root image has 345 free blocks of 2000** — it had 181
until the `lib/test` programs moved to the test pack, which has 1,686 free of its own, and
`/usr/bin/yacc` and `/usr/bin/lex` have since taken 68 back, `/bin/expr` 14, `/bin/egrep` 14 and
`/bin/m4` 19.

**Twelve programs are built twice** — `cpp`, `as`, `ld`, `nm`, `size`, `strip`, `disasm`, `ar`,
`ranlib`, `cc`, `yacc`, `lex` — as the host `b6*` tools and, from the same sources under
`cmd/<x>/rootfs/`, as native `/usr/bin/*` reproducing the host tools' output byte for byte. A
second native build needs that subdirectory, `cmd/<x>` being added above the `B6RUNTIME_LIB`
guard where `b6_prog()` does not yet exist. Size profiles are keyed on the `besm6` macro `b6cpp`
predefines (`cmd/cpp/defs.h`, `cmd/as/as.h`, `cmd/ld/intern.h`, `cmd/yacc/dextern.h`,
`cmd/lex/ldefs.h`). **Native `cc` cannot compile C** — `b6parse`, `b6lower`, `b6codegen` cannot
be built for the target — and says so. `yacc` and `lex` each stage a data file too,
`/usr/lib/yaccpar` and `/usr/lib/lex/ncform`, and neither will run without its own. Where a
profile cuts a table that is `calloc`'d rather than declared — `lex`'s are — **`rootfs_<x>_size`
sees none of it**, and the program links, passes every check and dies in `malloc`.

**Bulk I/O moves a word, not a byte**: six chars pack big-endian into a 48-bit word and a word
on disk is six big-endian bytes, the same bit pattern, so where a stdio cursor sits on a word
boundary one load or store replaces six `getc`/`putc` expansions (`lib/libc/stdio/`,
`cmd/libaout/{fgetw,fputw}.c`; guard in `cmd/libaout/fastio.h`). `fgeth`/`fputh` deliberately
do **not** — a half-word leaves the cursor unaligned. Two consequences: **a stdio buffer size
must be a whole number of words** or the path dies after the first refill, and **every
relational lowers to an out-of-line call** (`b$ge`, `b$lt`, …), even `x >= 0` in an `if`.

Traps: **`b6lower` ignores designated initializers**, initializing positionally and silently;
a **string literal cannot initialize a `char *` inside a struct initializer**; there is **no
`int64_t`** (an `int` is 41 bits, an `unsigned` exactly 48, so only what holds a whole word
becomes `uword_t`); **no `posix_spawn()` and no `waitpid()`**, `<sys/wait.h>` having the
argument-less `wait(2)` only; and `mkstemp()`/`mkstemps()` are **not atomic**, this kernel
having no `O_CREAT` and no `O_EXCL`.

`build/rootfs/` is staged only, never installed, and also carries
`/lib/{crt0.o,libc.a,libruntime.a}`, `/usr/include` and `/usr/man` through the top-level
`CMakeLists.txt`'s `B6_STAGE_*` lists. **`root.manifest`** at the tree top describes the image;
paths resolve against `b6fsutil`'s working directory (`build/kernel/test`), and modes (six
setuid) and the one hard link (`/bin/[` → `/bin/test`) live there rather than in the staging
tree.

**There are two disks.** `build/testfs/` is the second staging tree and **`test.manifest`**
the second manifest: the `lib/test` programs, which were `/usr/test` on the root until the
**test pack** existed and are now `/test/*` on `test3077.disk`. Nothing mounts it
automatically — `/etc/rc` has no line for it, deliberately — so a dialogue that wants them
types `/etc/mount /dev/md1 /mnt -r` and reads them as `/mnt/test/*`. It is attached `-r` and
mounted `-r`, so one shared copy serves every test; only `kernel/test/core` uses it, and that
is the tree's only live caller of `smount()`.

**Porting v7 userland**: read `cmd/README.md`, the twelve-point recipe. `b6parse` is **strict
C11** — no implicit `int`, no K&R parameter lists — but what bites is that v7 assumes `int` and
`char *` are the same thing: a flag packed into bit 0 of a pointer, a mask rounding to a word
assuming `BYTESPERWORD`, pointer casts that *floor* rather than round. Also: `long` is one
word; `BSIZE` is 3072 bytes but tools report 1024-byte blocks; `DIRSIZ` 18. **`opendir(3)`
exists**, and a new port that walks a pathname uses it rather than hand-rolling `<sys/dir.h>` —
a name read out of a directory is **not NUL-terminated**.

### Kernel

```sh
cd kernel && make          # produces `unix', unix.nm, unix.dis
make demo                  # boot under SIMH (besm6 unix.ini) — an interactive session,
                           # not a test run; the tests are the top-level `make run'
```

Built with the **in-tree** tool targets, so a rebuilt `b6as` relinks with no `make install`.
`make` prints `b6size -w unix`: the image **must end below `054000`** (`KEND`). Everything is
archived into one `libunix.a`, and **`besm6.o` must come first in `OBJ`** so its const
contribution pins the interrupt/extracode vectors at their fixed addresses. `brz.s` and
`syscall.c` are separate files because `kernel/test/` links them directly. Header dependencies
are **coarse, not computed** (`b6cc` has no `-M`): every object depends on every
`include/sys/*.h`, and a **new header file** needs a re-configure.

### Tests

**Kernel tests run on real SIMH, and the top-level `make run` already runs them** —
`kernel/test/`'s targets hang off `build_tests`, so `make test` rebuilds `root.img` and the
test images too. `cd kernel/test && make test` is the same thing narrowed to `-L kernel`. What
is *not* safe is a bare `ctest -L kernel`, which builds nothing and so tests a stale image.
Each test is a standalone BESM-6 program linking kernel objects, with a `.ini` that loads it,
runs it, and asserts on machine state; `b6sim` cannot substitute, and `mmutest` is the one to
copy.

- **Run every MMU test with `set mmu cache`** — the БРЗ hazards are invisible otherwise.
- **Three tests boot the kernel**, and only three: `boot`, a one-second smoke test that it
  still reaches a shell prompt; `multi`, which types `^D` at that prompt and goes on into
  multi-user mode — `/etc/rc`, a getty per line of `/etc/ttys`, `crypt(3)`, and root on
  `/dev/console` with guest on `/dev/tty1` at the same instant; and `core`, which mounts the
  test pack and runs `/mnt/test/coret`. `multi` is also the worked example for typing at the
  guest and for driving the second Consul, which needs the host program
  `kernel/test/ttyhost.c`. When a README claims `fsck` repairing a pack, the swapper under
  pressure or the self-hosting `cc` run is covered, it describes one of the eighteen tests
  that no longer exist.
- The libc suite runs under `b6sim` only; adding a program = a `b6_libtest()` call +
  `lib/test/progs.cmake` + `test.manifest`. **A native test that is not a libc test wants the
  cheaper shape**: `b6_prog(... SOURCES ...)` + `b6_progtest()` staged into
  `build/rootfs/test/` (`cmd/novi/test/`, `cmd/libaout/rootfs/`); it can link sources
  `b6_libtest()` cannot, that one compiling exactly one `.c`.
- ctest labels: `kernel` (SIMH), `lib` (b6sim), `rootfs` (size checks) and `sh`. Every `cmd/`
  tool has a GoogleTest suite under `cmd/<tool>/test/`; `cmd/cpp/test/` is a full C11 (N1570)
  conformance suite.
- **A lone unexpected failure is usually the harness, not the change.** The suite runs in
  parallel and both simulators are timing-sensitive, so a case that fails under `make run` and
  passes on its own was a flake. Re-run it alone, then move on.

## Architecture notes

**BESM-6 is word-addressed.** The addressable unit is one 48-bit word; no sub-word load/store.
`CHAR_BIT == 8` but six chars pack into a word, so **`sizeof(int) == 6`** and addresses are
word indices. Bits number right-to-left from 1. Numbers are octal. No IEEE 754.

**Kernel memory model** (`kernel/README.md` tells how it works, `doc/Besm6_Kernel_Reference.md`
the rules — read them before touching anything memory-related, and keep both current):

- The kernel **runs unmapped** (БлП = БлЗ = 1), so a kernel address *is* physical; kernel
  image, u-area and buffer cache must fit the low 32 pages, since supervisor instruction fetch
  is never mapped.
- Two fixed physical areas, absolute symbols rather than bss (devices transfer to *physical*
  addresses): the **u-area, two pages at `074000`**, and **`buffers[NBUF][BSIZE]` at
  `054000`–`074000`**. Hence `KEND == BUFBASE == UBASE - NBUF*BSIZEW`. `NBUF` (16) must stay
  above `NMOUNT` (8): every mount pins one buffer until it is unmounted.
- **РП always holds the current process's map**, so a trap switches nothing. The shadow map is
  `u.u_upt[8]`, the hardware registers not being readable; `sureg()` (`kernel/utab.c`) loads
  the space in twelve `рег`s.
- **Drain the БРЗ write cache before every РП write** — `drainbrz()` in `kernel/brz.s`, which
  **cannot be written in C** (the nine stores must be consecutive; `b6cc` spills the pointer
  through a frame slot). Verified by disassembly; don't re-litigate. A device reads memory
  rather than the write cache, so drivers must drain before a write exchange too.

**SIMH device model** (`doc/Besm6_Peripherals.md`, `doc/Memory_Mapping.md`): no I/O address
space and no channel programs — a device is reached by a supervisor instruction naming a
register through the effective address with data in the accumulator, `033 «увв»` (`ext`) for
peripherals and `002 «рег»` (`mod`) for CPU registers. Devices answer through ГРП and ПРП; some
ГРП bits clear only at the device. The MMU is eight write-only registers, 32 pages of 1 Kword.

**Object format**: BESM-6 `a.out` and archive headers in `cross/besm6/{b.out.h,ar.h}` (separate
const/text/data/bss sizes), serialized by `cmd/libaout` with a **6-byte word**. `b6as` uses
AT&T syntax with Madlen mnemonics, `b6disasm` the same (`-b` for Cyrillic BEMSH).

**`b6cc` is a driver**, not a compiler: `b6cpp` → `b6parse` → `b6lower` → `b6codegen` → `b6as`
→ `b6ld`, the middle three external; `-O`/`-g` are no-ops. **`b6sim`** interprets one a.out and
traps `$77 N` to run v7 syscall `N` on the host (numbers from `kernel/sysent.c`; args below
`r15`, last in the accumulator, result in the accumulator, errno in `r14`), and **serves the
target's `etc/`, not the build machine's**, from `cmd/sim/etcfiles.cpp`.

**Two system calls are not v7's**, both taking the lowest free `sysent.c` row rather than
appending (`include/sys/syscall.h` carries a signature on every `SYS_*` line): **`kctl(2)`,
row 49** (`<sys/kctl.h>`, `kernel/kctl.c`), the kernel-variable interface, since **there is no
kernel image on this disk** for `nlist(3)` to read — `KCTL_GET` copies a value, `KCTL_STAT` an
address, `KCTL_PSINFO` walks the process table for `ps`, `KCTL_LIST` is the only view of the
table; and **`statfs(2)`, row 50** (`<sys/statfs.h>`), where `df(1)` gets its numbers.

**`include/`** is the v7 header tree, C11-ified. **Every header stands alone and includes what
it uses**, so include lists are sets, not sequences. Two traps: `<sys/param.h>` must stay
`#define`-only (`kernel/*.S` includes it, and there is no `__ASSEMBLER__`), and **`b6cpp`
rejects a macro redefinition whose replacement text is not character-identical**, alignment
included.

**Read `doc/` before touching codegen, the assembler, or anything ABI-related** — those files
are authoritative and kept current, notably `Besm6_Instruction_Set.md`,
`Besm6_Calling_Conventions.md` (args in direct order, last in the accumulator, r14 = negative
arg count, r13 = return address) and `Intrinsics.md` (the twelve `<besm6.h>` intrinsics that
let `kernel/dev/` be written in C — read before writing any driver).

## Conventions

- clang-format (`.clang-format` at repo root), but **not over `include/`** — re-spacing a
  `#define` changes what `b6cpp` compares on redefinition.
- Comments and identifiers are frequently in **Russian**; translate to English.
- Be concise in your comments in the code. Put the reasoning in the README.
- Build artifacts (`*.o`, `*.a`, `*.i`, `*.ast`, `*.yaml`) are git-ignored.
- `scripts/vscode-besm6/` transcribes the mnemonic tables of `cmd/as/tables.c` and
  `cmd/disasm/dis.c`; a new mnemonic goes into both places.
