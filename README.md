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

The kernel compiles and links as BESM-6 code with this project's own toolchain and the external
[cross-compiler](#related-projects), and it **boots on the [SIMH simulator](doc/Simh_Simulator.md)** —
the authentic full-machine emulator, and the hardware this port ultimately targets. The
machine-dependent half is retargeted: the memory model, the mapped brackets, `_start`, all three
trap doors, the timer and the context switch all work, and processes alternate under the real
scheduler, each seeing its own u-area, with the user stack growing on demand. The console, drum and
disk drivers are written and their failure modes classified.

**It boots to a shell.** `b6fsutil` (`cmd/fsutil/`) builds a root filesystem image in the kernel's
own on-disk layout; the kernel mounts it, hands process 1 the icode, **enters user mode** and execs
`/etc/init` — the real v7 one — which forks `/bin/sh` and prompts with `# ` on the SIMH console.
Type at it: the shell runs `cat`, `echo`, `ls`, `pwd` and `sync` off the disk, the kernel does the
erase, kill and end-of-file processing, and `^D` takes init round through `/etc/rc` to a fresh
prompt. It also **writes** — create files, `sync`, and the image fscks clean on the host afterwards.

The **libc runs on it too**: the test programs of [lib/test/](lib/test/) live on the image as
`/usr/test/*` and produce there, byte for byte, the same output they produce under
`b6sim` — stdio, `malloc`, `setjmp`, the exec family, signals, `<time.h>`, the passwd file, and a
shell started through `system()`/`popen()`. Running one suite under two independent harnesses is
what turns a disagreement into a bug report: under `b6sim` every system call is served by the host,
so a kernel fault cannot show, and the two answering differently means one of them is wrong.

What remains is the multi-user road — `getty`, `login`, and a driver for the 24-line terminal
multiplexer — and a handful of smaller leftovers. [kernel/README.md](kernel/README.md) is the
reference: the design the machine forces and the hardware rules every part of it obeys.
[kernel/TODO.md](kernel/TODO.md) is the work plan, task by task.

Alongside the running kernel, [kernel/test/](kernel/test/) holds standalone SIMH tests: each links
kernel objects against a hand-built environment and lets a `.ini` script assert on the machine state
afterwards. That is how the MMU and the mass-storage drivers were verified. Four more boot the whole
kernel against the disk image, each going one step past the last, so that a failure names its own
layer: the prompt appears, a typed dialogue works, a session writes files that fsck clean, and the
libc suite runs off the image.

## Repository layout

```text
kernel/        v7 kernel sources, device drivers (kernel/dev/), the design (README.md) and
               the work plan (TODO.md)
kernel/test/   SIMH tests: standalone component tests, and four that boot the whole kernel
include/       v7 system headers (sys/), the hosted half of the C11 header tree
lib/           libc, libm and crt0, cross-compiled; lib/test/ is the suite that exercises them
cmd/           BESM-6 toolchain (cc, as, ld, cpp, disasm, sim, fsutil) and the native
               programs that go on the disk image (init, sh, cat, echo, ls, pwd, sync)
etc/           the static files of the image: group, motd, passwd, rc
root.manifest  what all of that is assembled into: the root filesystem the kernel mounts
cross/         BESM-6 object/archive format headers (b.out.h, ar.h, ranlib.h)
scripts/       the shared CMake cross-toolchain module, build checks, and a VSCode
               grammar for BESM-6 assembly (scripts/vscode-besm6/)
doc/           BESM-6 architecture references
```

## Components and status

| Component                     | Location           | Status                                      |
|-------------------------------|--------------------|---------------------------------------------|
| C compiler driver             | `cmd/cc`           | ✔ working, tested, documented               |
| Assembler (AT&T / Madlen)     | `cmd/as`           | ✔ working, tested, documented               |
| Linker + binutils             | `cmd/ld`           | ✔ working, tested, documented               |
| C preprocessor                | `cmd/cpp`          | ✔ C11, tested, documented                   |
| Disassembler                  | `cmd/disasm`       | ✔ working, tested                           |
| a.out simulator (Unix v7)     | `cmd/sim`          | ✔ working, tested, documented               |
| Filesystem image builder      | `cmd/fsutil`       | ✔ working, tested, documented               |
| Kernel, built for the BESM-6  | `kernel/`          | ✔ builds, links and boots under SIMH        |
| Memory management (the MMU)   | `kernel/utab.c`    | ✔ retargeted, tested under SIMH             |
| Boot, traps, context switch   | `kernel/besm6.S`   | ✔ working — processes switch under SIMH     |
| Peripheral drivers            | `kernel/dev/`      | ✔ console, drum and disk                    |
| Mounting a root filesystem    | `kernel/`          | ✔ mounts, reads and writes; fscks clean     |
| `exec` of a BESM-6 `a.out`    | `kernel/sys1.c`    | ✔ user mode, argv/envp, shared text         |
| System calls                  | `kernel/sysent.c`  | ✔ the v7 set, exercised from the image      |
| libc / libm / crt0            | `lib/`             | ✔ tested under `b6sim` **and** on the image |
| Userland (`init`, `sh`, …)    | `cmd/`             | ✔ v7 init, Bourne shell, five commands      |
| Single-user shell prompt      | —                  | ✔ **it prompts, and you can type at it**    |
| Swapping and shared text      | `kernel/text.c`    | ☐ to do — written, never run under load     |
| `/dev/mem`, `/dev/kmem`       | `kernel/dev/mem.c` | ☐ to do — minors 0 and 1 give `ENXIO`       |
| Multi-user (`getty`, `login`) | `kernel/dev/sr.c`  | ☐ to do — the multiplexer is a skeleton     |

## Building

**One CMake project**, driven through a thin top-level `Makefile`. It builds three kinds of
thing: the `cmd/` host tools, the cross-built BESM-6 artifacts (`kernel/`, `lib/`), and the
native BESM-6 programs staged into `build/rootfs/` for the disk image. The cross-builds use the
**in-tree** tool targets, so a rebuilt `b6as` relinks the kernel with no `make install` in
between; the Makefiles under `kernel/` and `lib/` are thin wrappers over the same `build/` tree.

**Toolchain** — from the repo root:

```sh
make            # configure and build everything into build/
make run        # run every test (ctest)
make install    # install the tools as b6* into ~/.local (or /usr/local)
```

Building requires CMake and a host C/C++ compiler; GoogleTest is fetched
automatically, and every `cmd/` component has a unit-test suite run by `make run`.

**Library and root filesystem** — `lib/` (`libc.a`, `libm.a`, `crt0.o`) is part of the same
build, cross-compiled by the `b6*` tools built alongside it, and so is `build/rootfs/`, the tree
that becomes the disk image. A fresh checkout needs just:

```sh
make && make install    # tools, kernel, lib/ and rootfs/; installs include/ + the archives
```

`make` builds the archives with the in-tree tools, and `make install` puts them into
`share/besm6/lib`; `crt0.o` landing there is what makes `b6cc` able to link at all (until then
it can compile and assemble but not produce an executable). The one thing this repo does *not*
build is `libruntime.a`, the `b$*` compiler-support helpers, which come from the
[c-compiler](https://github.com/besm6/c-compiler/) and can come from nowhere else.

**Kernel** — cross-compiled for the BESM-6 with `b6cc`/`b6as`/`b6ld`:

```sh
cd kernel && make            # produces `unix`, a BESM-6 a.out, plus unix.nm and unix.dis
cd kernel && make run        # boot it under SIMH and type at the shell yourself
cd kernel/test && make test  # run the kernel's SIMH tests
```

The kernel tests need the [SIMH simulator](doc/Simh_Simulator.md) on the path as `besm6`; they
are not host unit tests but BESM-6 programs the real simulator runs. `ctest` labels carve the
suite up — `kernel` (SIMH), `lib` (the libc programs under `b6sim`), `rootfs` (size checks on
the programs staged for the image) and `sh` (the shell's own scripts).

See [CLAUDE.md](CLAUDE.md) for deeper build and architecture detail, and
[kernel/README.md](kernel/README.md) for the state of the retarget.

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
- [doc/Intrinsics.md](doc/Intrinsics.md) — the twelve `<besm6.h>` compiler intrinsics that let the
  kernel issue `002 «рег»`, `033 «увв»`, the PSW instructions and the bit-manipulation
  instructions from C rather than assembly.

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

- [kernel/README.md](kernel/README.md) — the reference: the design the machine forces, the hardware
  rules every part of it obeys, what each SIMH test is really asserting, what a standalone test
  costs to get right, and the consequences deliberately accepted.
- [kernel/TODO.md](kernel/TODO.md) — the work plan: the road to multi-user, and the smaller
  leftovers, one scoped task each.
- [doc/Kernel_Assembly_Routines.md](doc/Kernel_Assembly_Routines.md) — the machine-language
  assist: what each routine must do, the contract it owes its C callers, and — routine by
  routine — how `kernel/besm6.S` and its companion files satisfy it.
- [doc/Unix_Context_Switch.md](doc/Unix_Context_Switch.md) — how this kernel takes an interrupt,
  takes an extracode, saves the CPU context, switches address spaces and gets back out: the four
  gates, the trap frame, the exit through `выпр`, `sureg()` and the u-area copy.
- [doc/Unix_V7_System_Calls.md](doc/Unix_V7_System_Calls.md) — the system calls this kernel
  implements: the `$77` gate and its argument convention, a brief entry for each call, and where
  a call had to change shape because the machine is word-addressed.
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

The BESM-6 port — the toolchain, the retargeted kernel and the documentation — is
Copyright (c) 2025-2026 Serge Vakulenko, under the MIT license.

The Unix v7 portions it builds on are distributed under the Caldera BSD-style license, and the
kernel sources descend from Robert Nordier's v7/x86 port, whose modifications carry his own
BSD-style notice. See [COPYRIGHT](COPYRIGHT) for all of these in full.
