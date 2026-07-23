# The user-level libraries

A work plan, in the same spirit as [`../kernel/TODO.md`](../kernel/TODO.md): it says what is to
be built here and in what order. A task is struck from this file as it lands, exactly as its
source file is deleted from `tmp/libc/`, so what is written here is always the work that is
*left*; the account of how a landed task turned out is in its commit. Whatever it taught that
the rest of the library must obey is kept below, under **Ground rules**.

## What this is

`libc` for programs that run **under this kernel**, not for the kernel itself. The kernel needs
none of it: it defines its own `printf` in [`../kernel/prf.c`](../kernel/prf.c) and references no
`str*`, `mem*`, `bcopy` or `bzero` anywhere, so it links only `libruntime.a` for the `b$*`
helpers. User programs need the real thing — `crt0`, the syscall stubs, `errno`, `stdio`,
`malloc`, the string and conversion routines — and after that `libm`, `libtermlib` and
`libcurses`.

Two source trees feed it:

- **[`tmp/libc/`](tmp/libc/)** — Unix v7 libc as ported to x86 by Robert Nordier (`csu`, `crt`,
  `gen`, `stdio`, `sys`; ≈5200 lines). This is the *content*: the algorithms, the v7 semantics,
  the file-by-file decomposition. Its machine-dependent parts (`.s` files, `doprnt.s`,
  `fltpr.s`, `setjmp.s`, every `sys/*.s`) are x86 and are rewritten, not ported.
- **`~/Project/Besm-6/c-compiler/libc/besm6/`** — a small BESM-6-native libc written for this
  same toolchain: a full printf engine (`doprnt.c`), `malloc.c`, the `mem*`/`str*` family, and
  the compiler-support routines in `unix/*.s`. This is the *form*: how a BESM-6 C routine
  handles fat pointers, one-word scalars, and the `$77` syscall leaf. `unix/read.s`,
  `unix/write.s`, `unix/crt0.s` are the models for `sys/` and `csu/`.
  It is a **source tree to copy from, not a library to link**: that project now installs
  `libruntime.a` and nothing else, its own libc having been split off as `libc0.a` for its
  own tests. Where it already has a routine, **take it rather than porting the v7 one** —
  phase 2 took the whole `str*`/`mem*` family and `atoi` that way. Its versions are ANSI
  already and their signatures match the `<string.h>`/`<stdlib.h>` this same tree supplies, so
  nothing ends up defined as `int n` where the declaration says `size_t n`. Each copied file
  says at its head where it came from; a divergence has to be written down there.

The ported file is deleted from `tmp/libc/` as it lands, so what is left there always names the
remaining work; `tmp/` goes away entirely at the end.

## Ground rules

Everything in [`../doc/`](../doc/) applies, `Besm6_Data_Representation.md` and
`Besm6_Calling_Conventions.md` above all. The consequences that bite in libc specifically:

