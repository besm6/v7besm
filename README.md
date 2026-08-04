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
trap doors, the timer and the context switch all work; processes alternate under the real scheduler,
each seeing its own u-area, with the user stack growing on demand; and a machine squeezed below its
own working set swaps real images through the drum while two processes share one copy of a text
segment. The console, drum and disk drivers are written and their failure modes classified.

**It is a multi-user Unix.** `b6fsutil` (`cmd/fsutil/`) builds a root filesystem image in the
kernel's own on-disk layout; the kernel mounts it, hands process 1 the icode, **enters user mode**
and execs `/etc/init` — the real v7 one — which prompts `#` on the SIMH console. `^D` there carries
init through `/etc/rc` and into the state a Unix spends its life in: `/etc/ttys` names both Consul
typewriters, `/etc/getty` puts a `login:` on each, `/bin/login` checks a password through `crypt(3)`,
writes `/etc/utmp`, hands the terminal over and execs a shell — and when that shell exits, init
respawns the getty for the next person. Two people log in at once on the two terminals, see each
other in `who`, and `write` to one another; `passwd`, `su`, `newgrp` and `mesg` change who they are
and who may reach them, and the kernel does the erase, kill, interrupt, quit and end-of-file
processing on both lines, in eight-bit UTF-8, with the erase character rubbing out on the screen.

**And the machine builds its own programs.** Ten of the host tools are built a second time from the
same sources, as `/usr/bin/{cpp,as,ld,nm,size,strip,disasm,ar,ranlib,cc}`: the native
`as` and `ld` reproduce the host tools' objects and images **byte for byte** — the whole kernel and
the toolchain itself included — the native `ar` and `ranlib` build `libc.a`, archive and `__.SYMDEF`
both, byte for byte too, and `cc -o demo hello.S` on the image drives `cpp`, `as` and `ld` against
`/lib/crt0.o` and `/lib/libc.a` and produces a runnable `a.out` — `/usr/guest/hello.S` is the demo
the image ships, and typing that one command is the whole of it. `/lib` and the whole
`/usr/include` are on the disk for it. What `cc` cannot yet do is compile C — the parser, lowerer
and code generator are the external cross-compiler's and cannot be built for the target — and it
says so rather than reporting a missing file.

**It administers itself.** `mkdir`, `rmdir`, `cp`, `ln`, `mv` and `rm` build and rearrange the tree
(three of them setuid root, because there is no `mkdir(2)` or `rename(2)` here), while `chmod`,
`chown`, `chgrp` and `touch` change what a file *is*. `mkfs` makes a filesystem on a second drive,
`mount` and `umount` attach and detach it, `dd` moves bulk data through the raw device, `fsck`
repairs a damaged one and `icheck`, `dcheck`, `ncheck` and `clri` are the tools it grew out of;
`df`, `du` and `quot` measure the store, and `tar` archives the tree onto another volume and reads
it back. `ps`, `vmstat`, `iostat`, `dmesg` and `pstat` look inside the running kernel — through
`kctl(2)`, a small kernel-variable interface that stands in for the `nlist(3)`-over-`/unix`
rummaging every earlier Unix did, there being no kernel image on this disk to rummage in. Together
with `statfs(2)`, which is where `df` gets its numbers, it is what lets an ordinary user ask these
questions without being able to read core: only `pstat` still climbs through `/dev/kmem`.

