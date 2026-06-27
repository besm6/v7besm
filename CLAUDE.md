# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A port of **Unix v7 to the BESM-6**, a Soviet 48-bit-word mainframe from the 1960s. The
work has two halves:

- **`kernel/` + `include/`** — the Unix v7 kernel itself. Sources are derived from Robert
  Nordier's v7/x86 port (see the copyright in `kernel/mch.s`). They currently build and
  are validated as a **32-bit i486 ELF binary**, not yet BESM-6 code — this is an
  intermediate step to get the C compiling cleanly with modern warnings before retargeting.
- **`cmd/`** — the BESM-6-specific toolchain being written/ported to eventually build the
  kernel for real BESM-6 hardware: an assembler, a linker (+ archiver/nm/size/etc.), a C
  preprocessor, and a disassembler.

External pieces this project depends on (not in this repo):
- BESM-6 C cross-compiler: https://github.com/besm6/c-compiler/
- BESM-6 hardware simulator (SIMH): https://github.com/besm6/simh/tree/master/BESM6/

## Building

There is no top-level build. Each component has its own Makefile and is built from its
own directory.

### Kernel (`kernel/`)
```sh
cd kernel && make          # produces `unix` (i486 ELF) and `unix.nm`
make clean
```
Requires **LLVM 19** (`clang`, `ld.lld`, `llvm-nm`, `llvm-size`). The Makefile locates it
via `/usr/local/Cellar/llvm@19/...` (Homebrew on macOS); adjust `LLVMBIN` if installed
elsewhere. The kernel is compiled with `clang -target i486-unknown-linux-gnu
-ffreestanding -Os -DKERNEL -Wall -Werror -Wshadow` and linked with `ld.lld -T unix.ld`.

The kernel is split into two static libs that are link-pulled so unused code is dropped:
`libsys.a` (core kernel, `kernel/*.c`) and `libdev.a` (device drivers, `kernel/dev/*.c`).
`mch.s` is x86 machine-assist (still x86-specific); `conf.c` is the device config table.

Diagnostic make targets (require the external `cast` tool / C compiler AST dump):
- `make i` — preprocess each source to `.i`
- `make yaml` — dump `.yaml` for each source
- `%.ast` — C compiler AST dump (`*.ast` files are committed snapshots)

### Toolchain commands (`cmd/`)
```sh
cd cmd/as  && make    # assembler           -> `as`  (installs as besm6-as)
cd cmd/ld  && make    # linker              -> `ld`  (installs as mkb-ld)
cd cmd/cpp && make    # C preprocessor      -> `cpp`
cd cmd/disasm && make # disassembler
```
`cmd/ld`'s Makefile also has targets for the companion binutils built from the same object
helpers (`ar`, `nm`, `size`, `strip`, `ranlib` → installed as `mkb-*`); only `ld` is built
by default — uncomment in the `all:` rule to build the rest. These are built with the host
compiler (`$(CC)`), since they are tools that *run on the host* and emit BESM-6 objects.

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

**Object/executable format** is a BESM-6-specific `a.out` variant defined in
`include/b.out.h` (magic `RMAGIC`/`OMAGIC`/`NMAGIC`, `struct exec` with separate
`const`/`text`/`data`/`bss` segment sizes). The assembler, linker, and disassembler all
share this header. The assembler uses **AT&T-style syntax with Madlen mnemonics**; the
disassembler prints the Cyrillic BESM-6 mnemonics directly.

**`include/` is the Unix v7 system-header tree** (`sys/` plus libc-style headers). The
kernel includes them via `-I../include`.

## Conventions

- C sources use clang-format (`.clang-format` at repo root).
- Comments and identifiers in the toolchain are frequently in **Russian** (BESM-6 is a
  Russian machine); match the surrounding language when editing a given file.
- Build artifacts (`*.o`, `*.a`, `*.i`, `*.ast`, `*.yaml`, etc.) are git-ignored