- **The syscall gate is `$77 N`.** Arguments 1…N−1 sit just below `r15`, the last one is in the
  accumulator, the result comes back in the accumulator, **`errno` in r14** (zero on success —
  there is no carry flag), and a second result, where v7 has one, in **r12**
  (`pipe`, `wait`, `getpid`, `getuid`, `getgid`). Both implementations of the gate agree:
  [`../kernel/syscall.c`](../kernel/syscall.c) and
  [`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp).
- **A stub must not pop the stack.** The gate stands in for the called function and performs the
  callee's cleanup itself — `tr->r15 -= n - 1` in the kernel, the same in `b6sim`.
- **A stub therefore has no prologue.** The C calling convention already places the arguments
  where the gate reads them, so the stub is a bare `$77 N` and a return; `b$save` would move
  them. This is why `sys/` is assembly and not C.
- **The number is a name, and the leaf is `.S`.** No syscall number is written down anywhere
  under `lib/`: a leaf says `$77 SYS_write`, from
  [`../include/sys/syscall.h`](../include/sys/syscall.h), and `sys/syscalls.tbl` maps a libc
  symbol to the macro it issues (they differ for `lseek`/`SYS_seek` and `_break`/`SYS_break`).
  That forces the suffix — `b6cc` sends a `.S` through the preprocessor and a `.s` straight to
  the assembler ([`rules.mk`](rules.mk)) — so a new leaf that traps must be `.S`. `cerror.s`
  and `csu/crt0.s` stay `.s`: neither issues an extracode. b6as's `#` constant-pool operator
  passes through cpp untouched (`sys/dup.S` has both `aox #0100` and `$77 SYS_dup`), because a
  `#` that does not start a line is an ordinary token.
- **The gate's arity is the C prototype's**, and nothing in the stub carries it: the gate reads
  it from `sysent[].sy_narg` ([`../kernel/sysent.c`](../kernel/sysent.c)) and from
  `syscall_nargs()` ([`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp)), which must agree with
  each other and with every caller. A count that disagrees reads every argument from the wrong
  slot *and* drifts the user stack by a word per call. Two PDP-11 leftovers were fixed this way
  in phase 1 — `seek` and `stime` each shed the second word of a `long` that is one word here —
  and `dup` is the reverse case: it is a two-argument gate whichever name you call, so `dup(fd)`
  pushes a word it does not have and `dup2(fd, fd2)` sets bit `0100` in the first, exactly as v7
  does.
- **An extracode returns to the left half of the *next* word.** Whatever is packed after `$77 N`
  in its own word is never executed, and no gate can recover it — the half is not recorded
  anywhere. `b6as` now word-aligns after an extracode (the `TALIGN` that `vjm`/`ij`/`stop`
  already carried), so a stub may be written as the obvious three instructions; but a stub
  assembled with `-a`, or by any other assembler, may not.
- **`errno` cannot be read in C at the call site**, only *set* by `cerror`. A stub that forgets
  the `14 v1m cerror` still returns −1 — the gate puts it there — and only `errno` gives it away,
  which is what `test/errno` exists to catch.
- **`errno` cannot be picked up from C.** r14 is caller-saved and the compiler will have
  clobbered it before any C statement runs. The stub itself tests r14 and branches to a shared
  `cerror`, which stores it into `errno` and returns −1. The value is always one of the 34 that
  [`../include/errno.h`](../include/errno.h) defines — the kernel assigns from its own copy in
  [`../include/sys/user.h`](../include/sys/user.h), and `b6sim` maps the host's numbering onto
  the same list — so `sys_errlist` needs 35 entries and no more.
- **`break` takes a word address**, not a byte count, and drops any byte offset: a fat `char *`
  and a plain word address arrive at the gate as the same 15 bits. So a mid-word `char *` would
  floor the break to its word, and `brk`/`sbrk` keep `curbrk` as a real `char *`, convert the
  byte increment themselves, call the gate with a whole number of words, and hand back the old
  break as the fat pointer they already hold. The kernel rounds up to a page and refuses growth
  that reaches the stack at `070000`; `b6sim`'s `Syscall.Break` is the shared spec.
- **`sbrk` fails with NULL, not `(char *)-1`.** v7's value would have to be fabricated from an
  integer, which is the one thing the fat-pointer rule below forbids; the break can never
  legitimately be word 0, so NULL costs nothing. `malloc` tests for NULL.
  `sbrk` rounds *up* when growing (`btow()`) and *toward zero* when shrinking — `btow()` is
  `(x + 5) / 6` and C truncates a negative quotient, so it would free one word too few — and it
  refuses an increment large enough to wrap the 15-bit word address, which an `int` is wide
  enough to express and a `char *` is not. **The break is granted a page at a time** — both
  the kernel and `b6sim` round it up to 1024 words — so an allocator's growth chunk and the
  step it backs off by when refused are both a page: anything smaller asks for exactly what
  was just given, or exactly what was just refused.
- **A second result is only 15 bits.** r12 is an index register, so `wait`'s status — `(code <<
  8)` — is truncated for any exit code above 127. It bites identically on the kernel and under
  `b6sim`. Widening it means giving `wait` an argument again and writing the status through the
  caller's pointer kernel-side; until then, exit codes stay small.
- **`char *` is a fat pointer** — bit 48 set, byte offset in bits 47–45 over a 15-bit word
  address — and `int *` is not. Never fabricate one by casting an integer. Six chars pack into a
  word, `sizeof(int) == 6`, `NBPW == 6`, and `int` is 41 bits signed / 48 unsigned.
- **A flag packed into a pointer goes *above* the address, never in bit 0.** A word pointer
  holds its 15-bit address in bits 15–1 with bits 48–16 zero, so bit 16 — one past the top of
  the address space — is free, and it stays clear of the bit-48 marker that would make the
  value look fat. Bit 0 is not free: an address is a *word index*, so a one-word object's
  neighbour is one away and setting the low bit names it. This is the whole of what had to
  change in v7's `malloc`, whose `BUSY` flag lives in bit 0 on the PDP-11 — and it is cheap,
  because the casts it takes are free: pointer↔integer is a bare copy in both directions
  (every type is one word), and `(char *)` of a word pointer sets the marker with the offset
  of byte **#0**, so the fat pointer names the word's first byte and casting back recovers it.
- **A null word pointer cast to `char *` is not a zero word.** The cast sets the marker and an
  offset over the zero address, so `p == NULL` still answers correctly — the compiler compares
  the address part alone — but `if (!p)` need not. Return a plain `NULL` from the failure path
  rather than casting one.
- **File I/O counts bytes.** `u_count` and `u_offset` are byte counts and `b_addr + on` is fat
  pointer arithmetic ([`../kernel/rdwri.c`](../kernel/rdwri.c)), so stdio's counts are bytes,
  and a block is `BSIZE == 3072` bytes == 512 words. `getw`/`putw`, which move a *word*, move
  **six** bytes and not the PDP-11's two.
- **`FILE` grew two fields, and both were free.** `_flag` is an `int` because all eight bits of
  v7's `char` were spoken for and line buffering needed a ninth (`_IOLBUF`); `_bufsiz` is new
  because `setvbuf` lets a caller name a size and v7, which had only `setbuf`, wrote `BUFSIZ`
  into `_filbuf` and `_flsbuf` outright. Neither costs anything: a `char` **struct member**
  occupies a whole word here anyway — `struct sgttyb` is four chars in four words.
- **A line-buffered stream is held at `_cnt == 0`.** That is the whole mechanism, and it is why
  the `putc` macro shows no sign of the mode: every `putc` misses and lands in `_flsbuf`, which
  appends the byte and writes the line out on `'\n'`. The fully buffered fast path is untouched,
  and `stdout` takes the mode when `isatty(1)` — where v7 went fully *un*buffered and spent a
  syscall per character.
- **`_doprnt` sinks into a `FILE *`, and `_IOSTRG` is what makes `snprintf` count.** The engine
  is the c-compiler's `doprnt.c`, not v7's x86 assembly; it emits through `putc` and returns the
  number of characters it produced. `sprintf`/`snprintf` build a v7 `_IOSTRG` stream on the
  stack over the caller's buffer, and `_flsbuf` **drops** the byte when such a stream fills,
  leaving the engine counting characters it could not store — which is exactly C11's return
  value. The KOI7 upper-case fold the engine arrived with is gone: this terminal is ASCII, so
  `%x`/`%X`, `%e`/`%E` and `%g`/`%G` are three distinct pairs again.
- **`exit` is C and `_exit` is the bare trap.** `exit` lives in `gen/cuexit.c` and runs
  `_cleanup()` — which `fclose`s every stream — before calling `_exit`. `crt0.s` tail-jumps to
  `exit`, so *every* program links the stdio machinery whether it prints or not; v7 makes the
  same bargain, and a static linker pulling members by symbol cannot make it conditional.
- **The terminal is ASCII**, not KOI7 ([`../kernel/dev/sc.c`](../kernel/dev/sc.c)). The v7
  `ctype` tables carry over unchanged, and the c-compiler's printf engine must *lose* its
  upper-case folding when it is adopted.
- **The user address space is 32 pages** with the stack based at `070000`, so text + data + bss
  must fit below that. One function per file, so `b6ranlib`'s index lets `b6ld` pull only what a
  program actually calls.
- **ANSI, not K&R.** The kernel sources were converted; libc follows. `va_dcl`-style variadic
  definitions do not parse at all, so `printf`, `execl` and the rest become `(fmt, ...)` over
  the compiler's `<stdarg.h>` — where one argument is exactly one word.
- **There is one header tree, this repo owns the hosted half of it, and that half is C11.**
  [`../include/`](../include/) holds the v7 headers plus the C11 ones v7 never had, adapted from
  the c-compiler's tree; where the two overlapped (`stdio.h`, `ctype.h`, `errno.h`, `setjmp.h` …)
  the v7 header stayed and was refitted rather than replaced. Four consequences land on code
  written here: `assert()` is an expression and `<assert.h>` is unguarded on purpose;
  `toupper`/`tolower` are functions and v7's unconditional macros are `_toupper`/`_tolower`;
  `isprint(' ')` is now true; and a signal handler is `void (*)(int)`, which is what phase 6 has
  to deliver. `libc.a` answers every declaration `<stdio.h>` makes, the C11 additions v7 never
  had included — the v-forms, `snprintf`, `setvbuf`, `fgetpos`/`fsetpos`,
  `remove`/`rename`/`tmpfile`/`tmpnam` — most of them thin wrappers over the v7 core. One name
  to watch there: v7's
  `_IONBF` flag bit is spelled `_IOUNBUF` now, because C11 needs `_IONBF` for a `setvbuf` mode.
  `<math.h>` names three routines that are in **libc** and not the libm of phase 7 — `modf`,
  `frexp` and `ldexp` — because the conversions need them, and v7 keeps them in `gen/` too.
  The freestanding ten — `<stddef.h>`,
  `<stdarg.h>`, `<limits.h>`, `<besm6.h>` and the rest — are the *compiler's*, and it installs
  them into the same directory; they are not in this tree at all.
  `-I../../include` names ours in the source tree, ahead of the installed copy that
  [`../cmd/cc/cc.c`](../cmd/cc/cc.c) appends to every preprocessor run, so a build here sees what
  it just edited. Nothing under `lib/` should
  add a header to `include/` merely to declare its own function: a routine that no v7 header
  declares (`index`, `rindex`, `swab`, `mktemp`, `isatty`, `perror`) declares itself at the
  head of its file, exactly as `test/*.c` declares the syscalls it calls.
- **A relational between two `char *` is safe**, even though a fat pointer does *not* sort as
  a plain word: incrementing one *decreases* the byte offset, which sits above the word address
  in bits 47–45, so a raw word compare of two pointers into the same word comes out backwards.
  The compiler lowers `<`/`>` between fat pointers through `b$pdiff`, the same helper as `-`,
  and tests the sign. `memmove`'s direction test and `qsort`'s partition both depend on it, and
  `test/strings` overlaps *within one word* on purpose to keep it that way.
- **A call through a function pointer is a `wtc` of the pointer and a bare `13 vjm`**,
  wherever the pointer lives — a parameter, an auto, an array element or a file-scope
  variable (`wtc` carries a 15-bit address, so it reaches a global with no `utc` escape).
  A callback needs no special handling: `qsort` keeps its comparison in the file-scope static
  v7 used.
- **`long` is `int`, one word**, so `atol` *is* `atoi` and is written as a call to it; the
  same collapse is why `lseek` and `stime` shed a word in phase 1. `sizeof(int) == 6` char-units
  and `sizeof(char) == 1`, so `sizeof arr / sizeof arr[0]` still counts elements.
- **What links but cannot run yet.** `sleep` needs a `SIGALRM` handler of its own and `abort`
  raises `SIGIOT`; delivery is phase 6, and `b6sim` answers `signal()` with anything but
  `SIG_DFL`/`SIG_IGN` with `EINVAL`. Both are in `libc.a` and neither has a test — a test of
  `abort` would take the simulator down with the program, `b6sim` servicing `kill` by killing
  its own process.

## Layout and build

```
lib/
    README.md           this plan
    Makefile            recurses into each library
    rules.mk            tools, flags, and the link recipe, in one place
    libc/
        Makefile        builds libc.a and crt0.o
        csu/            crt0
        sys/            syscall stubs, cerror, errno
        gen/            strings, ctype, setjmp, malloc, misc
        stdio/          FILE machinery, the formatting engine, the scanning engine
    test/               programs run under b6sim
    libm/               \
    libtermlib/          > phase 7
    libcurses/          /
```

Hand-written Makefiles in the style of [`../kernel/Makefile`](../kernel/Makefile) — `b6cc
-I../../include`, `b6as`, `b6ar`, `b6ranlib`, and a hand-maintained `###` header-dependency
block, since `b6cc` has no `-M`. **Not** part of the CMake build, and it could not be: these
are the *installed* `b6*` tools, so `make && make install` at the top level has to come first,
and `make -C lib install` after — the three-step bootstrap of the top-level
[`../README.md`](../README.md). `install` here copies `libc.a` and `crt0.o` into
`<prefix>/share/besm6/lib`, beside `libruntime.a`, which is where `b6cc` looks for both.

A program links against two archives, ours first:

```sh
b6ld crt0.o prog.o -L$(TOP)/lib/libc -L$HOME/.local/share/besm6/lib -lc -lruntime
```

That is what `b6cc` itself puts on the line when it links, and `$(link)` in
[`rules.mk`](rules.mk) does the same for the programs built here. The second archive is the
external c-compiler's, and is reached **only** for the `b$*` compiler-support helpers
(`b$save`, `b$ret`, `b$mul`, `b$div`, the relational and conversion routines) that every
compiled function calls; it is all that project installs for this toolchain, and the libc, the
`crt0.o` and the headers are ours.

The order is the contract. An archive is scanned once, where it stands on the line, and a
member is pulled only for a symbol still unresolved, so `libc.a` must precede `libruntime.a`:
libc calls the helpers, and no helper calls back into libc (`b6nm libruntime.a` shows its only
undefined names are `b$*` of its own).

The one hazard is silent: a routine we forget to port is satisfied from the external archive
instead of failing to link. The check is to relink *without* it and read the errors:

```sh
b6ld crt0.o prog.o -o /dev/null -L$(TOP)/lib/libc -lc      # every undefined name must be b$*
```

Anything other than a `b$*` in that list is a routine the external library has been quietly
supplying. The check used to be the only way to tell, since both archives were called `libc.a`
and named their members `strcpy.o`, `write.o`, `exit.o` alike, so `b6nm` on the result could
not say whose was pulled; that ambiguity went with the rename, and the check is now exact.

## Phase 5 — process, accounts, terminals

`execl execv execvp execle`, `system`, `popen`, the `getpwent`/`getgrent` families, `getlogin`,
`ttyname`, `ttyslot`, `stty`/`gtty`, `times`, `timezone`, `ctime`, `crypt`, `tell`. `nlist` is rewritten
against [`../cross/besm6/b.out.h`](../cross/besm6/b.out.h) — the v7 `a.out` layout does not
apply. `monitor`/`mcount` and `csu/mcrt0.s` — phase 1's one leftover — stay stubs until
profiling is wanted.

## Phase 6 — signal delivery

Blocked on the kernel. `sendsig()` in [`../kernel/machdep.c`](../kernel/machdep.c) pushes a
single word and jumps; there is no signal frame, and the handler is not told which signal it is
handling. The frame has to be designed first — the signal number as an argument, the saved
accumulator and mode word, and a way back through it — and implemented kernel-side. Only then
does the libc side follow, and it needs a `dvect`/`tvect`-style trampoline like the x86 port's
only if the kernel does not deliver the number directly.

**The number is no longer optional.** [`../include/signal.h`](../include/signal.h) declares the
C11 shape, `void (*signal(int, void (*)(int)))(int)`, so every handler now takes the signal it is
handling as its one argument and `sleep()` is already written that way. Delivering it is the
frame's job.

## Phase 7 — libm, libtermlib, libcurses

`libm` from the v7 sources, with constants and range checks refitted to the 40-bit mantissa and
the 2^±63 exponent — no infinities, no NaNs, no denormals to fall back on. Then `libtermlib`
(termcap) and `libcurses`; [`../include/curses.h`](../include/curses.h) and
[`../include/unctrl.h`](../include/unctrl.h) are already in the tree. Each is a separate archive
built by the same recursion and exercised by the same `b6sim` harness.

## Porting checklist, per file

K&R → ANSI prototypes · clang-format · no 16- or 32-bit width assumptions · `sizeof(int) == 6`
and `NBPW == 6` · `char *` is fat and `int *` is not · octal in BESM-6 contexts · one function
per file · comments in the repo's voice, saying *why* a BESM-6 deviation was necessary.

## Testing

`b6sim` is the harness until a root filesystem exists — it runs a user `a.out` and services the
v7 syscalls on the host, which is exactly what libc needs and is far faster to iterate on than
booting SIMH. `test/` holds one program per area, each linked against the real `crt0.o` and
`libc.a`, given the arguments in its `.args` file, and run with its output diffed against its
`.expected` file — so adding a test is adding those three files and a name to `PROGS`. `hello`
covers `argv` as crt0 finds them, `vararg` the one-word argument, `errno` a failing call and the
`cerror` arm, `procs` the syscalls that answer in r12, `sbrkt` the break, `malloct` the
allocator, `strings` the string
and memory routines, `gen` the small utilities, `environ` the vector crt0 computes, `jmp`
setjmp/longjmp, `stdiot` the FILE machinery through a real file, `printft` every printf
conversion including floats, and `scanft` scanf and the `atof`/`ecvt`/`gcvt` conversions
either side of it. `b6size -w` on each keeps the image below `070000` words.

An `.expected` file may record only what the *program* does. Nothing host-dependent may reach
it — which is why `environ` checks `getenv` against the vector it was handed, entry by entry,
and prints neither a name nor a count: `b6sim` passes through a whitelist of the host's
variables (`ENV_WHITELIST` in [`../cmd/sim/session.cpp`](../cmd/sim/session.cpp)), and even
`MAKEFLAGS` alone would make a count differ between `make test` and the same run by hand. The
harness captures fd 2 along with fd 1, so `perror`'s output is diffed too.

Nothing here may be linked against a stale library: `$(LIBDEP)` names `crt0.o` and `libc.a` as
ordinary prerequisites of every program and `$(link)` filters them back out of `$^`, because
order-only prerequisites — which is how this started — never trigger a relink at all.

Once [`../kernel/TODO.md`](../kernel/TODO.md) task 18b.6 lands a root filesystem, the same
programs are put on it and run under the real kernel on SIMH — which is the first time the
`$77` gate is exercised from user mode by anything but the kernel's own tests.
