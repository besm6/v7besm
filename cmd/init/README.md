# init — `/etc/init` for the BESM-6 Unix v7 port

Process 1: the program the kernel's icode execs, and the one that never exits. It brings the
system up single-user, runs `/etc/rc`, and — once there is a terminal driver to do it with —
keeps a `getty` on every line in `/etc/ttys`.

This is the **first native BESM-6 program in `cmd/`**, and that is the thing to know about
this directory. Every other `cmd/` subdirectory is a *host* tool: `b6cc`, `b6as`, `b6ld` and
the rest are compiled by the build machine's C compiler and run there. Nothing here runs on
the build machine. `init.c` is compiled by the `b6*` toolchain those tools make up, linked
against the libc built in [`../../lib`](../../lib), and staged as **`build/rootfs/etc/init`** —
the `/etc/init` of the root filesystem the kernel mounts.

## Building

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make                    # builds build/rootfs/etc/init among everything else
make run                # runs its size check (ctest label `rootfs') with the rest
```

Two conditions gate it, both shared with `kernel/` and `lib/`: the external
[c-compiler](https://github.com/besm6/c-compiler/) must be installed (the top-level
`CMakeLists.txt` guards on its `libruntime.a`), and libc must build, since the link is
`crt0.o init.o -lc -lruntime` in exactly that order — the archive-scan contract in
[`../../lib/README.md`](../../lib/README.md).

[`CMakeLists.txt`](CMakeLists.txt) is six lines, because the work is in `b6_prog()` in
[`../../scripts/BesmCross.cmake`](../../scripts/BesmCross.cmake). Any further native program
— a shell, `cat`, or `cmd/cpp` built a second time for the target
([`../cpp/TODO.md`](../cpp/TODO.md)) — is one more `b6_prog()` call.

## The source

`init.c` is the Unix v7 `/etc/init`, and does what v7's does. What changed is what C11
requires of a source written for a compiler that defaulted everything to `int`: `b6parse` has
no implicit `int`, no K&R parameter lists and no untyped `register i;`. So every function has
a prototype and an explicit return type; the K&R definitions became prototypes; the flag
arguments are spelled `O_RDWR` and `SEEK_END` rather than written as the small integers v7
used. `merge()` and `reset()` are signal handlers, so C11 gives them the `void (*)(int)` shape
`<signal.h>` declares — v7 could hand `signal()` a niladic function, C11 cannot.

The port also needed four declarations the header tree did not have: `kill()` (now in
`<signal.h>`), `wait()` (a new `<sys/wait.h>`), and `chmod()` with its four neighbours (now in
`<sys/stat.h>`). And `struct utmp`'s `ut_time` is a `time_t`, where v7 wrote `long`: the same
one word on this machine, so the record's layout is unchanged, but `time()` takes a `time_t *`
and the `long` did not compile.

## What it does today, and what it does not

With no `/etc/ttys` on the root image, `merge()` returns as soon as the open fails and
`multiple()` falls straight through. So this init is exactly the single-user loop the port
needs right now: shut everything down, a shell on `/dev/console`, `/etc/rc`, and around again
when the shell exits. `getty`, `login` and the multi-user half wait on a terminal driver —
[`../../kernel/TODO.md`](../../kernel/TODO.md), task 29.

It **is the image's `/etc/init`** (task 25b). Under the real kernel it forks, opens
`/dev/console`, `dup`s, execs `/bin/sh`, waits, runs `/etc/rc` and cycles — the boot reaches the
shell's root prompt, `# `, and that prompt is what [`kernel/test/boot`](../../kernel/test/boot.ini.in)
asserts. [`kernel/test/console`](../../kernel/test/console.ini) goes a step further and sends `^D`
at the shell, which drives one whole turn of the loop: the shell exits, `runcom()` runs
`/etc/rc`, the motd appears, and `single()` prompts again.

Getting here needed one thing below this program: **the kernel stack was not big enough to run
the real shell**, and booting it wrapped `r15` past `0100000` into the interrupt vectors. The
measurement and the fix — `UBASE` down one page, so the u-area spans two — are task 25a in
[`../../kernel/TODO.md`](../../kernel/TODO.md).

## The ceilings

A user program here lives in 32 pages of 1 Kword. The top four are the stack, based at
`070000`, so `const+text+data+bss` must fit **28,672 words**, and no symbol may sit above word
**32,767** — the reach of a 15-bit pointer. Both are checked on every build by
`scripts/check-size.sh`, registered as the ctest `rootfs_init_size`; the failure they catch is
otherwise silent, since the link succeeds and only the running program misbehaves.

`init` uses 1,207 words of the 28,672, so there is no pressure here. There will be in a
program the size of `cpp`; see [`../cpp/TODO.md`](../cpp/TODO.md), blockers L1 and L2.

`init.8` is the v7 manual page, kept as it was.
