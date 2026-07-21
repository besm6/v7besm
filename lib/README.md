# The user-level libraries

A work plan, in the same spirit as [`../kernel/TODO.md`](../kernel/TODO.md): it says what is to
be built here and in what order. A task is struck from this file as it lands, exactly as its
source file is deleted from `tmp/libc/`, so what is written here is always the work that is
*left*; the account of how a landed task turned out is in its commit. Whatever it taught that
the rest of the library must obey is kept below, under **Ground rules**.

Phase 0 — the ABI defects that had to go before any of this could be written correctly — is
done, and so is the build skeleton: `lib/rules.mk`, the three Makefiles, `csu/crt0.s`, the
`sys/exit.s` and `sys/write.s` leaves, and a `hello` that runs under `b6sim`.

## What this is

`libc` for programs that run **under this kernel**, not for the kernel itself. The kernel links
the external c-compiler's `libc.a` for its handful of string routines; user programs need the
real thing — `crt0`, the syscall stubs, `errno`, `stdio`, `malloc`, the string and conversion
routines — and after that `libm`, `libtermlib` and `libcurses`.

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
- **`errno` cannot be picked up from C.** r14 is caller-saved and the compiler will have
  clobbered it before any C statement runs. The stub itself tests r14 and branches to a shared
  `cerror`, which stores it into `errno` and returns −1. The value is always one of the 34 that
  [`../include/errno.h`](../include/errno.h) defines — the kernel assigns from its own copy in
  [`../include/sys/user.h`](../include/sys/user.h), and `b6sim` maps the host's numbering onto
  the same list — so `sys_errlist` needs 35 entries and no more.
- **`break` takes a word address**, not a byte count, and drops any byte offset: a fat `char *`
  and a plain word address arrive at the gate as the same 15 bits. So a mid-word `char *` would
  floor the break to its word, and `brk`/`sbrk` keep `curbrk` as a real `char *`, convert the
  byte increment with `btow()` themselves, call the gate with a whole number of words, and hand
  back the old break as the fat pointer they already hold. The kernel rounds up to a page and
  refuses growth that reaches the stack at `070000`; `b6sim`'s `Syscall.Break` is the shared
  spec.
- **`char *` is a fat pointer** — bit 48 set, byte offset in bits 47–45 over a 15-bit word
  address — and `int *` is not. Never fabricate one by casting an integer. Six chars pack into a
  word, `sizeof(int) == 6`, `NBPW == 6`, and `int` is 41 bits signed / 48 unsigned.
- **File I/O counts bytes.** `u_count` and `u_offset` are byte counts and `b_addr + on` is fat
  pointer arithmetic ([`../kernel/rdwri.c`](../kernel/rdwri.c)), so stdio's counts are bytes,
  and a block is `BSIZE == 3072` bytes == 512 words.
- **The terminal is ASCII**, not KOI7 ([`../kernel/dev/sc.c`](../kernel/dev/sc.c)). The v7
  `ctype` tables carry over unchanged, and the c-compiler's printf engine must *lose* its
  upper-case folding when it is adopted.
- **The user address space is 32 pages** with the stack based at `070000`, so text + data + bss
  must fit below that. One function per file, so `b6ranlib`'s index lets `b6ld` pull only what a
  program actually calls.
- **ANSI, not K&R.** The kernel sources were converted; libc follows. `va_dcl`-style variadic
  definitions do not parse at all, so `printf`, `execl` and the rest become `(fmt, ...)` over
  the compiler's `<stdarg.h>` — where one argument is exactly one word.
- **The v7 headers win.** [`../cmd/cc/cc.c`](../cmd/cc/cc.c) appends the compiler's
  `share/besm6/include` *after* the user's `-I`, so `-I../../include` resolves `stdio.h`,
  `ctype.h`, `errno.h` … from [`../include/`](../include/) while `<stdarg.h>` still resolves.

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
        stdio/          FILE machinery and the formatting engine
    test/               programs run under b6sim
    libm/               \
    libtermlib/          > phase 7
    libcurses/          /
```

Hand-written Makefiles in the style of [`../kernel/Makefile`](../kernel/Makefile) — `b6cc
-I../../include`, `b6as`, `b6ar`, `b6ranlib`, and a hand-maintained `###` header-dependency
block, since `b6cc` has no `-M`. **Not** part of the CMake build; the toolchain must be
`make install`ed first.

A program links against two archives, ours first:

```sh
b6ld crt0.o prog.o -L$(TOP)/lib/libc -lc $HOME/.local/share/besm6/lib/libc.a
```

The second archive is the external c-compiler's library, and is reached **only** for the `b$*`
compiler-support helpers (`b$save`, `b$ret`, `b$mul`, `b$div`, the relational and conversion
routines) that every compiled function calls. Archives are scanned in order and a member is
pulled only for a symbol still unresolved, so anything we define ourselves is taken from our
own library. The one hazard is silent: a routine we forget to port is satisfied from the
external library instead of failing to link. `b6nm` on the result is the check.

