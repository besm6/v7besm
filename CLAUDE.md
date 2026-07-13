# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A port of **Unix v7 to the BESM-6**, a Soviet 48-bit-word mainframe from the 1960s. The
work has two halves:

- **`kernel/` + `include/`** — the Unix v7 kernel itself. Sources are derived from Robert
  Nordier's v7/x86 port (see the copyright in `kernel/x86.s`). They currently build and
  are validated as a **32-bit i486 ELF binary**, not yet BESM-6 code — this is an
  intermediate step to get the C compiling cleanly with modern warnings before retargeting.
- **`cmd/`** — the BESM-6-specific toolchain being written/ported to eventually build the
  kernel for real BESM-6 hardware: a C compiler driver, an assembler, a linker
  (+ archiver/nm/size/etc.), a C preprocessor, a disassembler, and a user-level a.out
  simulator (`b6sim`, `cmd/sim/`) that runs BESM-6 executables and services Unix v7
  syscalls — the only tool here that actually *executes* BESM-6 code.

External pieces this project depends on (not in this repo):
- BESM-6 C cross-compiler: https://github.com/besm6/c-compiler/
- BESM-6 hardware simulator (SIMH): https://github.com/besm6/simh/tree/master/BESM6/ — the
  authentic full-machine emulator, distinct from the in-repo user-level `b6sim`. **This is the
  machine the kernel will boot on**, so it is the target the port aims at, not just a convenience.
  It is documented here in `doc/Simh_Simulator.md` (how to build, run, and drive it) and
  `doc/Besm6_Peripherals.md` (how a program talks to its hardware).

## Building

Two separate build systems. The **`cmd/` toolchain** builds with a **top-level CMake
project**, driven through a thin top-level `Makefile`; there are no per-component
Makefiles under `cmd/` anymore (everything is configured by the root `CMakeLists.txt`).
The **kernel** keeps its own hand-written Makefile because it cross-compiles with LLVM.

### Toolchain (`cmd/`) — top-level build

From the repo root:
```sh
make            # configure (into build/) and build every cmd/ tool
make test       # build the unit tests, but don't run them
make run        # run all unit tests via ctest
make clean      # remove build/
make install    # install the tools as b6* into ~/.local (or /usr/local)

make clean; make debug; make   # reconfigure as a Debug build (default is RelWithDebInfo)
```
Requires **CMake** and a host C/C++ compiler (C++17). GoogleTest is fetched automatically
at configure time, and **cppcheck** runs as part of the build when installed. Everything is
compiled with `-Wall -Werror -Wshadow`. Each tool is built under a `b6`-prefixed name and
`make install` copies it into `bin/` (`cmd/cc`→`b6cc`, `cmd/as`→`b6as`, `cmd/ld`→`b6ld`,
`cmd/cpp`→`b6cpp`, `cmd/disasm`→`b6disasm`, `cmd/sim`→`b6sim`, plus the binutils
`b6ar`/`b6nm`/`b6size`/`b6strip`/`b6ranlib`/`b6lorder`).
These are host tools that run on the build machine and emit BESM-6 objects. **Do not** invoke `cc`/`clang` by hand or run
`cmake --build` directly — always go through the top-level `make` targets.

### Kernel (`kernel/`)
```sh
cd kernel && make          # produces `unix` (i486 ELF) and `unix.nm`
make clean
```
The kernel is **not** part of the CMake build. It requires **LLVM 19** (`clang`, `ld.lld`,
`llvm-nm`, `llvm-size`). The Makefile locates it via `/usr/local/Cellar/llvm@19/...`
(Homebrew on macOS); adjust `LLVMBIN` if installed elsewhere. The kernel is compiled with
`clang -target i486-unknown-linux-gnu -ffreestanding -Os -DKERNEL -Wall -Werror -Wshadow`
and linked with `ld.lld -T unix.ld`.

The kernel is split into two static libs that are link-pulled so unused code is dropped:
`libsys.a` (core kernel, `kernel/*.c`) and `libdev.a` (device drivers, `kernel/dev/*.c`).
`x86.s` is the x86 machine-assist (the former `mch.s`, still x86-specific) and `besm6.S` is its
BESM-6 counterpart (a skeleton — symbols only, bodies still to be written); `conf.c` is the
device config table.

Diagnostic make targets (require the external `cast` tool / C compiler AST dump):
- `make i` — preprocess each source to `.i`
- `make yaml` — dump `.yaml` for each source
- `%.ast` — C compiler AST dump (`*.ast` files are committed snapshots)

### Tests

