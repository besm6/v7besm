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

The port proceeded in three steps, and is on the third.

1. **Done — validate the C as i486.** The v7 kernel (derived from Robert Nordier's
   [v7/x86](http://www.nordier.com/v7x86/) port) was first compiled as a 32-bit i486 ELF binary
   with modern Clang and strict warnings, to shake the decades-old C into clean, warning-free
   shape before retargeting.
2. **Done — build it as BESM-6 code.** The kernel now compiles and links with this project's own
   toolchain and the external [cross-compiler](#related-projects), into a BESM-6 `a.out`.
3. **Under way — make it run.** Retarget the machine-dependent half: memory management, boot,
   traps, and the context switch, and boot the result on the
   [SIMH simulator](doc/Simh_Simulator.md) — the authentic full-machine emulator, and the
   hardware this port ultimately runs on. The memory model is settled and the address-space code
   is real; `_start`, the trap gate and the switch are still skeletons, so the kernel does not
   boot yet. [kernel/TODO.md](kernel/TODO.md) is the live work plan.

Kernel code that *does* run today runs under SIMH, as standalone tests in
[kernel/test/](kernel/test/): each links kernel objects against a hand-built environment and lets
a `.ini` script assert on the machine state afterwards. That is how the MMU code was verified.

## Repository layout

```text
kernel/       v7 kernel sources, device drivers (kernel/dev/), and the work plan (TODO.md)
kernel/test/  standalone SIMH tests: the only way to run kernel code on the target so far
include/      v7 system headers (sys/)
cross/        BESM-6 object/archive format headers (b.out.h, ar.h, ranlib.h)
cmd/          BESM-6 toolchain: cc, as, ld, cpp, disasm, sim
doc/          BESM-6 architecture references
```

## Components and status

| Component                     | Location         | Status                                  |
|-------------------------------|------------------|-----------------------------------------|
| C compiler driver             | `cmd/cc`         | ✔ working, tested, documented           |
| Assembler (AT&T / Madlen)     | `cmd/as`         | ✔ working, tested, documented           |
| Linker + binutils             | `cmd/ld`         | ✔ working, tested, documented           |
| C preprocessor                | `cmd/cpp`        | ✔ C11, tested, documented               |
| Disassembler                  | `cmd/disasm`     | ✔ working, tested                       |
| a.out simulator (Unix v7)     | `cmd/sim`        | ✔ working, tested, documented           |
| Kernel, built for the BESM-6  | `kernel/`        | ✔ builds and links                      |
| Memory management (the MMU)   | `kernel/utab.c`  | ✔ retargeted, tested under SIMH         |
| Boot, traps, context switch   | `kernel/besm6.S` | ◐ skeleton — the kernel cannot boot yet |
| Peripheral drivers            | `kernel/dev/`    | ◐ console done; the rest to do          |
| libc library                  | —                | ☐ to do                                 |

## Building

The `cmd/` toolchain builds with a top-level CMake project (driven through a thin
`Makefile`); the kernel has its own Makefile and cross-compiles with the tools that build
installs, so the toolchain comes first.

**Toolchain** — from the repo root:

```sh
make            # configure and build every cmd/ tool into build/
make run        # build and run the unit tests (ctest)
make install    # install the tools as b6* into ~/.local (or /usr/local)
```

Building requires CMake and a host C/C++ compiler; GoogleTest is fetched
automatically, and every `cmd/` component has a unit-test suite run by `make run`.

**Kernel** — cross-compiled for the BESM-6 with `b6cc`/`b6as`/`b6ld`:

```sh
cd kernel && make          # produces `unix`, a BESM-6 a.out, plus unix.nm and unix.dis
cd kernel/test && make test   # run the kernel's SIMH tests
```

The kernel tests need the [SIMH simulator](doc/Simh_Simulator.md) on the path as `besm6`.

See [CLAUDE.md](CLAUDE.md) for deeper build and architecture detail, and
[kernel/TODO.md](kernel/TODO.md) for the state of the retarget.

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
  calls; its CLI, tracing, syscall set, and a worked example.

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

- [kernel/TODO.md](kernel/TODO.md) — the live work plan for the memory-management retarget: the
  design the machine forces, the hardware rules every part of it obeys, and what is left to do.
- [doc/Kernel_Assembly_Routines.md](doc/Kernel_Assembly_Routines.md) — the machine-language
  assist: what each routine must do, the contract it owes its C callers, and — routine by
  routine — what the BESM-6 version (`kernel/besm6.S`) has to do differently from the x86
  original it is derived from.
- [doc/Unix_Context_Switch.md](doc/Unix_Context_Switch.md) — how this kernel takes an interrupt,
  takes an extracode, saves the CPU context, switches address spaces and gets back out: the four
  gates, the trap frame, the exit through `выпр`, `sureg()` and the u-area copy.
- [doc/Dubna_Context_Switch.md](doc/Dubna_Context_Switch.md) — the same five questions answered by
  Dubna, a BESM-6 operating system that ran on the real machine for two decades. The companion
  piece: several of the idioms above are taken from it.

## Related projects

- [besm6/c-compiler](https://github.com/besm6/c-compiler/) — C cross-compiler for the BESM-6.
- [besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) — authentic BESM-6 hardware
  simulator; the machine this port targets. Documented locally in
  [doc/Simh_Simulator.md](doc/Simh_Simulator.md) (operator's view) and
  [doc/Besm6_Peripherals.md](doc/Besm6_Peripherals.md) (programmer's view).

## License

The Unix v7 portions are distributed under the Caldera BSD-style license. See
[COPYRIGHT](COPYRIGHT) for the full notice.
