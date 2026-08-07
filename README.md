# Unix v7 for the BESM-6

Seventh Edition Unix, ported to the **BESM-6** — the Soviet mainframe that was the most widely
used high-performance computer of its era. It is an unusual machine: no bytes, a 48-bit word as
the smallest addressable thing, its own instruction set and its own floating-point format. It
boots, it runs multi-user, and it runs on the
[SIMH](https://github.com/besm6/simh/tree/master/BESM6/) hardware simulator.

## You can program this machine again

That is the point of it. The BESM-6 never ran Unix, the last of the hardware was scrapped
decades ago, and most of the software written for it is gone. Now there is a system on it where
you write a program, build it and run it — **on the machine itself**, at a shell prompt:

```sh
$ cat hello.S
#include <sys/syscall.h>

        .text
        .globl  main
main:
        xta     #1          // arg 1: file descriptor 1, the standard output
        xts     msgptr      // arg 2: pushes arg 1, A := the message pointer
        xts     #MSGLEN     // arg 3, the last, so it stays in A; pushes arg 2
        $77     SYS_write   // write(1, msgptr, MSGLEN)

        xta     #0          // exit's one argument is the status
        $77     SYS_exit    //   ...and this does not return

        .data
msgptr: .word  0'64 + message       // fat pointer
message: .ascii "Hello BESM-6!\n"   // message contents
MSGLEN = 14                         // length in bytes

$ cc -o hello hello.S
$ ./hello
Hello BESM-6!
```

**Assembly with the full C preprocessor** — `#include`, `#define`, conditionals and macros —
so the program above pulls the real system-call numbers out of `/usr/include/sys/syscall.h`
rather than repeating them. `cc` drives the chain: `cpp` preprocesses, `as` assembles, `ld`
links against `/lib/crt0.o` and the C library in `/lib/libc.a`. All of it is on the disk, and
so is the whole of `/usr/include`. You can call any of libc from assembly — `printf`, `malloc`,
`open`, `read` — because the calling convention is documented and the library is right there.

Also on the machine: `ar` and `ranlib` to build your own libraries, `nm`, `size` and `strip`
to look at what you built, and `disasm` to read a binary back as assembly. Ten of the
toolchain's programs are the same source code as the cross-tools that build the system on your
workstation, and they produce identical output — the machine's own assembler and linker
reproduce the whole kernel, byte for byte.

What is **not** there yet is a C compiler on the machine. `cc` preprocesses, assembles and
links; it does not compile C, and says so plainly. Cross-compiling C from a modern host works
today (that is how the kernel and the libraries are built).

## Try it

You need [CMake](https://cmake.org/), a C++ compiler, the
[BESM-6 C cross-compiler](https://github.com/besm6/c-compiler/) and the
[SIMH simulator](https://github.com/besm6/simh/tree/master/BESM6/) on your path as `besm6`.

```sh
make                    # build the toolchain, the kernel and the disk image
cd kernel && make run   # boot it, and type at the shell yourself
```

You get a single-user root shell first, at a `#` prompt. Type `^D` and the system comes up
multi-user with a `login:` on the console — log in as `guest`, no password. `/usr/guest/hello.S`
is the program above, ready to build.

## What works

- **Multi-user Unix.** `init` runs the boot script and puts a `login:` prompt on both Consul
  typewriters; two people log in at once, see each other in `who` and `write` to one another.
  `passwd`, `su` and `newgrp` do what they always did. Terminals are eight-bit and handle
  UTF-8, so Cyrillic prints correctly.
- **The kernel.** Memory management, processes, signals, pipes, swapping, shared program text,
  the v7 system calls. It runs on real simulated hardware — MMU, drums, disks, the console.
- **A filesystem you can look after.** `mkfs` makes one on a second drive, `mount` attaches
  it, `fsck` repairs a damaged one, `tar` archives the tree, `df` and `du` measure it.
- **92 programs.** The shell and its scripts, the file commands, two dozen text filters
  (`grep`, `sed`, `sort`, `diff`, `tr`, …), two editors — `ed` and the full-screen `novi` —
  the system-inspection set (`ps`, `vmstat`, `iostat`, `dmesg`), and the toolchain above.
- **The C library.** libc, libm, libtermcap and libcurses, cross-built and tested under a
  user-level simulator on the host.

Everything above is built and most of it is tested: `make run` is the whole suite.

## What is missing

A **C compiler on the machine**, and **`yacc` and `lex`** — six of the remaining v7 programs
are grammars (`expr`, `egrep`, `m4`, `make`, `bc`, `awk`) and none can be built until yacc
exists. `at`, `cron` and `calendar` need a clock the hardware has not got. The typesetting
suite, tape utilities, `uucp` and the PDP-11 compiler internals are not coming at all, and
[cmd/TODO.md](cmd/TODO.md) says why for each. The kernel has no work plan left; its reference is
[doc/Besm6_Kernel_Reference.md](doc/Besm6_Kernel_Reference.md).

## Repository layout

```text
kernel/        the v7 kernel and its device drivers
include/       the system headers
lib/           libc, libm, libtermcap, libcurses
cmd/           the toolchain, and the ~90 programs that go on the disk image
etc/           the static files of the image: passwd, group, motd, rc, ttys, termcap
root.manifest  how all of that is assembled into the root filesystem
doc/           BESM-6 architecture and toolchain references
```

Building is one CMake project behind a thin top-level `Makefile`. `make` builds everything into
`build/`; the top-level `make run` runs the tests; `make install` puts the `b6`-prefixed
cross-tools (`b6cc`, `b6as`, `b6ld`, …) into `~/.local`. See [CLAUDE.md](CLAUDE.md) for the
details.

## Documentation

**The machine** — [instruction set](doc/Besm6_Instruction_Set.md),
[calling conventions](doc/Besm6_Calling_Conventions.md),
[data representation](doc/Besm6_Data_Representation.md),
[peripherals](doc/Besm6_Peripherals.md), [memory mapping](doc/Memory_Mapping.md),
[compiler intrinsics](doc/Intrinsics.md).

**The toolchain** — [assembler](doc/Assembler_Manual.md), [linker](doc/Linker_Manual.md),
[archiver](doc/Archiver_Manual.md), [runtime library](doc/Besm6_Runtime_Library.md),
[file magic](doc/File_Magic.md).

**The simulators** — [SIMH, the full machine](doc/Simh_Simulator.md),
[b6sim, a user-level `a.out` runner](doc/Aout_Simulator.md).

**The kernel** — [kernel/README.md](kernel/README.md) is an article on how it works and
[the maintainer's reference](doc/Besm6_Kernel_Reference.md) is the rest of it; alongside them,
[system calls](doc/Unix_V7_System_Calls.md), [context switching](doc/Unix_Context_Switch.md),
[the assembly routines](doc/Kernel_Assembly_Routines.md), and
[how Dubna did it](doc/Dubna_Context_Switch.md) — the BESM-6 operating system that ran on the
real machine for two decades.

**The userland** — [cmd/README.md](cmd/README.md) is the manual for porting a v7 command to
this machine; most `cmd/<program>/` directories have a `README.md` of their own.

## Related projects

- [besm6/c-compiler](https://github.com/besm6/c-compiler/) — the C cross-compiler.
- [besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) — the hardware simulator.

## License

The BESM-6 port — the toolchain, the retargeted kernel and the documentation — is
Copyright (c) 2025-2026 Serge Vakulenko, under the MIT license.

The Unix v7 portions it builds on are under the Caldera BSD-style license, and the kernel
sources descend from Robert Nordier's v7/x86 port, whose modifications carry his own BSD-style
notice. See [COPYRIGHT](COPYRIGHT) for all of these in full.
