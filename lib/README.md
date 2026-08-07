# The user-level libraries

`libc`, `libm`, `libtermcap` and `libcurses` for programs that run **under this kernel** — the C library a Unix v7 user
program links against, ported from v7 to the BESM-6 and refitted to C11. The kernel itself uses
none of it: it has its own `printf` in [`../kernel/prf.c`](../kernel/prf.c) and links only
`libruntime.a` for the `b$*` compiler-support helpers. The declarations these libraries answer
come from [`../include/`](../include/), the one system-header tree, whose hosted half this repo
owns.

## Layout

```text
lib/
    CMakeLists.txt      toolchain setup; recurses into each library
    libc/               libc.a and crt0.o, plus man/ and its own README
        csu/            crt0
        sys/            syscall stubs, cerror, errno, the exec wrappers, sbrk
        gen/            strings, ctype, setjmp, malloc, conversions, <time.h>, misc
        stdio/          FILE machinery, the printf and scanf engines, the accounts
        man/            the v7 manual pages, sections 2 and 3
    libm/               libm.a — the math library, plus its .3m pages and its own README
    libtermcap/         libtermcap.a — termcap, plus termcap.3 and its own README
    libcurses/          libcurses.a — 4.3BSD curses, plus curses.3 and its own README
    test/              programs run under b6sim (CMakeLists.txt + run-test.sh)
```

One function per file, so `b6ranlib`'s index lets `b6ld` pull only what a program actually
calls. `crt0.o` sits beside the archive rather than in it: a program's startup is named on the
link line, never pulled by symbol, exactly as v7 keeps it in `/lib/crt0.o`.

**Each of the four has its own README, and that is where the reasoning lives.**
[`libc/README.md`](libc/README.md) is the long one, in proportion to the library: the `$77` gate
contract and why `sys/` is assembly, every place a fat pointer or a one-word `long` forced a
change, stdio's line buffering, the `_cleanup`-through-a-pointer measurement that is the
difference between a 100-word `hello` and a 2,255-word one, and the eleven upstream bugs fixed
rather than carried. [`libm/README.md`](libm/README.md) is the short one, because that port has a
single theme — overflow is a *fault* and not an infinity, so `HUGE_VAL` is a value a routine
returns and never one it computes, and every range gate sits before the arithmetic.

`libtermcap` landed with [`../include/term.h`](../include/term.h) and the `/etc/termcap` that
[`../etc/`](../etc/) stages onto the disk image; [`libtermcap/README.md`](libtermcap/README.md) is
its account, and the four `char *` comparisons it had to delete are the reason it has one.
`libcurses` is its first consumer and landed next: 4.3BSD curses, and
[`libcurses/README.md`](libcurses/README.md) is its account. Two things there are worth knowing
before reading any of it. **[`../include/curses.h`](../include/curses.h) was replaced**, not
kept — what stood there was v7's `1.7 (4/17/81)` and it is ABI-incompatible with these sources
in five ways, each of which corrupts memory rather than failing to compile. And **eleven `char *`
comparisons** had to go rather than libtermcap's four, because curses is a program made almost
entirely of buffer cursors; the two in `refresh.c` are what decide whether clearing to end of
line is cheaper than printing the blanks. Six upstream bugs are fixed there too, three of them
memory corruption that this machine is what made visible.

## The syscall stubs

Most leaves are **generated**: [`libc/sys/syscalls.tbl`](libc/sys/syscalls.tbl) lists
`symbol macro` per line, and `libc/sys/mkstub` turns each into one `.S` and one object. Adding
a syscall is adding a line and nothing else. Hand-written beside the table are the calls that
cannot be uniform — a second result in r12, an argument that must survive the trap, or no
return on success — plus `cerror.s`, `sbrk.c` and the C `exec*` wrappers.

**No syscall number is written down under `lib/`.** Column 2 of the table is the `SYS_*` macro
of [`../include/sys/syscall.h`](../include/sys/syscall.h), and a stub issues `$77 SYS_write`;
that is what forces the `.S` suffix, since `b6cc` sends a `.S` through the preprocessor and a
`.s` straight to the assembler.

