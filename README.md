# Unix v7 for the BESM-6

A port of Seventh Edition Unix to the **BESM-6**, a Soviet 48-bit-word mainframe from
the 1960s.

## Background

The BESM-6 was the most widely used Soviet high-performance computer of its era — a
word-addressed machine with a 48-bit word and its own native instruction set and
floating-point format. This project revives Research Unix v7 on it, both as a piece of
computing archaeology and as a working system for the BESM-6 hardware simulator. The
machine's architecture differs from anything modern; the full details live in
[`doc/`](doc/).

## Approach

The port proceeds in two stages:

1. **Now — validate the C as i486.** The v7 kernel (derived from Robert Nordier's
   [v7/x86](http://www.nordier.com/v7x86/) port) is compiled as a 32-bit i486 ELF binary
   with modern Clang and strict warnings. This shakes the decades-old C into clean,
   warning-free shape before retargeting.
2. **Goal — retarget to the BESM-6.** Build the kernel for real BESM-6 code using this
   project's own toolchain together with the external [cross-compiler](#related-projects),
   and boot it on the [SIMH simulator](doc/Simh_Simulator.md) — the authentic full-machine
   emulator, and the hardware this port ultimately runs on.

## Repository layout

```text
kernel/    v7 kernel sources and device drivers (kernel/dev/)
include/   v7 system headers (sys/)
cross/     BESM-6 object/archive format headers (b.out.h, ar.h, ranlib.h)
cmd/       BESM-6 toolchain: cc, as, ld, cpp, disasm, sim
doc/       BESM-6 architecture references
```

## Components and status

| Component                     | Location      | Status                        |
|-------------------------------|---------------|-------------------------------|
| C compiler driver             | `cmd/cc`      | ✔ working, tested, documented |
| Assembler (AT&T / Madlen)     | `cmd/as`      | ✔ working, tested, documented |
| Linker + binutils             | `cmd/ld`      | ✔ working, tested, documented |
| C preprocessor                | `cmd/cpp`     | ✔ C11, tested, documented     |
| Disassembler                  | `cmd/disasm`  | ✔ working, tested             |
| a.out simulator (Unix v7)     | `cmd/sim`     | ✔ working, tested, documented |
| Kernel (i486 validation)      | `kernel/`     | ✔ builds                      |
| libc library                  | —             | ☐ to do                       |
| Build & link kernel for BESM-6| —             | ☐ to do                       |
| Peripheral drivers            | `kernel/dev/` | ◐ in progress                 |

## Building

The `cmd/` toolchain builds with a top-level CMake project (driven through a thin
`Makefile`); the kernel keeps its own LLVM Makefile.

**Toolchain** — from the repo root:

```sh
make            # configure and build every cmd/ tool into build/
make run        # build and run the unit tests (ctest)
make install    # install the tools as b6* into ~/.local (or /usr/local)
```

Building requires CMake and a host C/C++ compiler; GoogleTest is fetched
automatically, and every `cmd/` component has a unit-test suite run by `make run`.

**Kernel** — a separate LLVM cross-build:

```sh
cd kernel && make          # for now produces `unix` (i486 ELF) and `unix.nm`
```

See [CLAUDE.md](CLAUDE.md) for deeper build and architecture detail.

## Documentation

**The machine** — the BESM-6 architecture:

- [doc/Besm6_Instruction_Set.md](doc/Besm6_Instruction_Set.md) — opcodes, registers, and
  instruction encoding.
- [doc/Besm6_Calling_Conventions.md](doc/Besm6_Calling_Conventions.md) — the C ABI:
  argument passing, registers, and return linkage.
- [doc/Besm6_Data_Representation.md](doc/Besm6_Data_Representation.md) — how C scalar types
  are laid out in a 48-bit word.
- [doc/Besm6_Peripherals.md](doc/Besm6_Peripherals.md) — the programmer's view of the hardware:
  the `002 «рег»` and `033 «увв»` I/O instructions, every device register and control word, and
  the ГРП/ПРП interrupt bits. The reference the `kernel/dev/` drivers are written against.
- [doc/Memory_Mapping.md](doc/Memory_Mapping.md) — the MMU: how a virtual address becomes a
  physical one, the page registers РП and the protection register РЗ, why an instruction fetch is
  protected differently from a data load, supervisor mode and the extracode/interrupt gates into
  it, and how a fault is reported. The reference the kernel's memory management is written against.
- [doc/Intrinsics.md](doc/Intrinsics.md) — the nine `<besm6.h>` compiler intrinsics that let the
  kernel issue `002 «рег»`, `033 «увв»` and the bit-manipulation instructions from C rather than
  assembly.

**The target** — where the kernel runs:

- [doc/Simh_Simulator.md](doc/Simh_Simulator.md) — the SIMH full-machine BESM-6 emulator:
  building and running it, attaching peripherals, the front panel, tracing and debugging, and
  booting the DISPAK operating system.
- [doc/Aout_Simulator.md](doc/Aout_Simulator.md) — the `cmd/sim` simulator (`b6sim`): an
  apout-style user-level runner for BESM-6 `a.out` executables that services Unix v7 system
  calls; its CLI, trace modes, syscall set, and a worked example.

**The toolchain**:

- [doc/Assembler_Manual.md](doc/Assembler_Manual.md) — the `cmd/as` assembly language:
  syntax, directives, expressions, and addressing forms.
- [doc/Linker_Manual.md](doc/Linker_Manual.md) — the `cmd/ld` linker: linking model, symbol
  resolution, relocation, archives, and the `a.out` object/executable format.
- [doc/Archiver_Manual.md](doc/Archiver_Manual.md) — the `cmd/ar` archiver: commands, options,
  and the on-disk `.a` archive format.
- [doc/File_Magic.md](doc/File_Magic.md) — how to recognise a BESM-6 object or executable from
  its first bytes.
- [doc/Besm6_Runtime_Library.md](doc/Besm6_Runtime_Library.md) — the compiler-support routines
  (`b$save`, `b$mul`, the relational and conversion helpers): what the compiler emits calls to,
  the helper calling convention, and the ω-mode contract each one obeys.

**The kernel**:

- [doc/Kernel_Assembly_Routines.md](doc/Kernel_Assembly_Routines.md) — the machine-language
  assist (`kernel/x86.s`, to be rewritten as `kernel/besm6.S`): what each routine must do and
  the contract it owes its C callers.

## Related projects

- [besm6/c-compiler](https://github.com/besm6/c-compiler/) — C cross-compiler for the BESM-6.
- [besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) — authentic BESM-6 hardware
  simulator; the machine this port targets. Documented locally in
  [doc/Simh_Simulator.md](doc/Simh_Simulator.md) (operator's view) and
  [doc/Besm6_Peripherals.md](doc/Besm6_Peripherals.md) (programmer's view).

## License

The Unix v7 portions are distributed under the Caldera BSD-style license. See
[COPYRIGHT](COPYRIGHT) for the full notice.