Every `cmd/` component has a GoogleTest suite under `cmd/<tool>/test/`, wired into the
`build_tests` target and run by `make run` (ctest). The C preprocessor has the most
extensive one: a **C11 (N1570) conformance suite** in `cmd/cpp/test/` that drives the built
`b6cpp` over source snippets. All of its suites derive from a single `PreprocessorTest`
gtest fixture in `cmd/cpp/test/test_support.{h,cpp}` — the harness spawns the tool in a
temp dir, captures and normalizes its output, and exposes `Preprocess`/`Normalize` plus the
`EXPECT_TOKENS`/`EXPECT_PP_OK`/`EXPECT_PP_DIAGNOSES` matchers as methods; a per-suite alias
(`using Macro = PreprocessorTest;`) keeps each suite's name. b6cpp now implements the bulk of
C11 preprocessing — variadic macros and `__VA_ARGS__`, the `#` (stringize) and `##` (paste)
operators, `_Pragma`, the C11 predefined macros, trigraphs, `#line`/`#error`/`#pragma`, and
"blue paint" rescanning — and the conformance suite passes in full (no `DISABLED_` tests
remain). See [cmd/cpp/README.md](cmd/cpp/README.md) for the user-facing feature and option
list.

## Architecture notes

**BESM-6 is word-addressed, not byte-addressed.** The addressable unit is one 48-bit word;
there are no sub-word load/store instructions. Consequences that pervade the toolchain and
any retargeting work: `CHAR_BIT == 8` but six chars pack into a word, so `sizeof(int) == 6`
(six char-units = one word) and addresses are word indices. Bit numbering is right-to-left
from 1 (bit 1 = LSB, bit 48 = MSB). Numbers in BESM-6 contexts are octal. There is no IEEE
754 — the machine has its own float format.

**Read `doc/` before touching codegen, the assembler, or anything ABI-related.** These are
the authoritative references and are kept current:
- `doc/Besm6_Instruction_Set.md` — opcodes, registers (A accumulator, r1–r15, mode reg R),
  24-bit instructions packed two per 48-bit word.
- `doc/Besm6_Calling_Conventions.md` — args pushed in direct order, last arg left in the
  accumulator, r14 = negative arg count, r13 = return address; `_Noreturn` tail-call rules.
- `doc/Besm6_Data_Representation.md` — how every C scalar is laid out in a word.
- `doc/Besm6_Peripherals.md` — the peripherals as a *program* sees them: the `002 «рег»` and
  `033 «увв»` supervisor instructions, the full `033` address map, each device's control-word
  bit fields, and the ГРП/ПРП interrupt bits. Read it before touching `kernel/dev/`.
- `doc/Simh_Simulator.md` — the external SIMH full-machine emulator: building and running it,
  attaching peripherals, the front panel, tracing/debugging, and booting DISPAK.
- `doc/Assembler_Manual.md` — the `cmd/as` assembly language: lexical rules, directives,
  expression grammar, number formats, operand/addressing forms, and `$NN`/`@NN` raw opcodes.
- `doc/Linker_Manual.md` — the `cmd/ld` linker: the linking model, symbol resolution,
  relocation, archives/libraries, and the `a.out` object/executable format.
- `doc/Archiver_Manual.md` — the `cmd/ar` archiver: command/option letters and the on-disk
  `.a` archive format (`ARMAG`, `struct ar_hdr`, word padding).
- `doc/File_Magic.md` — how to recognise a BESM-6 object/executable from its leading bytes.
- `doc/Besm6_Runtime_Library.md` — the compiler-support routines (`b$save`, `b$mul`, the
  relational/conversion helpers): the lightweight helper calling convention (first operand on
  the stack, second in the accumulator, result in the accumulator) and the ω-mode/`NTR 3`
  contract every helper must preserve. Sources live in the external c-compiler repo under
  `libc/besm6/unix/`.
- `doc/Intrinsics.md` — the nine C compiler intrinsics of `<besm6.h>`, **implemented** in the
  external c-compiler, that let the kernel drive the hardware from C instead of assembly: the
  privileged pair `__besm6_ext` (033 «увв») and `__besm6_mod` (002 «рег»), `__besm6_stop`, the
  bit-manipulation builtins C has no equivalent for (`apx`/`aux` gather and scatter, `acx`, `anx`,
  `arx`), and `__besm6_extracode`. Each compiles to a single inline instruction, never a call.
  Signatures, semantics, diagnostics, the generated code, and worked examples of an `spl`, an
  interrupt dispatch and a drum read written in C. Read it before writing anything in
  `kernel/dev/` or adding to `kernel/besm6.S` — much of what that file was meant to hold is now
  expressible in C.
- `doc/Aout_Simulator.md` — the `cmd/sim` simulator (`b6sim`): what it is (an apout-style
  user-level a.out runner, not full-machine SIMH), its CLI and trace modes, the Unix v7
  syscall set, and the `$77 N` extracode syscall trap.
- `doc/Kernel_Assembly_Routines.md` — the machine-language assist (`kernel/x86.s`, to be
  rewritten as `kernel/besm6.S`): every routine's contract with its C callers.