The gate's **arity is the C prototype's**, and nothing in the stub carries it: the kernel reads
it from `sysent[].sy_narg` ([`../kernel/sysent.c`](../kernel/sysent.c)) and `b6sim` from
`syscall_nargs()` ([`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp)). Those two and every
caller must agree — a count that disagrees reads the arguments from the wrong slots and drifts
the user stack by a word per call.

On failure the gate leaves the error number in r14. Since r14 is caller-saved and unreadable
from C, each stub tests it and branches to the shared `cerror`
([`libc/sys/cerror.s`](libc/sys/cerror.s)), which stores it into `errno` — defined in that same
file — and returns −1.

## Building and linking

`lib/` is part of the top-level CMake build (a guarded `add_subdirectory(lib)`), cross-compiled
by the `b6*` toolchain rather than the host compiler, driven through custom commands and sharing
[`../scripts/BesmCross.cmake`](../scripts/BesmCross.cmake). Integrated it uses the **in-tree**
tool targets, so no install has to happen first; standalone (`cmake -S lib -B …`) it falls back
to the installed ones. From the repo root:

```sh
make            # builds cmd/, lib/ and the kernel
make install    # copies libc.a, libm.a, libtermcap.a and crt0.o into <prefix>/share/besm6/lib
make test       # builds the test images
make run        # runs them (ctest; the library tests carry the label `lib')
```

A program links against two to four archives, **ours first**:

```sh
b6ld crt0.o prog.o -o prog -L…/lib -lm -lcurses -ltermcap -lc -lruntime   # whichever it needs
```

That is what `b6cc` puts on the line itself. The order is a contract: `b6ld` scans an archive
once, where it stands, and pulls a member only for a symbol still undefined. libm calls into
libc (`errno`, `frexp`, `ldexp`, `modf`), libcurses calls into libtermcap (`tgetent`, `tgetstr`,
`tgoto`, `tputs`) and libc, libtermcap calls into libc (`getenv`, `open`, `read`,
`write`, the string routines), libc calls the `b$*` helpers, and none of them calls back.
`libruntime.a` comes from the external
[c-compiler](https://github.com/besm6/c-compiler/) and supplies **nothing but** those helpers.

One hazard is silent: a routine missing from `libc.a` can be satisfied from `libruntime.a`
instead of failing to link. Relink without it and read the errors — every undefined name must
be a `b$*`:

```sh
b6ld crt0.o prog.o -o /dev/null -L…/libc -lc
```

## BESM-6 facts this code obeys

Everything in [`../doc/`](../doc/) applies, `Besm6_Data_Representation.md` and
`Besm6_Calling_Conventions.md` above all. What bites in libc specifically:

- **The syscall gate is `$77 N`**: arguments 1…N−1 just below `r15`, the last in the
  accumulator, the result in the accumulator, the error number in r14, and a second result —
  `pipe`, `wait`, `getpid`, `getuid`, `getgid` — in r12, which is an index register and so only
  15 bits wide. The gate performs the callee's stack cleanup, so a stub must **not** pop the
  stack and has **no prologue**; that is why `sys/` is assembly.
- **`char *` is a fat pointer** — marker bit 48, byte offset in bits 47–45 over a 15-bit word
  address — and `int *` is not. Never fabricate one by casting an integer. Ordering is the
  compiler's problem and it handles it: incrementing a fat pointer *decreases* the offset
  field, so `<`/`>` lower through `b$pdiff` rather than comparing the raw words.
- **A flag packed into a word pointer goes above the address**, in bit 16, never in bit 0: an
  address is a *word index*, so bit 0 names the neighbouring word. This is what `malloc`'s
  `BUSY` bit had to become.
- **Every scalar is one word.** `sizeof(int) == 6` char-units, `NBPW == 6`, `long` *is* `int`
  (so `atol` is a call to `atoi`), and one `va_arg` is exactly one word.
- **`break` takes a word address** and is granted a page at a time, so `brk`/`sbrk` keep the
  break as a real `char *`, convert the byte increment themselves, and grow or shrink by a page.
  `sbrk` fails with `NULL`, not `(char *)-1`, which cannot be fabricated here.
- **File I/O counts bytes.** A block is `BSIZE == 3072` bytes (512 words), and `getw`/`putw`
  move **six** bytes, not the PDP-11's two.
- **stdio.** A line-buffered stream is held at `_cnt == 0` — every `putc` misses into `_flsbuf`,
  which writes the line out on `'\n'` — and `stdout` takes that mode when `isatty(1)`.
  `sprintf`/`snprintf` build an `_IOSTRG` stream over the caller's buffer, where `_flsbuf`
  *drops* the overflowing byte and leaves the engine counting, which is C11's return value.
  v7's `_IONBF` flag bit is spelled `_IOUNBUF`, because C11 needs that name for a `setvbuf` mode.
- **`exit` reaches `_cleanup()` through a pointer**, armed by `_flsbuf` on the first buffered
  write, never by name: `crt0` tail-jumps to `exit`, so naming `_cleanup` outright would pull
  the whole of stdio into every program that never prints.
- **A signed value cast to unsigned is not widened.** An integer is a 41-bit two's complement
  field with bits 48–42 zero, and the front end reinterprets rather than sign-extends:
  `(unsigned long)(-1L)` is 2^41−1, *not* `ULONG_MAX`. So the BSD idiom of carrying a signed
  limit through an `unsigned long` — `-(unsigned long)LONG_MIN`, or `acc = LONG_MIN` on an
  unsigned accumulator — reads wrong here. `gen/strtol.c` keeps every cast value-preserving
  instead, and builds the negative end out of `(unsigned long)LONG_MAX + 1`.
- **The terminal is ASCII**, not KOI7, so the v7 `ctype` tables carry over unchanged. The user
  address space is 32 pages with the stack based at `070000`, so text + data + bss must fit
  below it (`b6size -w`).
- **libm: overflow is a fault, underflow is a silent zero.** An exponent past 2^63 kills the
  program; one below 2^−63 quietly becomes zero. So `HUGE_VAL` is a value a routine *returns*,
  never one it computes, and every range gate sits **before** the arithmetic that would
  overflow. `frexp`, `ldexp` and `modf` live in libc, not libm, because the conversions need
  them.
- **ANSI prototypes, clang-format, octal in BESM-6 contexts**, and a comment saying *why*
  wherever the code deviates from v7.

## Testing

`b6sim` is the harness until a root filesystem exists: it runs one user `a.out` and services the
v7 syscalls on the host, which is exactly what libc needs and is far faster than booting SIMH.
[`test/`](test/) holds one program per area — `hello`, `vararg`, `errno`, `procs`, `sbrkt`,
`malloct`, `strings`, `gen`, `strtolt`, `environ`, `jmp`, `headers`, `stdiot`, `printft`,
`scanft`, `execs`, `spawn`, `timet`, `pwent`, `signals`, `matht`, `termcapt`, `cursest`. Each is linked
against the real `crt0.o`
and `libc.a`, run with the arguments in its `.args` file, and its output — fd 1 and fd 2 both —
diffed against its `.expected` file by [`test/run-test.sh`](test/run-test.sh).

Adding a test is adding `<name>.c`, `<name>.expected`, optionally `<name>.args`, and a
`b6_libtest(<name>)` line to [`test/CMakeLists.txt`](test/CMakeLists.txt). A `.args` file may
write `@srcdir@`, which `run-test.sh` substitutes with the source directory — that is how
`termcapt` and `cursest` name a file in the tree by absolute path.

Two programs run on the disk image only, because what they assert is the kernel rather than the
library: `curstty`, which reads and writes the console's real tty modes (`b6sim` answers every
`ioctl` with success and does nothing), and `memt`. `b6_libtest()`'s `IMAGEONLY` keyword says so.

An `.expected` file may record only what the *program* does; nothing host-dependent may reach
it. That is why `environ` checks `getenv` against the vector it was handed instead of printing
names or counts, `timet` converts only literal `time_t` values, `spawn` never starts a real
shell, `pwent` prints no line of `/etc/passwd`, and `termcapt` replaces its own `environ` rather
than let a `$TERMCAP` in the developer's shell decide what it reads.

Once the kernel lands a root filesystem, these same programs go
on it and run under the real kernel on SIMH — the first time the `$77` gate is exercised from
user mode by anything but the kernel's own tests.