**And it edits, filters and branches.** `novi` is a full-screen editor — Dave W Plummer's, the one
program here that is not a v7 port, keeping the document on disk in two unlinked `/tmp` files with a
gap at the cursor rather than in a memory this machine has not got; `ed` is here too, eight-bit.
Two dozen text filters — `grep`, `fgrep`, `sed`, `sort`,
`diff`, `tr`, `uniq`, `comm`, `tail`, `od`, `col`, `pr`, `join`, `tsort`, `file`, `find`, `look`,
`cal` and the rest — run in pipelines the shell builds, `test` (and `[`, its second name and the
image's one hard link) lets a script branch, `date` sets the clock, `sleep` waits on an alarm the
kernel delivers, `kill` signals another process, `time` measures a command against a 250 Hz clock
and `yes` runs until a `SIGPIPE` down a pipeline stops it.

The **libc runs on it too**: the test programs of [lib/test/](lib/test/) live on the image as
`/usr/test/*` and produce there, byte for byte, the same output they produce under
`b6sim` — stdio, `malloc`, `setjmp`, the exec family, signals, `<time.h>`, `opendir(3)`, the passwd
file, and a shell started through `system()`/`popen()`. Running one suite under two independent
harnesses is what turns a disagreement into a bug report: under `b6sim` every system call is served
by the host, so a kernel fault cannot show, and the two answering differently means one of them is
wrong.

[kernel/README.md](kernel/README.md) is the reference: the design the machine forces and the
hardware rules every part of it obeys. [kernel/TODO.md](kernel/TODO.md) and
[cmd/TODO.md](cmd/TODO.md) are the work plans, task by task.

Alongside the running kernel, [kernel/test/](kernel/test/) holds standalone SIMH tests: each links
kernel objects against a hand-built environment and lets a `.ini` script assert on the machine state
afterwards. That is how the MMU and the mass-storage drivers were verified. Seventeen more boot the
whole kernel against the disk image, each going one step past the last, so that a failure names its
own layer: the prompt appears, a typed dialogue works, a session writes files that fsck clean, the
file-management set rearranges a tree, the libc suite runs off the image, the machine swaps, two
people log in on the two Consuls at once and change their own passwords, the filters run in
pipelines, the filesystem tools make and repair a second volume, `tar` archives the tree onto it,
the inspection programs read the live kernel — and the machine builds and runs a program with its
own `cc`.

## Repository layout

```text
kernel/        v7 kernel sources, device drivers (kernel/dev/), the design (README.md) and
               the work plan (TODO.md)
kernel/test/   SIMH tests: standalone component tests, and seventeen that boot the whole
               kernel and drive it by typing at it
include/       v7 system headers (sys/), the hosted half of the C11 header tree
lib/           libc, libm, libtermcap, libcurses and crt0, cross-compiled; lib/test/ is
               the suite that exercises them
cmd/           the BESM-6 toolchain as host tools (cc, cpp, as, ld, the binutils, disasm,
               sim, fsutil), and the ninety-odd native programs that go on the disk image:
               init, getty, login, sh, the file-management and account commands, the
               filesystem tools, two dozen text filters, ed and novi, tar, the kernel
               inspection set, and ten of the toolchain built a second time for the machine
               itself (cmd/<tool>/rootfs/)
etc/           the static files of the image: group, motd, passwd, rc, termcap, ttys
root.manifest  what all of that is assembled into: the root filesystem the kernel mounts
cross/         BESM-6 object/archive format headers (b.out.h, ar.h, ranlib.h)
scripts/       the shared CMake cross-toolchain module, build checks, and a VSCode
               grammar for BESM-6 assembly (scripts/vscode-besm6/)
doc/           BESM-6 architecture references
```

## Status

**It runs multi-user, and everything in this table is tested.** `make run` is 1,257 cases, 23 of
them under the `kernel` label — the standalone SIMH tests and the image checks. `make weekly` adds
52 more, and seventeen of those boot the whole kernel and drive it by typing at it.

|part|where|what works|
|---|---|---|
|Toolchain, host|`cmd/`|`cc`, `cpp`, `as`, `ld`, `disasm`, the binutils, `b6sim` and `b6fsutil` — each working, unit-tested and documented|
|Toolchain, native|`cmd/*/rootfs/`|`/usr/bin/{cpp,as,ld,nm,size,strip,disasm,ar,ranlib,cc}` + `lorder` — the machine assembles, links, archives and indexes its own programs, and reproduces the host tools' output byte for byte. `cc` does not compile C yet|
|Kernel|`kernel/`|boots under SIMH: the MMU, the three trap doors, the context switch, console/drum/disk drivers, the v7 system-call set, swapping and shared text, `/dev/mem` and `/dev/kmem`, plus `kctl(2)` and `statfs(2)`|
|Filesystem|`kernel/`|mounts a root, reads and writes it, fscks clean on the host afterwards; `mkfs` makes a second volume, `mount`/`umount` attach it, `fsck` repairs it|
|Libraries|`lib/`|libc, libm, libtermcap, libcurses and `crt0.o` — the suite runs under `b6sim` **and** off the disk image, diffed against the same expectations|
|Userland|`cmd/`|92 programs and one hard link on the image — six of them setuid root|
|Terminals|—|both Consul typewriters, eight-bit and UTF-8, with erase, kill, interrupt, quit and end-of-file|
|Multi-user|—|`/etc/rc`, `getty`, `login`, `crypt(3)`, `/etc/utmp`, `passwd`, `su`, `newgrp`, `who`, `write`, `wall`, `mesg` — **two people logged in at once, on the two terminals**|

**Not there yet.** The largest gap is `yacc` and `lex` ([cmd/TODO.md](cmd/TODO.md) task C10): six
of the programs still to come are yacc grammars — `expr`, `egrep`, `m4`, `make`, `bc` and `awk`,
the last a lex scanner besides — so none of them can start until `b6yacc` exists, and `make` is the
one that costs most to be without. `cc` compiling C on the machine waits on the external
[cross-compiler](#related-projects) being retargetable to the BESM-6 itself. `at`, `cron` and
`calendar` wait on a clock this hardware has not got — there is no clock-calendar a program can
read, so `time` is seeded from the root superblock at boot. Eight kernel items were deferred
deliberately and are listed in [kernel/TODO.md](kernel/TODO.md), the two visible ones being the
4,096-word user stack and the guest's non-reproducible timing that keeps the `console` and `edit`
tests disabled. What is not coming at all — the typesetting suite, tape, `uucp`, plotters, the
PDP-11 compiler internals — is a table of decisions with line counts at the foot of
[cmd/TODO.md](cmd/TODO.md), and [cmd/README.md](cmd/README.md) is the porting manual for
everything that is.

## Building

**One CMake project**, driven through a thin top-level `Makefile`. It builds three kinds of
thing: the `cmd/` host tools, the cross-built BESM-6 artifacts (`kernel/`, `lib/`), and the
native BESM-6 programs staged into `build/rootfs/` for the disk image. The cross-builds use the
**in-tree** tool targets, so a rebuilt `b6as` relinks the kernel with no `make install` in
between; the Makefiles under `kernel/` and `lib/` are thin wrappers over the same `build/` tree.

**Toolchain** — from the repo root:

```sh
make            # configure and build everything into build/
make run        # run the daily suite (ctest, less the `weekly' label)
make install    # install the tools as b6* into ~/.local (or /usr/local)
```

Building requires CMake and a host C/C++ compiler; GoogleTest is fetched
automatically, and every `cmd/` component has a unit-test suite run by `make run`.

**Library and root filesystem** — `lib/` (`libc.a`, `libm.a`, `libtermcap.a`, `libcurses.a`,
`crt0.o`) is part of the same
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
cd kernel && make run          # boot it under SIMH and type at the shell yourself
cd kernel/test && make test    # run the kernel's fast SIMH tests
```

The kernel tests need the [SIMH simulator](doc/Simh_Simulator.md) on the path as `besm6`; they
are not host unit tests but BESM-6 programs the real simulator runs. `ctest` labels carve the
suite up — `kernel` (SIMH), `lib` (the libc programs under `b6sim`), `rootfs` (size checks on
the programs staged for the image) and `sh` (the shell's own scripts). A fifth, `weekly`, sits
on top of `kernel` and marks the tests that boot the whole machine and drive it by typing at
it: they hold one resource lock and so run one at a time, about seventy seconds of serial wall
clock, so the daily `make run` leaves them out and `make weekly` is what runs them. `make test`
under `kernel/test/` rebuilds `root.img` first; a bare `ctest -L kernel` tests a stale image.

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
- [kernel/TODO.md](kernel/TODO.md) — the work plan: the eight kernel items deferred deliberately,
  one scoped task each.
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

**The userland**:

- [cmd/README.md](cmd/README.md) — the porting manual: the eleven-point recipe a v7 command is
  taken through, the hazards its source walks into on a word-addressed machine, how a program
  gets onto the image, and which harness tests it.
- [cmd/TODO.md](cmd/TODO.md) — what is left, one task per program — and, at the foot, the table
  of what is not coming and why, with the line count of each so the decision can be re-examined.
- Most `cmd/<prog>/` directories carry a `README.md` of their own; the ten tools built twice
  each have a "Building for the BESM-6" section with the size measurements that made it fit.

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
