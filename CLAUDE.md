# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A port of **Unix v7 to the BESM-6**, a Soviet 48-bit-word mainframe from the 1960s. The
work has two halves:

- **`kernel/` + `include/`** — the Unix v7 kernel itself. Sources are derived from Robert
  Nordier's v7/x86 port; the upstream copyright is in the top-level `COPYRIGHT` file.
  The kernel **builds as BESM-6 code** with this
  repo's own toolchain (`b6cc`/`b6as`/`b6ld`) and **boots under SIMH**: the memory model,
  `_start`, all three trap doors, the timer and the context switch work, and two processes
  alternate under the real scheduler. With no root disk, boot stops at `panic: iinit`; with
  `root3072.disk` and a drum attached it mounts the root, hands process 1 the icode, **enters
  user mode**, execs `/etc/init` — the real v7 one — and **gives you a shell**: `/bin/sh`
  prompts with `# ` on the console, runs `ls`, `pwd`, `cat` and `echo` off the disk, honours
  the kernel's erase, kill and `^D`, and on `^D` cycles back through `/etc/rc` — which prints
  the **date**, a literal to the minute because the boot clock is the image's
  own `-T` stamp — to a fresh prompt. That prompt no longer arrives unannounced: `init`'s
  `single()` says which mode the machine is in and what `^D` does, a divergence from a v7
  whose operator had just typed the boot line by hand (`cmd/init/README.md`). The motd is
  `login(1)`'s alone since task 29b; `/etc/rc` cat'd it to the console until it became the
  second copy within a second or two, and the wording of the banner that replaced it is
  constrained by every `expect` in `kernel/test` — no `#`, and no line ending in `.`. It also **writes**: create files, make and remove directories with a **setuid-root**
  `mkdir`/`rmdir` (`ISUID` really does change a uid at exec — `lib/test/suidt` drops to uid 7
  and proves it), `sync`, and the image fscks clean. And it **rearranges** what it wrote:
  `cp`, `ln`, `rm` — `rm -r` execing that same setuid `/bin/rmdir` — and a `mv` that is the
  third setuid program, because renaming a directory into another parent is four `link`/`unlink`
  calls no ordinary user may make and a link count nothing but an fsck can check. And it
  **changes what a file is**, not merely where it lives: `chmod` (the whole symbolic grammar,
  and the only port in the set that needed no bounds check, having no buffer), `chown` and
  `chgrp` — neither setuid, because `chown(2)` is `suser()`-gated and that gate is what stops a
  user giving a file away — and a `touch` that calls `utime(2)` where v7's rewrote the file's
  first byte, the one deliberate divergence in the set. Their modes and owners are asserted on
  the *host*, out of `b6fsutil -v -v`, because `ls -l` prints a date beside them and no time
  here is reproducible: the guest clock advances about two seconds over a whole `files` run, so
  `ls -t` between two files a script made is a coin toss and only a fixture grafted with
  `b6fsutil -T <far-off>` can show a `touch` moving anything. And it **knows what time it is,
  waits, and signals**: `date` sets the clock through `stime(2)` — which takes the `time_t` by
  *value* here, the gate having shed a word with the PDP-11's two-word `long`, and which no
  caller had ever exercised until this one — `sleep` waits on an alarm the kernel has to
  *deliver*, and `kill` sends a signal that `sh`'s `wait` reports back as `0200|signo`. That
  `date` also names years past 1999 is a deliberate divergence: v7 wrote `year += 1900` flat, so
  the only program that can set this machine's clock could not name the century its `time_t`
  reaches into. And a **shell script can branch**: `test` is on the image, under both its names
  — `[` is a `link` stanza in `root.manifest` and the only hard link there is, without which the
  `argv[0]` half of the program is unreachable code — beside `basename`, `tty`, a `time` whose
  PDP-11 60 Hz arithmetic had to be rebuilt for this machine's 250, and a `yes` that nothing but
  a `SIGPIPE` can stop, which is what put **the first pipeline this image has ever run** into
  `kernel/test/utils.sh`. One finding from that set reaches past it: an **exit status above 127
  does not survive `wait(2)`** here, the status being `(code << 8)` returned through fifteen-bit
  r12, so `test`'s `exit(255)` reaches `$?` as 127 (`cmd/test/README.md`). And the whole
  **libc runs on it**: the `lib/test/` programs live on the image as `/usr/test/*`
  and produce there, byte for byte, the output they produce under `b6sim`. And it has **a second
  terminal**: `kernel/dev/sc.c` drives *both* of the machine's Consul-254 typewriters as
  `cdevsw[0]` minors 0 and 1 — one driver rather than two because their `ext` addresses are
  adjacent and their ПРП bits one apart, so a variable unit folds into the instruction — which
  puts `/dev/tty1` on the image beside `/dev/console`. (That is what became of the 24-line
  multiplexor `dev/sr.c` was a skeleton for; `sr.c` is
  gone, and `rmd`/`rmb` moved to majors 3 and 4 behind it.) And, task 29b, it is
  **multi-user**: `/etc/ttys` names both typewriters, `merge()` in the v7 `init` — code that had
  never run here, having had nothing to read — forks an `/etc/getty` on each, and `/bin/login`
  checks a password through `crypt(3)` against a real encrypted field in `/etc/passwd`, writes
  `/etc/utmp`, `chown`s the terminal *before* it drops root, and execs a shell. Log out and
  `multiple()` respawns the getty for the next person. The prompt comes back **`$ ` rather than
  `# `**, which is the point: until now `init` exec'd `/bin/sh` directly and every process above
  the icode was root's, so `getxfile()`'s `ISUID` branch could only be reached by a program that
  dropped privilege on purpose (`lib/test/suidt`). Two divergences worth knowing — `getty`'s
  thirteen-entry PDP-11 speed table is **one entry** here, since a Consul has no baud rate, no
  parity, no delays and no lower case to fold (`cmd/getty/README.md`), and `login` needs an
  `fflush()` v7 did not, this libc's stdout being line-buffered on a terminal where v7's was
  unbuffered, so a prompt with no newline never arrived (`cmd/login/README.md`). And it **swaps**:
  squeeze the machine to 31 pages and `sched()`/`newproc()` move real images through the drum,
  while `/bin/sh` and `/usr/test/puret` — the two binaries linked pure — share one copy of their
  text between processes. **`/dev/mem` and `/dev/kmem` work** too: a program reads its own
  `struct user` out of the kernel at `074000`, follows `u_procp` to its proc entry, and then
  reads *and writes* its own image at a physical address above `0100000` — which no unmapped
  access can name, so the driver goes through `copyphys()`, `kernel/seg.S`'s mapped window.
  And, task 29c, **two people are logged in at once**: root on `/dev/console` and guest on
  `/dev/tty1`, each shell naming its own terminal through `ttyname(3)`'s match by i-number,
  guest's file read by root's shell, and the disk afterwards carrying two *different* non-zero
  owners on two terminal nodes at the same instant — which no single session can produce, `login`
  chowning its terminal before it drops root and `init`'s `dfork()` chowning it back on every
  respawn. Driving the second typewriter needed two fixes in the simulator, both real bugs:
  `vt_getc()` accepted only the tag SIMH puts on a character that came off the *socket*, so
  `send TTY:n,"…"` was a silent no-op on every BESM-6 mux line, and `attach tty26 4199` binds the
  *mux* rather than line 26, handing the connection to line 1. Even fixed, a listener is wrong for
  a test — the accept poll runs once a second of host time, which is after the getty has prompted
  into the void — so `kernel/test/ttyhost.c`, the one host-compiled C program under `kernel/`,
  binds a port and the simulator **dials out** to it while the `.ini` is still being parsed.
  And, task C3, **it can author text of its own**: `/bin/ed` is on the image, so a shell script,
  an `/etc/ttys` or a C source need no longer be written on the build host and staged in —
  which is the precondition for the self-hosting task meaning anything. Two things about that
  port are worth knowing before touching anything near it. It is **eight-bit transparent**,
  and it had to be: v7's `getfile()` calls `error()` on any byte with `0200` set, so that
  editor could not so much as *open* `/etc/motd`, 365 bytes of UTF-8 that `cat` has always
  printed — and `getchr()`'s `& 0177` folded a Cyrillic capital's second byte onto `020`–`057`,
  so `Ъ` arrived as `P*` and a Cyrillic letter typed into a *regular expression* became a
  metacharacter. The mark that `compsub()` kept in bit `0200` of a replacement byte is a
  `QESC` prefix now, exactly as the shell's is. And **`-x` is gone**, the one deliberate
  divergence: it execs a `/usr/lib/makekey` that is not on this image and is in no plan to be.
  `cmd/ed/README.md` is the account, and it also records that this file's own porting recipe
  was wrong about `ed` twice — ten `char *` comparisons where there are twenty, and a `CCL`
  bitmap that belongs to `grep` and `sed` and that `ed` does not have at all.
  And, task C4a, **it can measure the store it lives on**: `df`, `du` and `quot` are on the
  image, the first programs here that report on the filesystem rather than change it — `df`
  counting the free list by draining it exactly as the kernel's `alloc()` does, `du` walking a
  tree and charging for a hard link once, `quot` sweeping the whole i-list by owner. All three
  **report in 1024-byte blocks**, and so does `ls -s` since the same task: they count the
  filesystem's 3072-byte block and print `KBPB` — three — of them per block at the `printf`
  and nowhere else, so a number means something without knowing `BSIZE`, every number is a
  multiple of three, and a number is half a PDP-11's rather than a sixth. `quot -c` is what
  fixes *where* the multiply goes: its histogram is indexed by a block count, so converting at
  the source would cut what the table covers to a third. Two of the
  three go through the **raw disk**, and that is what the task was really about:
  `open`/`lseek`/`read` on `/dev/rmd0` is not the v7 call sequence it looks like, because
  `physio()` and `mdstrategy()` impose four conditions between them — the buffer at byte #0 of a
  word, the count a whole number of `BSIZE`s, the buffer's *word address* a multiple of
  `MDTRACK` (512 words, the disk's half-zone), and the seek block-aligned. Three of the four are
  an `EFAULT`; the fourth is **silent**, `physio()` truncating the offset and reading the wrong
  block. Nothing in C asks for that alignment, so the idiom is an over-sized `bss` array stepped
  forward with `(int)ptr` — a word pointer *is* a word address here — and obeying the rules made
  the programs *smaller*, `quot`'s 4,096-word `itab[256]` disappearing because one block already
  is `INOPB` inodes. `cmd/df/README.md` is the account, and the rest of C4 — `dd`, `mkfs`,
  `fsck`, `icheck` — takes the same path. **Since task C4c that path runs both ways**: `mkfs`
  writes a filesystem onto a second drive, which is the first time `kernel/dev/md.c`'s
  `mdwrite()` has ever run and the first time this machine has had two disks. A write obeys all
  four conditions above and a fifth of its own, and the second drive turned up a bug one drive
  could not hold — both in `cmd/mkfs/README.md`. **And since task C4d it can put a filesystem
  right**: `/etc/fsck` is on the image, so the machine takes a volume that is wrong, works out
  what is wrong with it and repairs it over that same write path — an orphaned file reconnected
  into a `/lost+found` the root now carries, and in phase 6 the whole free list of a 2000-block
  volume laid down again, which is the longest run of raw writes anything here performs. Then it
  reads **the filesystem it is running on**, read-only, and pronounces it sound, three
  independent measurements of that volume's free space agreeing — `fsck`'s, `df`'s and the
  host's. Its oracle is the tool that had been fscking every image in this tree from outside:
  `cmd/fsutil/check.cpp` implements the same checks in host C++, sharing no line with the guest,
  so a filesystem damaged by a new `b6fsutil -D` verb can be handed to both and every
  disagreement is a bug in one of them. Four turned up, three of them in `fsck` — a free-inode
  count one too high on every clean filesystem this system ever made (inode 1 exists and can
  never be allocated), a superblock magic number v7's `fsck` never looked at, and a reconnected
  file that could have been left unreachable by `namei()`; the fourth is a field the two
  deliberately differ about, and the harness marks it rather than hiding it
  (`cmd/fsck/README.md`). It also settled how a program that reports on a whole
  filesystem is *asserted*: not against a checked-in table, which would have to be re-typed
  whenever anything joined `/bin`, but against numbers the host **recomputes** from the finished
  image — and to the **console** rather than to a file, since a `quot` writing to `/tmp` counts
  the file it is about to fill. Three upstream bugs went with it: `du` scanning its hard-link
  table one slot past what it had filled, `quot` writing one element past `du[NUID]`, and
  `quot`'s `qsort` comparator calling `strcmp()` on two `NULL`s — which a PDP-11 forgave,
  address 0 being readable there, and which here dereferences word 0.
  Fourteen tests
  guard that ladder — `kernel/test/boot` (the prompt appears), `kernel/test/console` (a typed
  dialogue with the shell, and the only one that reaches `/etc/rc`, whose date it
  asserts, ending on the `login:` the first getty puts there — **currently `DISABLED`**, since it
  fails about one run in three for reasons that are the simulator's rather than the kernel's;
  `kernel/TODO.md` task 35 owns it), `kernel/test/login` (that prompt
  typed at: an unknown name refused, root logged in, the shell exited and the getty respawned,
  guest logged in to a non-root shell, then a host-side fsck and the four inodes only the host
  can see), `kernel/test/multi` (both typewriters driven at once, the second through a socket
  `ttyhost` holds open, with a transcript of what Consul 2 printed diffed on the host beside the
  modes that prove the two sessions overlapped), `kernel/test/session` (files written, `sync`, then a host-side
  fsck and diff), `kernel/test/files` (the file-management set rearranging a tree and
  then re-permissioning it, fscked on the host afterwards and its modes and owners diffed out
  of `b6fsutil -v -v`), `kernel/test/libtest` (the libc suite run off the image, one ctest case
  per program), `kernel/test/swap` (more processes than core, asserted through the kernel's
  own counters), `kernel/test/utils` (the clock moved and read back, an alarm delivered, a
  background process killed, a script branching on `test` through both its names, a terminal
  named, a `yes` stopped by a broken pipe, a command timed, and — task C11 — a Cyrillic word
  crossing `exece()` into another program and a Cyrillic pattern globbed against a real
  directory) and `kernel/test/edit` (task C3: a file authored from nothing, two more edited
  that `cat` had written, `/etc/motd` read and written straight back and `cmp`'d **on the host
  against the original out of the same extracted image** — which is the whole assertion and
  needs no checked-in fixture — three copies of it substituted into Cyrillic through `ed`'s own
  512-byte temp-file pager, a shell forked out of `ed`'s `!`, and a closed listing of `/tmp`
  proving nothing leaked; it is also the only test that runs a **here-document** under the
  booted kernel, which nothing had done before — and it too is **currently `DISABLED`**, failing
  two runs in six on the *typed* stage that follows all of that, which is `console`'s wobble
  again and so also task 35's; run it by hand, and note that disabling a script rather than an
  `.ini` suspends its host-side fsck and `cmp`s with it) and `kernel/test/fsinfo` (task C4a: a tree of
  known sizes with a hard link across two directories that `du` charges for **once**, and then
  the superblock and the whole i-list read off the raw device — with `df`'s free count and
  `quot`'s per-uid sums diffed against figures the host **recomputes**, out of `b6fsutil -c -v`
  and an `awk` over `b6fsutil -v -v`, so that neither has to be re-typed when a program joins
  `/bin`; it is the only test that captures the **console transcript** as part of its oracle,
  which it must, since a program that measures a filesystem cannot write its answer into one)
  and `kernel/test/dd` (task C4b: two blocks pulled off `/dev/rmd0` twice over, once at the
  default record size and once through the fast `bs=` path, then the same blocks asked for at
  two sizes the hardware cannot name — `bs=512`, which `physio()` refuses as not a whole number
  of words, and `bs=6`, which it passes and `mdstrategy()` refuses as not a whole half-zone —
  which makes it the first test here to assert a **refused** transfer at all; its oracle is the
  disk itself, the guest's copy `cmp`'d on the host against the same offset in the container the
  boot was handed, so there is nothing checked in and nothing recomputed, and beside it 365
  bytes of UTF-8 go through the EBCDIC tables and back, which needs `dd` run twice and so cannot
  be one of the `cmd_dd_*` cases)
  and `kernel/test/mkfs` (task C4c: the only test here that attaches **two** disks, and the
  only one whose subject is a filesystem that did not exist when the machine booted — a scratch
  pack formatted by `attach -n md01`, one block of /etc/motd pushed onto it through `mdwrite()`
  and read straight back, two misaligned **writes** refused where `dd` had only ever asserted a
  refused read, and then a 2000-block filesystem made on it and measured with `df`; its oracle is
  a **byte-for-byte `cmp` against `b6fsutil -n`**, available because the guest program and the
  host tool are transcriptions of each other and the only thing between them is a timestamp six
  bytes into the superblock, which the host reads back out of the guest's own output)
  and `kernel/test/fsck` (task C4d: the only test here whose scratch pack arrives with something
  deliberately **wrong** on it — built and then broken on the host with `b6fsutil -D`, so it is
  attached *without* `-n`, which would format away the thing under test — and the only one that
  examines the filesystem the machine is **running on**: `fsck -y /dev/rmd1` repairs an orphan
  and salvages a whole free list over the write path, a second pass finds nothing, and then
  `fsck -n /dev/rmd0` reads the mounted root and its free count is held against `df`'s and
  against the host's. That last check is sound only while nothing allocates a block after
  `fsck`'s `sync(2)`, which is a property of the *order* of `kernel/test/fsck.sh` and not of any
  program, so that file's header says so in the imperative)
  — and `cd kernel && make run` is
  where you type at it yourself. The drums
  must be attached to exec anything: they are `swapdev`, and `exece()` stages the argument
  list in swap. See `kernel/README.md`, the reference — the settled design, the hardware rules
  it obeys, what a standalone SIMH test costs to get right, and the consequences accepted —
  and `kernel/TODO.md`, the work plan: a scoped task each for the road past the shell
  (numbered from 30, since the sources cite the earlier numbers). Kernel components are also
  exercised piecemeal by the standalone SIMH tests in `kernel/test/`.
- **`cmd/`** — the BESM-6-specific toolchain being written/ported to eventually build the
  kernel for real BESM-6 hardware: a C compiler driver, an assembler, a linker
  (+ archiver/nm/size/etc.), a C preprocessor, a disassembler, and a user-level a.out
  simulator (`b6sim`, `cmd/sim/`) that runs BESM-6 executables and services Unix v7
  syscalls — the only tool here that actually *executes* BESM-6 code.

External pieces this project depends on (not in this repo):
- BESM-6 C cross-compiler: https://github.com/besm6/c-compiler/
- BESM-6 hardware simulator (SIMH): https://github.com/besm6/simh/tree/master/BESM6/ — the
  authentic full-machine emulator, distinct from the in-repo user-level `b6sim`. **This is the
  machine the kernel boots on**, so it is the target the port aims at, not just a convenience.
  It is documented here in `doc/Simh_Simulator.md` (how to build, run, and drive it) and
  `doc/Besm6_Peripherals.md` (how a program talks to its hardware).

## Building

**One CMake project**, driven through a thin top-level `Makefile`; there are no
per-component Makefiles under `cmd/` or `lib/` anymore, and the ones in `kernel/` and
`kernel/test/` are thin wrappers over the same `build/` tree. It produces **three kinds of
thing**, and knowing which one you are touching is most of what the build layout means:

- **host tools** — `cmd/*`, compiled by the build machine's C/C++ compiler, run there;
- **cross-built BESM-6 artifacts** — `kernel/` and `lib/`, compiled by the `b6*` toolchain
  above through `b6_obj()` in `scripts/BesmCross.cmake`;
- **native BESM-6 programs** — `cmd/{init,getty,login,fsck,mkfs,quot,sh,basename,cat,chgrp,chmod,chown,cp,date,dd,df,du,echo,ed,kill,ln,ls,mkdir,mv,pwd,rm,rmdir,sleep,sync,test,time,touch,tty,yes}`, linked against libc by `b6_prog()`
  and staged into `build/rootfs/` (with the static files of `etc/`) for the disk image the
  kernel mounts.

The last two are guarded on the **external** c-compiler's `libruntime.a` being installed;
without it the tree still configures and builds the `cmd/` tools alone.

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

`make install` also installs **`include/`** to `<prefix>/share/besm6/include`, the one system
header tree, which `b6cc` appends to every preprocessor run. That directory has **two owners**:
this repo ships the **hosted** half (the v7 headers, `sys/` included, plus the C11 ones v7 never
had), and the external c-compiler installs the **freestanding** ten alongside them —
`stddef.h`, `stdarg.h`, `limits.h`, `float.h`, `stdbool.h`, `stdint.h`, `iso646.h`,
`stdalign.h`, `stdnoreturn.h` and `besm6.h`. Those describe the compiler's own data model,
`<stdarg.h>` ABI and intrinsics, so they track the compiler; there is no copy of them here.

The hosted half is **C11**, and [include/README.md](include/README.md) is the account of what
that cost: `assert()` is an expression and `<assert.h>` is deliberately unguarded,
`toupper`/`tolower` are functions (v7's unconditional macros are `_toupper`/`_tolower`),
`isprint(' ')` is true, a signal handler is `void (*)(int)`, and v7's `_IONBF` flag bit is
`_IOUNBUF` so C11 can have the name for a `setvbuf` mode. `complex.h`/`stdatomic.h`/`threads.h`
are absent by design and `b6cpp` says so with `__STDC_NO_COMPLEX__`/`_ATOMICS_`/`_THREADS_`
(plus `__STDC_NO_VLA__`). `lib/test/headers.c` includes the whole tree twice and is what keeps
it that way.

### Library (`lib/`) — part of the top-level build

`lib/` (`libc.a`, `libm.a`, `libtermcap.a`, `libcurses.a`, `crt0.o` and the `b6sim` test harness) is a guarded
`add_subdirectory(lib)` of the top-level CMake project, cross-compiled by the b6* toolchain
rather than the host compiler — like `kernel/`, and sharing its `scripts/BesmCross.cmake`
toolchain module. Integrated it drives the **in-tree** tool targets (`$<TARGET_FILE:b6cc>` …),
so it no longer needs the toolchain installed first, and the old three-step bootstrap collapses
to the standard two:

```sh
make && make install        # builds cmd/ tools, the kernel, lib/ and build/rootfs/; installs include/ + the archives
```

`lib/` has **no per-directory Makefiles** (like `cmd/`): `make` builds it, `make install` puts
`libc.a`/`libm.a`/`libtermcap.a`/`libcurses.a`/`crt0.o` into `share/besm6/lib` beside `libruntime.a`, and `make run` runs its
tests via ctest (label `lib`). It can also be built in isolation with `cmake -S lib -B <dir>`,
which falls back to the *installed* tools. `crt0.o` is what makes `b6cc` able to *link* — until
`make install` has run, `find_crt0()` says so.

**All four archives now have their own README, and that is where the reasoning lives** — read the
one for the library you are touching before touching it. `lib/libc/README.md` is the long one, in
proportion to the library (184 objects, 12,137 words): the `$77` gate contract and why `lib/libc/sys/`
is assembly and not C, every place a fat pointer or a one-word `long` forced a change (`malloc`'s
`BUSY` bit at bit 16, `sbrk`'s `NULL`, `crypt`'s `L[32]`, `execle`'s raw-word terminator, `strtol`'s
value-preserving casts), stdio's line buffering and `_IOSTRG`, the `_cleanup`-through-a-pointer
measurement that is the difference between a 100-word `hello` and a 2,255-word one, and eleven
upstream bugs fixed rather than carried. `lib/libm/README.md` is the short one, because that port
has a single theme: **overflow is a fault and not an infinity**, so `HUGE_VAL` is a value a routine
*returns* and never one it computes, and every range gate sits *before* the arithmetic — plus the
PDP-11 magic numbers that were mantissa widths (`sinh`'s 21 → 14, `sin`'s 32764 → 2^40), the
j0/j1 coefficients that could not be written as literals at all, and an `fma` whose split cannot
be Dekker's because this machine rounds by forcing the low bit.

**The v7 manual pages are in the tree too**, beside the libraries they document and **corrected in
place** on `termcap.3`'s precedent — every SYNOPSIS ANSI, every wrong claim fixed where it stands
and marked `Note:`, and the C11 routines v7 lacked folded into the page that owns them.
`lib/libc/man/` holds 86 (sections 2 and 3) and `lib/libm/` its six `.3m`. Two structural edits
worth knowing: every `.SH ASSEMBLER` section is replaced, `intro.2` now carrying the whole `$77`
contract and each page pointing at it; and the four pages that pulled a header in with
`.so /usr/include/…` have their structures written out, there being no `/usr/include` here.
**Nothing installs any of them** — no `CMakeLists.txt` in `lib/` has a man rule — so they are read
with `nroff -man`.

**`lib/libtermcap/`** is the third archive, and the newest: the 4.xBSD termcap library —
`tgetent`/`tgetnum`/`tgetflag`/`tgetstr`, `tgoto` and `tputs` — reading the `/etc/termcap` that
`etc/` stages onto the disk image (BSD's `termcap.small`, verbatim). It is declared by
`include/term.h`, a header this repo added and v7 had none of, and it links **`-ltermcap` before
`-lc`** for the same one-scan reason `-lm` does. `lib/libtermcap/README.md` is the account, and
what it is mostly about is **four `char *` comparisons that had to go**: termcap is nothing but
buffer cursors, and `<` between two `char *` does not order them here (see `cmd/ls/README.md`),
so every one of `p < tbuf`, `cp > bp`, `cp >= bp+BUFSIZ` is an `int` count now. That same hazard
was live in `lib/libc/stdio/getpass.c`, where it made `getpass()` return the empty string every
time; fixed in the same pass. `tputs` deliberately emits **no padding** — nothing on this machine
can be overrun — which is also why the library defines neither `PC` nor `ospeed`.

**`lib/libcurses/`** is the fourth archive and termcap's first consumer: **4.3BSD curses**, 39
sources and 5,311 words, linking **`-lcurses` before `-ltermcap` before `-lc`** for the same
one-scan reason. Two things about it are worth knowing before touching anything nearby.
**`include/curses.h` was replaced**, not kept — what stood there was v7's `1.7 (4/17/81)`, and it
is ABI-incompatible with these sources in five ways (`struct _win_st` grew three members, every
flag bit shifted, `bool` was a `char`, `_puts` carried a stray semicolon, the tty macros went
from `stty`/`gtty` to `ioctl`), each of which corrupts memory rather than failing to compile.
And **eleven `char *` comparisons** had to go rather than libtermcap's four, because curses is
almost entirely buffer cursors; the two in `refresh.c` decide whether clearing to end of line is
cheaper than printing the blanks. `lib/libcurses/README.md` is the account, and it also records
six upstream bugs fixed — three of them memory corruption, including a `_id_subwins()` that
wrote one word in front of a heap block. It is also **the tree's first and only `_Bool` user**:
`<curses.h>` includes `<stdbool.h>` and the flags and boolean capabilities are `bool`, which is
the type and not a macro — BSD's own `#define bool int` would collide with `<stdbool.h>`'s in
either inclusion order. That took two fixes in the external compiler (c-compiler `2fcd322`,
prompted from here): `b6lower` could not lower `_Bool` at all, and conversion to it did not
normalise to 0/1. `_Bool` now has **int's representation** — one word, so `bool *` is an
ordinary word pointer, which `cr_tty.c`'s table of pointers-to-flags requires.

The **only** thing this repo does not build is **`libruntime.a`**, the `b$*` compiler-support
helpers (`b$save`, `b$ret`, `b$mul`, …) that every compiled function calls. It comes from the
external c-compiler, which installs it and the ten freestanding headers above, and nothing
else for this toolchain — the libc, the `crt0.o` and the v7 headers are ours. Every link
therefore names two archives, **ours first**: `-lc -lruntime`, because `b6ld` scans an archive once, in order, libc calls the helpers, and no
helper calls back into libc. The kernel takes `-lruntime` **alone** — it defines its own
`printf` in `kernel/prf.c` and uses no other library routine.

### Native BESM-6 programs (`cmd/{init,getty,login,fsck,mkfs,quot,sh,basename,cat,chgrp,chmod,chown,cp,date,dd,df,du,echo,ed,kill,ln,ls,mkdir,mv,pwd,rm,rmdir,sleep,sync,test,time,touch,tty,yes}` + `etc/` → `build/rootfs/`)

The third category, and the newest. These are **`cmd/` subdirectories that are not host
tools**: `cmd/init/init.c` is the Unix v7 `/etc/init`, `cmd/sh/` is S. R. Bourne's v7 shell,
and `cmd/cat`, `cmd/echo`, `cmd/ls`, `cmd/pwd`, `cmd/sync` are the five commands that prove the
prompt, with `cmd/mkdir` and `cmd/rmdir` (task C1a) the first two that can *change* the tree,
`cmd/cp`, `cmd/ln`, `cmd/mv`, `cmd/rm` (task C1b) the four that can rearrange it, and
`cmd/chmod`, `cmd/chown`, `cmd/chgrp`, `cmd/touch` (task C1c) the four that change an *inode*
rather than a directory — none of those last four setuid, since `chmod(2)` is gated on
`owner()` and `chown(2)` on `suser()`, and `cmd/touch` the one program in the set that
deliberately does not do what v7's did (`utime(2)`, not a rewritten first byte) — and
`cmd/date`, `cmd/sleep`, `cmd/kill` (task C2a) the three that are about the *machine* rather
than the filesystem: the clock, an alarm, and a signal to another process.  Not setuid either,
and `date` emphatically not — `stime(2)` is `suser()`-gated and that gate is the whole reason a
user cannot move the clock. Last, `cmd/test`, `cmd/basename`, `cmd/tty`, `cmd/time` and
`cmd/yes` (task C2b) are the five the *shell* wanted, `test` above all: this shell has no
built-in for it, so nothing on this machine could branch until it arrived. Then `cmd/ed`
(task C3) is the one that changes what the machine *is* rather than what it can say about
itself — the first program here that can author text, and so the precondition for task C9. It
is eight-bit transparent where v7's refused any file with a byte above `0177`, its `-x`
encrypting mode is dropped as the one deliberate divergence in the port, and it is the one
large program on this image that links **no stdio at all** (`putd()` over `write(2)`), which is
why 1,764 lines come out within twenty-four words of `cat`. Then `cmd/df`, `cmd/du` and `cmd/quot`
(task C4a) are the first three that report on the filesystem rather than change it, and the
first two here to read a **device**: `df` and `quot` go through `/dev/rmd0` and so through
`physio()`, whose four alignment conditions are what that task was really about
(`cmd/df/README.md`). `quot` is the only one of the three in `/etc` rather than `/bin`, being
section 1M and root-only, and none of the three is setuid — `/dev/rmd0` is mode 0600 because
that one node is every file's contents. Then `cmd/dd` (task C4b) is the one that *moves* the
store rather than measuring it, and the only program here that can put bulk data onto
`/dev/rmd0` or take it off — which is why `cmd/TODO.md` puts it ahead of `mkfs` and `fsck`. Its
one divergence is a **default**: `ibs`/`obs` default to `BSIZE` and the `b` suffix multiplies by
`BSIZE`, where v7's were 512, because 512 is not a whole number of words and `physio()` refuses
it before the driver sees it — so with v7's defaults the commonest thing `dd` exists to do could
not reach the disk at all. `w` came along free, `sizeof(int)` being 6 rather than a PDP-11's 2,
so `1b`, `3k` and `512w` now name the same 3072 bytes. Three upstream bugs went with it, the
sharpest being a `conv=swab` that rounded its pair count down to even and so copied a two-byte
record entirely **unswapped**, silently. Then `cmd/mkfs` (task C4c) is the one that *makes* a
filesystem rather than measuring or moving one, and it is what finally **wrote** to a raw
device: `kernel/dev/md.c`'s `mdwrite()` was dead code until it, and this is the first thing on
this system that has had two drives — `/dev/rmd1` and `/dev/md1` are on the image beside
`/dev/rmd0` and `/dev/md0`, and `kernel/test/mkfs` hangs a scratch pack on SIMH's `md01`. It is
in `/etc` and root-only for `quot`'s reasons, and emphatically not setuid for a sharper one. Its
one divergence is that the **prototype-file form is not ported** — the boot-block token names
nothing here, `cfile()` recurses with about 1,024 words of frame per directory level against a
4,096-word unchecked stack, its two buffers are exactly the ones a raw transfer cannot use, and
`b6fsutil -n -M` on the host does that job better — so `mkfs special nblocks [ ninodes ]` is the
whole command. Three things it settled are `cmd/mkfs/README.md`'s: a raw **write** obeys all
four of the read's conditions plus a fifth (`physio()` refuses a `base` below `u_tsize`, so it
may never be sourced from the text — dormant while `b6_prog` links `FMAGIC`, live the day
anything that writes a device is linked *pure*); the disk's sector-header buffer belongs to the
**controller** rather than the drive, so a two-drive machine stamps one pack's volume number
onto another's and nothing may write block 0 of a pack this system did not label; and
`MDNBLK` need not be duplicated into a guest program, because reading the *last* block before
writing the first lets `mdstrategy()` answer — which is safe only because the superblock is
written last. Its oracle is byte-exact in both worlds: `b6fsutil -n` builds the same image and
the only thing between them is a timestamp six bytes into the superblock. Then `cmd/fsck`
(task C4d) is the one that *repairs* a filesystem, and the first program here that writes one
it did not make — the top of the C4 ladder and, at **10,842 words**, the largest thing on the
image, measured before it was ported because `cmd/TODO.md` held splitting it in reserve and it
turned out not to need it. It came out a third shorter than v7's 1,684 lines, almost all of the
deletion being one thing: v7 carries a whole second implementation of itself — a scratch file,
a buffer pool, an LRU search and a second arm in three map accessors — for the case where its
tables will not fit in 54 Kb, and here they are 1,300 words of `calloc`. Two of those deletions
were forced rather than merely available, the arena being unalignable and the multi-block i-list
sweep being a transfer `physio()` refuses. Its own findings are `cmd/fsck/README.md`'s: the
indirect-block buffer cannot be one static per level, because `pass2` descends into a
subdirectory from inside the *parent's* `iblock()` and two walks of a level are live at once —
so it is one shared buffer re-fetched each iteration, which is the idiom v7's own `dirscan()`
already uses on `fileblk` for exactly that reason; and `hotroot` never fired, `rootdev` being
`makedev(0,0)` while `/dev/rmd0`'s `st_rdev` is `makedev(3,0)`, so the raw name is mapped back
to the block one as 4.xBSD's `unrawname()` does rather than by duplicating `cdevsw[]`'s pairing
into a user program.
Last again,
`cmd/getty` and `cmd/login` (kernel task 29b) are the two that make the machine multi-user, and
they are the only pair here where one execs the other: `init` execs `/etc/getty`, getty execs
`/bin/login`, login execs the shell, all in one process, which is why a logout is that process
exiting and `init`'s `multiple()` starting a fresh getty. Neither is setuid, and `login`
emphatically not — it is *handed* root by getty rather than borrowing it, and the setuid version
is `su` (task C6). All compiled by the
`b6*` toolchain and staged into `build/rootfs/` as `etc/init`, `etc/getty`, `etc/fsck`, `etc/mkfs`, `etc/quot` and
`bin/{sh,basename,cat,chgrp,chmod,chown,cp,date,dd,df,du,echo,ed,kill,ln,login,ls,mkdir,mv,pwd,rm,rmdir,sleep,sync,test,time,touch,tty,yes}`. **`mkdir`, `mv` and `rmdir` are setuid
root** on the image, which is a property of [root.manifest](root.manifest) alone (`mode 04755`)
since nothing under `build/rootfs/` carries a mode; see
[cmd/mkdir/README.md](cmd/mkdir/README.md) for the general account and
[cmd/mv/README.md](cmd/mv/README.md) for the one program that is setuid for only *part* of what
it does. **`/bin/[` is the one file on the image that is a second *name* rather than a second
file** — a `link` stanza in the manifest, `/bin/test`'s inode, and the only hard link there is;
[cmd/test/README.md](cmd/test/README.md) is the account, and `kernel/test`'s `rootimg_link`
asserts it off the finished image because nothing else in the build can see it.
Alongside them `etc/` (the top-level directory, not `cmd/etc`)
stages the static files `group`, `motd`, `passwd`, `rc`, `termcap` and `ttys`, which are copied
rather than compiled — `ttys` being the one that changes what the machine *does* rather than what
it says, since `init` reads it and forks a getty per line.
`lib/test/` stages a third group, `usr/test/*` — the twenty-eight test programs,
the same linked images `b6sim` runs, copied rather than linked a second time so that both
harnesses provably run the same bytes.
Together that tree is the root filesystem the kernel mounts. All of it is added from inside
the `libruntime.a` guard, *after* `lib/`, not with the other `cmd/` subdirectories, because
each program links against the libc built there. (The `rootfs` aggregate every stager hangs
itself on is therefore declared *before* `add_subdirectory(lib)`.)

The machinery is one function, `b6_prog()` in `scripts/BesmCross.cmake`, so a further native
program is one call:

```cmake
b6_prog(init DEST etc/init SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/init.c)
```

It compiles each source with `b6_obj()` into a per-program object dir, links
`crt0.o … -lc -lruntime` (that order is the archive-scan contract), writes only the finished
program into `${B6_ROOTFS}` — the `.nm`/`.dis` listings stay in the build dir, since that tree
becomes a disk image — and registers one ctest, `rootfs_<name>_size` (label `rootfs`), running
`scripts/check-size.sh`. That check is the guard rail for the two **user** address-space
ceilings, which are not the kernel's: `const+text+data+bss` must fit **28,672 words** (32 pages
less the four the stack takes at `070000`) and no relocatable symbol may sit above word
**32,767**, the reach of a 15-bit pointer. Both failures are otherwise silent — the link
succeeds and only the running program misbehaves.

The C dialect is the thing that bites when porting v7 userland: **`b6parse` is strict C11**.
No implicit `int`, no K&R parameter lists, no untyped `register i;`. Every v7 source needs
that mechanical modernization before it compiles; `cmd/init/README.md` is the small worked
example and `cmd/sh/README.md` the large one, and `cmd/cpp/TODO.md` is the plan for the next
program (with three external-compiler bugs of its own still in the way).

**`cmd/README.md` is the manual for porting anything else from v7 userland** — what is already
in that directory, the eleven-point porting recipe every task is written on top of (the `char *`
ordering hazard, the one-word `long`, the 3072-byte block and the 1024-byte one reported in its
place, `DIRSIZ` 18, the three address-space
ceilings, how a program gets onto the image, which of the two harnesses tests it, and what the
manual page owes), and what task C1 taught. `cmd/TODO.md` beside it is only the work plan and
deliberately repeats none of it.

And, task C11, the shell is **eight-bit transparent** (the task number, not the language
standard the next paragraph is about): v7 marked a quoted character by setting
bit `0200` of it and `trim()` cleared that bit from every word on its way to `argv`, so `cat`
carried Cyrillic and `echo привет` did not. The mark is a prefix byte now — `QESC`, `0377` —
with an in-flight bit far above the character, the two `_ctype` tables grew to 256 entries
because the old `(c & QUOTE) == 0` guard *was* their bounds check, and `expand.c` lost the
second thing `0200` meant there (a sentinel for a `/`), since a variable-width encoding cannot
be scanned backwards. `cmd/sh/README.md` is the account, `cmd/sh/test/utf8.sh` the fast half of
the proof and `kernel/test/utils.sh` the half that needs a kernel.

**Read `cmd/sh/README.md` and `cmd/ls/README.md` first**,
though: the C11 work is mechanical; what is not is that a v7 source assumes
an `int` and a `char *` are the same thing, and on this machine they are not. `sh`'s names
three hazards that follow from that — a flag packed into bit 0 of a pointer, a bit mask used
to round to a word when `BYTESPERWORD` is 6, and casts to a node pointer that *floor* rather
than round — and `ls`'s adds a fourth: **`<` between two `char *` does not order them**, since
the byte offset sits above the word address and *decrements* as the pointer advances, and
there is no relational helper. (`-` is fine; `b$pdiff` decodes both operands.) Each names the
fix.

`build/rootfs/` is staged only — nothing installs it. **`root.manifest` at the top of the
tree** reads it with `source ../../rootfs/…`, resolved against `b6fsutil`'s *working
directory* (`build/kernel/test`) rather than against the manifest's own location — which is
why moving the file changed no path in it. The `rootimg`-depends-on-`rootfs` edge is drawn at
the foot of the top-level `CMakeLists.txt`, because `kernel/` is configured before the
`rootfs` target exists. Every `file` entry now comes from `build/rootfs/`, `/etc/init`
included; `kernel/test/coninit.S`, the task-23 stand-in that used to hold that slot, is still
built but is on no image and drives no test — it is the libc-free reference program a console
failure gets bisected against, and `kernel/test/CMakeLists.txt` gives the `b6fsutil -a`
commands that put it back on a scratch copy.

### Kernel (`kernel/`)
```sh
cd kernel && make          # produces `unix` (BESM-6 a.out), unix.nm and unix.dis
make run                   # boot it under SIMH (`besm6 unix.ini`)
make clean
```
The kernel **is** part of the CMake build — `kernel/Makefile` is a thin wrapper that drives
the top-level `build/` tree (`--target kernel`), and `add_subdirectory(kernel)` sits inside
the `libruntime.a` guard. It cross-compiles with the **in-tree** tool targets, so a rebuilt
`b6as` relinks it with no `make install` in between — `b6cc -I../include -DKERNEL`, `b6as`,
`b6ld`, `b6ar`/`b6ranlib`, linking
against `libruntime.a` (`~/.local/share/besm6/lib`) for the `b$*` helpers, and nothing else:
no `-lc`, since it has its own `printf` and calls no library routine.
`make` finishes by printing `b6size -w unix`: the image **must end below `062000`** (`KEND` in
`include/sys/param.h`), because supervisor instruction fetch is never mapped and the top two
areas of the unmapped space are spoken for — the two u-area pages at `074000` and, just under them,
`buffers[NBUF][BSIZE]` from `062000` to `074000` (`doc/Memory_Mapping.md`). Both are fixed
physical areas rather than bss, so they are *not* counted in the `b6size` total; the ceiling is
derived (`KEND == BUFBASE == UBASE - NBUF*BSIZEW`), so raising `NBUF` lowers it automatically.

The kernel is archived into one link-pulled static lib, `libunix.a`, so unused code is dropped.
Its three source groups are the Makefile variables `SYS` (core kernel, `kernel/*.c`), `DEV`
(device drivers, `kernel/dev/*.c`, found through `VPATH`) and `MACH` (the machine-language
assist). It was once two archives, `libsys.a` and `libdev.a`, with `libsys.a` named *twice* on
the link line because the drivers call back into the core kernel (`timeout()`, `wakeup()`, …)
and each archive is scanned once, in order. One archive plus `b6ranlib`'s symbol index resolves
those back-references in a single scan, so that workaround is gone.
`besm6.S` is the BESM-6 machine assist — the interrupt/extracode vector block at `0500`/`0501`
and `0550`–`0577`, plus the routines C cannot express, and
**`besm6.o` must come first in `OBJ`** so its const contribution pins those vectors at their
fixed addresses. `brz.s` is `drainbrz()`, alone in its own file for two reasons: it cannot be
written in C (see below), and `kernel/test/` links it directly. `syscall.c` is split out of
`trap.c` for that second reason alone — it holds the extracode door's C side (`syscall()` and
`badextr()`), and `kernel/test/usys` links the real thing rather than a copy. `conf.c` is the
device config table, and is C — it belongs to `SYS`, not `MACH`.

Header dependencies are **coarse, not computed**: `b6cc` and `b6cpp` implement no `-M`/`-MD`
family (both reject those flags outright), so `kernel/CMakeLists.txt` globs
`include/sys/*.h` and every object depends on every system header. Adding a source, or an
`#include` to an existing one, needs no bookkeeping; adding a **new header file** needs a
re-configure, since `file(GLOB)` runs at configure time. (The hand-maintained `###`
dependency list this used to describe went with the hand-written Makefile, in `da35740`.)

### Tests

**Kernel tests run on SIMH** (`cd kernel/test && make test`). They are not host unit tests:
each is a standalone BESM-6 program that links kernel objects against a hand-built
environment, plus a `.ini` script that loads it into the real simulator, runs it, and asserts
on the machine state afterwards. They exercise one kernel component at a time, in isolation from
a booting kernel — `b6sim` cannot substitute, since it runs a user `a.out` with no kernel
underneath. `crt0.s` (not libc's) seeds the stack and calls `main()`; the
program's status is left in the accumulator, where the `.ini` asserts on it. `mmutest` is the
one to copy: it links the kernel's real `utab.o` and `brz.o`, lets `sureg()` program the MMU,
and checks the mapping both from C and by examining РП/РЗ from the `.ini`. **Run every MMU
test with `set mmu cache`** — the БРЗ write-back hazards are invisible without it, and a
kernel that only works with the cache off would not have worked on the real machine. That
applies past the MMU: **a device reads memory, not the write cache**, so a driver must drain
the БРЗ before a write exchange — `kernel/test/session` is what found the disk driver doing
neither that nor maintaining the sector header it writes from that memory.

**Five of them boot the whole kernel** rather than forging an environment, and they form a
ladder: `boot` asserts the shell's root prompt appears; `console` types a dialogue at that
shell; `session` has the shell write files and `sync`, then converts the container back on the
host, fscks it and diffs what the session wrote (`kernel/test/run-session.sh`); `libtest` runs
the libc suite off the image; and `swap` deposits a much smaller `phymem` before the boot and
runs more processes than the coremap holds, asserting afterwards on the kernel's own
`nswapout`/`nswapin`/`ntextjoin` counters — a load test that cannot say swapping happened is a
load test that passes on a machine with room to spare. `boot` attaches
the pristine `root3072.disk` read-only — which is an assertion in itself, the boot path writing
nothing — and the others each convert their own copy at their own volume number, so no test
ever writes a build artifact.

The kernel objects a test links are compiled *into `kernel/test/`* from the sources next door,
never borrowed from `kernel/`'s own build: `b6_find_src()` locates them by basename, searching
`. .. ../dev` (the CMake equivalent of a Makefile's `vpath`), and `b6_test_obj()` compiles them
into the *program's own* object dir with `-DKERNEL` — **everything there gets it** except the
user-mode `coninit`, because `KERNEL` means "this translation unit is kernel-side" and not
"this object goes in the kernel image", and these programs include `<sys/systm.h>`. (It used
to be applied only to the kernel sources, which is why `<sys/stat.h>`, `<sys/times.h>` and
`<sys/wait.h>` once keyed their user-side prototypes on `_SYS_SYSTM_H` as well — an include
*order* dependency wearing systm.h's guard macro as a disguise, now gone.) The
per-program object dir is not tidiness: many images share a source, and one shared object
output would be emitted into every consuming target and race under `make -j`. Header
dependencies there are deliberately coarse (every object depends on all of `include/sys/*.h`),
because no `-M` support exists to do better.

`make run` runs everything; the ctest **labels** carve it up — `kernel` (SIMH), `lib` (the
libc programs under `b6sim`), `rootfs` (the size checks on the staged native programs) and
`sh` (the shell's own scripts under `b6sim`, which also carry the `rootfs` label).

**The libc suite runs twice, and that is the point.** `lib/test/*.c` is built once and both
run under `b6sim` (label `lib`) and staged onto the disk image, where `kernel/test/libtest`
runs it off `/usr/test` under the booted kernel (label `kernel`) and diffs each program
against **the same `.expected` file**. Under `b6sim` every system call is the host's, so a
kernel bug cannot show; the two harnesses disagreeing means one of them is wrong. Task 25c's
first run found two, both in code nothing else had exercised. Four programs are in one
world only — `spawn` needs a `/bin/sh` that *cannot* be exec'd, `shellt` one that can,
`memt` (which is not a libc test at all, but `/dev/mem`'s user-mode half) needs a kernel whose
memory it can read, and `suidt` (not a libc test either, but `cmd/mkdir`'s, `cmd/mv`'s and
`cmd/rmdir`'s)
needs a kernel that can drop it to uid 7 and a `/bin/mkdir` to exec — and `b6_libtest()`'s
`SIMONLY`/`IMAGEONLY` keywords say which. `termcapt` is
not a libc test either: it is `lib/libtermcap`'s, and it runs in both worlds because it is handed
its database as `argv[1]` — `/etc/termcap` on the image, and *the same file* out of the source
tree under `b6sim`, where a `.args` file's `@srcdir@` is substituted by `run-test.sh`. `cursest`
is `lib/libcurses`' and does the same, and gets into both worlds only because it sets `My_term`:
`initscr()`'s other path calls `gettmode()`, and `b6sim` answers every `ioctl` with success and
does nothing while the real console is `ECHO|CRMOD|XTABS`, so `GT` and `NONL` would differ and
change the cursor motion emitted. `curstty` is the half that follows — it does *not* set
`My_term`, so it is IMAGEONLY, and what it asserts is `kernel/dev/sc.c`'s console. Adding a
program means one `b6_libtest()` call, a name in `lib/test/progs.cmake`, a stanza in
`root.manifest` and a line in `kernel/test/libtest.sh` with its `expect` rule.

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

**The kernel's memory model is settled, and `kernel/README.md` states it** — read that file
before touching anything under `kernel/` that involves memory, and keep it current as you go.
It carries the design, the five hardware rules, the u-area invariant, and the notes for whoever
writes the next standalone SIMH test; `kernel/TODO.md` beside it carries what is left. The
`DONE: how it turned out` narrative it used to carry lives in the source comments and `doc/`
instead, which is where new findings belong too. The
shape of it: the **kernel runs unmapped** (БлП = БлЗ = 1), so a kernel address *is* a physical
address, and the kernel image plus the u-area plus the buffer cache must fit the low 32 pages,
because supervisor instruction fetch is never mapped. Two fixed physical areas are carved off
the top of that space, so the **image itself must end below `062000`** (`KEND`): the **u-area, two
fixed physical pages at `074000`** (`u` is an absolute symbol, not storage), of which the first —
`USIZE` words, and of those only the ones below `r15`, the count riding in the page as
`u_stkdepth` — is copied in and out on a context switch while the second is unsaved kernel-stack
overflow; and **`buffers[NBUF][BSIZE]` at `062000`–`074000`** (`buffers = BUFBASE`,
likewise absolute, declared `extern` in `main.c`), out of bss because the drum and disk
controllers transfer to a *physical* address. **РП always holds the current process's map**, so
a trap switches nothing.
Sizes and addresses are counted in **words**, page-aligned; there is no click. The **shadow map
is `u.u_upt[8]`** — the hardware registers cannot be read back, so this is the only copy —
and `sureg()` (`kernel/utab.c`) loads the whole address space in twelve `рег`s.

Two things that will bite:
- **Drain the БРЗ write cache before every РП write** — `drainbrz()` in `kernel/brz.s`. It
  **cannot be written in C**: the nine stores to physical 1–7 must be consecutive, and `b6cc`
  materializes the destination pointer through a frame slot, so each C store emits two ordinary
  stores of its own and resets the flush counter. Verified by disassembly; don't re-litigate it.
- **The hazard is invisible under default SIMH.** Test with `set mmu cache`, always.

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
- `doc/Memory_Mapping.md` — the MMU as a *program* sees it: the 15-bit/32-page virtual address
  space over 512 Kwords of physical memory, the write-only page registers РП and protection
  register РЗ (`002 020`–`033`), the two independent protection mechanisms (a zero РП entry blocks
  *execution*, an РЗ bit blocks *data*), the БлП/БлЗ override bits that make `copyin`/`copyout`
  free, supervisor mode and the extracode/interrupt gates, fault reporting in ГРП, and the БРЗ
  cache hazard a context switch must respect. Read it before touching `kernel/utab.c`,
  `kernel/besm6.S`, or the machine-dependent block of `include/sys/param.h`.
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
- `doc/Intrinsics.md` — the twelve C compiler intrinsics of `<besm6.h>`, **implemented** in the
  external c-compiler, that let the kernel drive the hardware from C instead of assembly: the
  privileged pair `__besm6_ext` (033 «увв») and `__besm6_mod` (002 «рег»), the PSW trio
  `__besm6_getpsw`/`__besm6_setpsw`/`__besm6_maskpsw` (the mode word at machine register 021, so
  `kernel/psw.s`'s `cli`/`sti`/`getpsw` are now expressible in C too), `__besm6_stop`, the
  bit-manipulation builtins C has no equivalent for (`apx`/`aux` gather and scatter, `acx`, `anx`,
  `arx`), and `__besm6_extracode`. Each compiles to a single inline instruction, never a call —
  including a *computed* `ext`/`mod` address, which rides the C register (`wtc`) and folds into one
  instruction, so a driver may write `__besm6_ext(EXT_DRUM1 + ctlr, cw)`.
  Signatures, semantics, diagnostics, the generated code, and worked examples of an `spl`, an
  interrupt dispatch and a drum read written in C. Read it before writing anything in
  `kernel/dev/` or adding to `kernel/besm6.S` — much of what that file was meant to hold is now
  expressible in C. (Not *everything* is, though: the intrinsics give you the instruction, not
  control over what the compiler emits around it. `drainbrz()` needs nine stores with nothing in
  between, and no C spelling of it survives register allocation — hence `kernel/brz.s`. Check the
  disassembly whenever the *sequence* is the contract, not just the instruction.)
- `doc/Aout_Simulator.md` — the `cmd/sim` simulator (`b6sim`): what it is (an apout-style
  user-level a.out runner, not full-machine SIMH), its CLI and tracing, the Unix v7
  syscall set, and the `$77 N` extracode syscall trap.
- `doc/Kernel_Assembly_Routines.md` — the machine-language assist: every routine's contract with
  its C callers, the globals the assist defines, and what is deliberately absent. The spec for
  `kernel/besm6.S` and its six companion files.
- `doc/Unix_Context_Switch.md` — how *this* kernel takes an interrupt, takes an extracode, saves the
  CPU context, switches address spaces and gets back out: the four gates (`trapgate`/`intrgate`/
  `sysgate`/`badext`), the 21-word `reg.h` trap frame, the shared `intret` exit, `sureg()` and the
  u-area copy in `save()`/`resume()`. Read it before touching `kernel/besm6.S` or `kernel/switch.s`.
- `doc/Unix_V7_System_Calls.md` — the system calls this kernel implements: the four hand-maintained
  copies of the list and which one is authoritative, the `$77 N` gate's argument/result convention,
  a brief entry per call, the rows that are `nullsys`/`nosys`, and the handful of calls whose shape
  changed because `off_t`/`time_t` are one word and the break names a word. Read it before touching
  `kernel/sysent.c`, `kernel/syscall.c`, `include/sys/syscall.h` or `lib/libc/sys/`.
- `doc/Dubna_Context_Switch.md` — the same five questions answered by Dubna, an OS that ran on the
  real machine for two decades. The source of several idioms above (the `its`/`sti` save pipeline,
  the forced Y → A → R restore order, the nine-store БРЗ drain).

**Object/executable format** is a BESM-6-specific `a.out` variant defined in
`cross/besm6/b.out.h` (magic `FMAGIC`/`NMAGIC`, `struct exec` with separate
`const`/`text`/`data`/`bss` segment sizes). The assembler, linker, disassembler, and
simulator all share this header. The shared on-disk serialization lives in `cmd/libaout` and uses the
BESM-6 **6-byte word** (`W == 6`, two 3-byte big-endian half-words, **high half-word
first**, so a word's six bytes read as one big-endian 48-bit number — this holds uniformly
for instructions, `.word`/`.half` data, the constant pool, and the header); the archive member
header (`cross/besm6/ar.h`, `struct ar_hdr`) is word-aligned with 30-char names and
`ARHDRSZ == 60`. The assembler uses **AT&T-style syntax with Madlen mnemonics**; the
disassembler prints the same **ASCII Madlen mnemonics by default**, so its output feeds back
into `b6as`. The Cyrillic BEMSH dialect is opt-in via `b6disasm -b` (both name tables live in
`cmd/disasm/dis.c`).

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
- **The MMU is eight write-only registers, not a page table.** A program sees 15 bits of address —
  **32 pages of 1 Kword** — mapped onto 512 Kwords of physical memory by the page registers РП
  (`002 020`–`027`); the protection register РЗ (`002 030`–`033`) closes a page to data. Neither can
  be read back, so the kernel must keep a shadow page table. **Supervisor instruction fetch is never
  mapped** (so kernel text lives below physical `0100000`) while supervisor *data* access is mapped
  or not according to the БлП mode bit — which is what makes `copyin`/`copyout` a matter of clearing
  one bit rather than switching an address space.

`doc/Besm6_Peripherals.md` has the full address map, every control word's bit fields, and both
interrupt registers bit by bit; `doc/Memory_Mapping.md` has the MMU, supervisor mode and the fault
reports; `doc/Simh_Simulator.md` covers driving the simulator itself.

**`include/` is the Unix v7 system-header tree** (`sys/` plus libc-style headers). The
kernel includes them via `-I../include`. **Every header in it stands alone and includes what
it uses**, so an include list here is a set and not a sequence — v7's `sys/` order (`types.h`,
`param.h`, `systm.h`, then the rest) is gone, along with the `// clang-format off` brackets
that used to protect it in every kernel source. Two consequences worth knowing before adding
a header or an `#include`: `<sys/param.h>` is the one that includes nothing, and must stay
`#define`-only because `kernel/*.S` includes it (there is no `__ASSEMBLER__` here); and
**`b6cpp` rejects a macro redefinition whose replacement text is not character-identical**, so
two headers defining the same name must not merely agree on the value — they must agree
character for character, alignment included. That is why the errno numbering has exactly one
home, `<sys/errno.h>`, which `<errno.h>` and `<sys/user.h>` both include; `<sgtty.h>` and
`<sys/tty.h>` are still an unfixed pair of that kind. See [include/README.md](include/README.md).

## Conventions

- C sources use clang-format (`.clang-format` at repo root). It sorts include blocks, and
  nothing under `kernel/` brackets one against that any more. Two cautions: the tree was
  formatted with a clang-format older than 22, so running it over a file nobody has touched
  produces a few lines of trailing-comment and macro-alignment churn; and it must **not** be
  run over `include/`, where re-spacing a `#define` changes what `b6cpp` compares when the
  same macro is defined twice. `TypeNames:` in the config lists the `<sys/types.h>` scalars,
  without which a cast like `(caddr_t)&x` is misread as a bitwise and.
- Comments and identifiers in the toolchain are frequently in **Russian** (BESM-6 is a
  Russian machine); match the surrounding language when editing a given file.
- Build artifacts (`*.o`, `*.a`, `*.i`, `*.ast`, `*.yaml`, etc.) are git-ignored
- `scripts/vscode-besm6/` is a grammar-only VSCode extension that colors `.s`/`.S` per
  `doc/Assembler_Manual.md`; its mnemonic tables are transcribed from `cmd/as/tables.c` and
  `cmd/disasm/dis.c`, so adding a mnemonic there means adding it here too. Install with
  `ln -s "$PWD/scripts/vscode-besm6" ~/.vscode/extensions/besm6-asm` (see its README) — that
  step is per-user; `.vscode/settings.json` pins the `*.s`/`*.S` association for the workspace.