**Object/executable format** is a BESM-6-specific `a.out` variant defined in
`cross/besm6/b.out.h` (magic `FMAGIC`/`NMAGIC`, `struct exec` with separate
`const`/`text`/`data`/`bss` segment sizes). The assembler, linker, disassembler, and
simulator all share this header. The shared on-disk serialization lives in `cmd/libaout` and uses the
BESM-6 **6-byte word** (`W == 6`, two 3-byte big-endian half-words, **high half-word
first**, so a word's six bytes read as one big-endian 48-bit number — this holds uniformly
for instructions, `.word`/`.half` data, the constant pool, and the header); the archive member
header (`cross/besm6/ar.h`, `struct ar_hdr`) is word-aligned with 30-char names and
`ARHDRSZ == 60`. The assembler uses **AT&T-style syntax with Madlen mnemonics**; the
disassembler prints the Cyrillic BESM-6 mnemonics directly.

**`cmd/cc` (`b6cc`) is the compiler driver**, not a compiler itself. It chains the
toolchain one sub-tool per stage — `b6cpp` → `b6parse` → `b6lower` → `b6codegen` → `b6as`
→ `b6ld` — where `b6parse`/`b6lower`/`b6codegen` are the three passes of the external
[c-compiler](https://github.com/besm6/c-compiler/) (installed to `~/.local/bin`). Stage
selection follows the usual `cc` flags: `-E` (stop after cpp), `-S` (after codegen, emit
Madlen assembly), `-c` (after assembling). Sub-tools are resolved via per-tool env
overrides (`B6CPP`, `B6PARSE`, …) or under `~/.local/bin` then `/usr/local/bin`. The full
pipeline runs end-to-end; `-O` and `-g` are accepted but currently no-ops. See
[cmd/cc/README.md](cmd/cc/README.md).

The driver also passes `b6cpp` an `-I` for the compiler's own header directory
(`<prefix>/share/besm6/include`, `~/.local` first, then `/usr/local`), so `#include <besm6.h>` —
the machine intrinsics, see `doc/Intrinsics.md` — resolves with no flags of its own.

**`cmd/sim` (`b6sim`) is a user-level a.out simulator**, in the spirit of Warren Toomey's
`apout` for the PDP-11 (reference copy under `cmd/sim/tmp/apout/`). It loads one BESM-6
`a.out`, interprets the instruction stream on a software CPU + memory model, and traps the
sole user-mode extracode `$77 N` to run Unix v7 syscall `N` on the host (`syscall.cpp`;
numbers from `kernel/sysent.c`). The syscall ABI follows the calling convention — args
1..N-1 below the stack pointer `r15`, last arg in the accumulator, result in the
accumulator, errno in `r14` — and because every C scalar is one word, structs like
`struct stat` are one word per field. Since there is no `crt0`/`libc` yet, the loader seeds
`r15` and the program break itself. Today's runnable path is `.s` → `b6as` → `b6ld` →
`b6sim`; the C front end plugs into the same final step as it matures. This is the harness
for verifying the compiler back-end and runtime library. See `doc/Aout_Simulator.md` and
the ABI-spec tests in `cmd/sim/test/sim_test.cpp`.

**SIMH is the target machine — the kernel boots there.** The external
[besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) emulator is the full BESM-6:
512K words of memory, an MMU, drums, disks, tapes, an АЦПУ drum printer, punch-tape and card
equipment, and a 24-line terminal multiplexer. Executables written by `b6ld` load into it
directly (`sim> load prog`), which is how code built here reaches the real machine model. What
this means for the kernel, and for the `kernel/dev/` drivers in particular:
- **There is no I/O address space and no channel programs.** Every device is reached by two
  *supervisor-only* instructions that name a register through the **effective address** and pass
  data in the **accumulator**: `033 «увв»` (`ext`) for the peripherals themselves, and
  `002 «рег»` (`mod`) for CPU-internal registers — including ГРП, which is how every device
  reports back. One bit of the address selects read vs. write (`04000` for `033`, `0200` for
  `002`).
- **Devices answer through two interrupt registers**, ГРП (48-bit, main) and ПРП (24-bit, slow
  character devices), each with a mask. ПРП has no interrupt line of its own — a pending ПРП
  interrupt is delivered by raising `GRP_SLAVE` in ГРП. Some bits are *wired* and cannot be
  cleared by writing to the register; only clearing the device clears them.
- **Mass storage exchanges in zones** of `8 + 1024` words — 8 service words plus 1 Kword of data.
  The data lands where the control word says, but the service words always land at a **fixed low
  memory address**, one buffer per controller (`010` drum 1, `030` disk 3, …).

`doc/Besm6_Peripherals.md` has the full address map, every control word's bit fields, and both
interrupt registers bit by bit; `doc/Simh_Simulator.md` covers driving the simulator itself.

**`include/` is the Unix v7 system-header tree** (`sys/` plus libc-style headers). The
kernel includes them via `-I../include`.

## Conventions

- C sources use clang-format (`.clang-format` at repo root).
- Comments and identifiers in the toolchain are frequently in **Russian** (BESM-6 is a
  Russian machine); match the surrounding language when editing a given file.
- Build artifacts (`*.o`, `*.a`, `*.i`, `*.ast`, `*.yaml`, etc.) are git-ignored