It has to be named by **path**, not as a second `-L … -lc`. `b6ld` keeps one *global* list of
`-L` directories and `-l` searches all of it with first match wins
([`../cmd/ld/pass1.c`](../cmd/ld/pass1.c), [`../cmd/ld/library.c`](../cmd/ld/library.c)), so
with both archives named `libc.a` a second `-lc` finds ours again and every `b$*` goes
undefined. A bare archive path is scanned as a library just the same — `open_input()`
recognises it by its `ARMAG`.

## Phase 1 — crt0, syscall stubs, errno

The milestone — a `hello` that runs under `b6sim`, prints, and exits with a status — is reached:
`csu/crt0.s` reads `argc` at `070000`, derives `argv` and `environ` from the block above it,
establishes the mode register, calls `main(argc, argv, envp)` and passes its accumulator to
`exit`. What is left here is everything a program larger than `hello` needs.

- `sys/cerror.s` and the `errno` word. The two leaves already in `sys/` — `exit.s` and
  `write.s` — ignore r14 and must be revisited once `cerror` exists.
- `sys/syscalls.tbl` plus a make rule generating the ~40 uniform stubs, one object each so link
  granularity survives. Everything with a different shape is hand-written: `pipe`, `fork`,
  `wait`, `getpid`, `getuid`, `getgid` (the r12 second result), `exit`/`_exit`, `brk`/`sbrk`
  (which keeps `curbrk`), `lseek`, and the `exec` family. `signal` registers a handler and no
  more — delivery is phase 6.
- `csu/mcrt0.s` waits for profiling.

## Phase 2 — `gen`: strings, ctype, small utilities

`abs atoi atol index rindex strcat strcmp strcpy strlen strncat strncmp strncpy swab qsort rand
getenv mktemp isatty sleep perror errlst ctype_ abort`, and `setjmp`/`longjmp` rewritten for
this machine (r5–r7, r13, r15 and the mode word). Where the c-compiler's library already has a
BESM-6-aware version — `memcpy memmove memset memcmp memchr strchr strrchr strstr strtok` —
take it rather than porting the v7 one. `qsort` gets a word-wise swap when the element size is
a multiple of six. `mpx.c`, `pkon.c`, `l3.c`, `fakcu.s` and `ldfps.s` are dropped: PDP-11/x86
only, or obsolete.

## Phase 3 — `sbrk` and `malloc`

v7's `gen/malloc.c`, which grows the heap through `sbrk`. Not the c-compiler's allocator: it
claims the whole span from `end` to `070000` on first use, which would defeat the kernel's
demand-grown break and collide with the stack. Every block returned starts on a word boundary.

## Phase 4 — `stdio`

The FILE machinery straight from v7 — `data.c`/`_iob`, `filbuf`, `flsbuf`, `fopen`, `fdopen`,
`freopen`, `endopen`, `fseek`, `ftell`, `rew`, `fgets`, `fputs`, `gets`, `puts`, `getw`, `putw`,
`ungetc`, `setbuf`, `clrerr`, `rdwr`, `fgetc`, `fputc`.

The formatting engine is the exception: v7's `doprnt.s`, `fltpr.s` and `ffltpr.s` are x86
assembly, and the c-compiler's `doprnt.c` already formats BESM-6 floats correctly. Port that
one to a `FILE *` sink, restoring lower-case output and case-sensitive conversion letters (its
KOI7 upper-case folding is a Dubna artifact and does not apply here). Then `printf`, `fprintf`,
`sprintf`, `doscan`/`scanf`, and `ecvt`/`gcvt`/`atof` with the exponent range of the native
format (≈10^±18), reusing the c-compiler's `frexp.s`, `ldexp.s` and `modf.c`.

## Phase 5 — process, accounts, terminals

`execl execv execvp execle`, `system`, `popen`, the `getpwent`/`getgrent` families, `getlogin`,
`ttyname`, `ttyslot`, `stty`/`gtty`, `times`, `timezone`, `ctime`, `crypt`. `nlist` is rewritten
against [`../cross/besm6/b.out.h`](../cross/besm6/b.out.h) — the v7 `a.out` layout does not
apply. `monitor`/`mcount` stay stubs until profiling is wanted.

## Phase 6 — signal delivery

Blocked on the kernel. `sendsig()` in [`../kernel/machdep.c`](../kernel/machdep.c) pushes a
single word and jumps; there is no signal frame, and the handler is not told which signal it is
handling. The frame has to be designed first — the signal number as an argument, the saved
accumulator and mode word, and a way back through it — and implemented kernel-side. Only then
does the libc side follow, and it needs a `dvect`/`tvect`-style trampoline like the x86 port's
only if the kernel does not deliver the number directly.

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
covers `argv` as crt0 finds them and `vararg` the one-word argument; still to come are
`environ`, `errno` from a failing `open`, the two-result syscalls, the string and memory
routines, malloc churn, stdio through a real file, and the printf/scanf conversions including
floats. `b6size -w` on each keeps the image below `070000` words.

Once [`../kernel/TODO.md`](../kernel/TODO.md) task 18b.6 lands a root filesystem, the same
programs are put on it and run under the real kernel on SIMH — which is the first time the
`$77` gate is exercised from user mode by anything but the kernel's own tests.
