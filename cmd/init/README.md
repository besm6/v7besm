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
([`../cpp/README.md`](../cpp/README.md), "Building for the BESM-6") — is one more `b6_prog()`
call.

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

## What it does today

The whole cycle runs. `/etc/ttys` names both Consuls (kernel task 29b), so `merge()` forks an
`/etc/getty` on each, `/bin/login` checks a password, and `multiple()` puts a fresh getty on the
line when a user logs out. Shut everything down, a shell on `/dev/console`, `/etc/rc`, a getty
per line, and around again on a hangup — [`kernel/test/login`](../../kernel/test/login.ini) and
[`kernel/test/multi`](../../kernel/test/multi.ini) drive it end to end.

## The one divergence: init says what it is doing

v7's init said nothing, anywhere, and could afford to: on a PDP-11 the operator had just typed
the boot line by hand and knew exactly what state the machine was in. This machine boots
itself, so everything on the console is the kernel's four size lines and then a bare `# ` —
which says neither that this is single-user nor what the shell is waiting for. So `single()`
writes a line of its own before it execs the shell, and by the same argument every other state
change this program makes says so too. Three of them are otherwise indistinguishable from a
working boot that has gone quiet: a missing `/etc/ttys`, an `/etc/ttys` that enables nothing,
and a system whose gettys have all died are three silent trips back to the single-user prompt in
v7.

| When | What the console says |
| --- | --- |
| `single()`, before the shell | `Single-user mode -- type ^D to run /etc/rc and go multi-user` |
| `runcom()`, before `/etc/rc` | `Going multi-user -- running /etc/rc and then a getty per terminal line` |
| `merge()` cannot open `/etc/ttys` | `No /etc/ttys to read -- no terminal line can be brought up` |
| `merge()` finds no usable line | `No line of /etc/ttys is enabled -- nobody can log in` |
| `multiple()` runs out of children | `Nothing is left running on any terminal line -- going back to single-user` |
| `SIGHUP` on the console | `Hangup on the console -- taking the system down to single-user` |
| `shutdown()`'s 60-second alarm | `A process would not die in 60 seconds -- starting the cycle over` |
| `execl` of the shell failed | `init: cannot execute /bin/sh` |
| `execl` of the getty failed | `init: cannot execute /etc/getty`, on that line's terminal |

State announcements read in the banner's voice; the two failures carry the program's name, as a
Unix program reporting its own trouble. Every one is a `write(2)` and not `printf`: init links no
stdio.

### Three rules any further message inherits

**Two are about the text.** Every SIMH test in [`../../kernel/test`](../../kernel/test) arms all
of its `expect` rules before the machine starts, and any rule can fire on anything in the console
stream. So no message may contain **`#`** — every test's first rule waits for the shell's `# `
prompt — and **no line may end in `.`**, because `kernel/test/edit` waits for `.\r\n`, the line
that ends an `ed` append. The first draft of the banner ended `go multi-user.` and fired that rule
before the shell had prompted, sending a `Z` into the middle of the boot. `edit.ini` has three
more rules that short, so no line may end in `a`, `ed` or `?` either. Check a new wording against
`grep -h '^ *expect' kernel/test/*.ini kernel/test/*.ini.in | sort -u` before believing a green
suite.

**The third is about placement, and it is why the multi-user line comes *before* `/etc/rc`
rather than at the transition it describes.** `login.ini`, `multi.ini` and `console.ini` each
match `GMT 2026\r\n\r\nlogin: ` as one string — the tail of the date `/etc/rc` prints, then the
first getty's prompt — so nothing may be written between those two events. Ahead of `/etc/rc` is
the last moment the announcement can be made, and the `login: ` is left to announce the arrival
itself.

### Why it all comes out of a child

`ttyopen()` in [`../../kernel/dev/tty.c`](../../kernel/dev/tty.c) makes the first process to open
a terminal the leader of its process group, and nothing ever clears `p_pgrp` again. An init that
opened `/dev/console` even once would therefore carry `p_pgrp = 1` for the life of the system;
every process would inherit it, and no shell, `getty` or `login` could claim a terminal after
that — `t_pgrp` would stay 0, so a `^C` would go to process group 0, and `u_ttyp` would stay
unset, so `/dev/tty` would answer `ENXIO` for every process on the machine. **So init's parent
opens nothing, ever.**

`single()`'s and `dfork()`'s children have the terminal on descriptor 1 already and write
straight to it; `runcom()`'s child has only the root directory there, so `tell()` opens the
console, writes and closes it — closing because `single()`'s child needs *its* open to land on
descriptor 0 for the two `dup()`s to fill 1 and 2. The five messages that arise in the *parent*
are left in `pending`, which `single()`'s child says just ahead of the banner: every one of them
is a reason the machine is on its way back to that prompt, so that is where they belong.
It also means the first pass through `main()` adds nothing to the boot stream, which is what
[`kernel/test/boot`](../../kernel/test/boot.ini.in) asserts on. The one event left unremarked is
a `SIGINT` making `merge()` re-read `/etc/ttys`: it is in the parent and leads nowhere near a
prompt, so saying it would cost a fork of its own.

It **is the image's `/etc/init`** (task 25b). Under the real kernel it forks, opens
`/dev/console`, `dup`s, execs `/bin/sh`, waits, runs `/etc/rc` and cycles — the boot reaches the
shell's root prompt, `# `, and that prompt is what [`kernel/test/boot`](../../kernel/test/boot.ini.in)
asserts. [`kernel/test/console`](../../kernel/test/console.ini) goes a step further and sends `^D`
at the shell, which drives one whole turn of the loop: the shell exits, `runcom()` runs
`/etc/rc`, the boot date appears, and — since there is an `/etc/ttys` now — `merge()` puts a
getty on each Consul rather than `single()` prompting again.

Getting here needed one thing below this program: **the kernel stack was not big enough to run
the real shell**, and booting it wrapped `r15` past `0100000` into the interrupt vectors. The
measurement and the fix — `UBASE` down one page, so the u-area spans two — are written up at
`UBASE` in [`../../include/sys/param.h`](../../include/sys/param.h); the geometry it produced is
in [`../../kernel/README.md`](../../kernel/README.md).

## The ceilings

A user program here lives in 32 pages of 1 Kword. The top four are the stack, based at
`070000`, so `const+text+data+bss` must fit **28,672 words**, and no symbol may sit above word
**32,767** — the reach of a 15-bit pointer. Both are checked on every build by
`scripts/check-size.sh`, registered as the ctest `rootfs_init_size`; the failure they catch is
otherwise silent, since the link succeeds and only the running program misbehaves.

`init` uses 1,207 words of the 28,672, so there is no pressure here. There is in a program the
size of `cpp`, which links at 23,826 and had to have its C11 limits cut to get there; see
[`../cpp/README.md`](../cpp/README.md), "Building for the BESM-6".

`init.8` is the v7 manual page, kept as it was.
