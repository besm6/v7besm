# The user-level libraries

A work plan, in the same spirit as [`../kernel/TODO.md`](../kernel/TODO.md): it says what is to
be built here, in what order, and — as tasks land — *how* each turned out when that differed
from the intention. Nothing in this directory is written yet; every phase below is open.

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
  `cerror`, which stores it into `errno` and returns −1.
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

## Phase 0 — foundations

Two ABI bugs and a divergence, all found by reading; the libc cannot be written correctly
around any of them.

**0.1 DONE — the exec argument vector holds plain word addresses.**
[`../kernel/sys1.c`](../kernel/sys1.c) lays the block at the fixed base `USTKPAGE * PGSZ =
070000` — `argc`, the `argv[]` pointers, `0`, the `envp[]` pointers, `0`, then the byte-packed
strings, with `r15` seeded just above — which is exactly what a `crt0` wants, since it needs no
register hand-off. But the pointers it writes are plain word addresses, and a `char *` must
carry the fat marker in bit 48. The compiler dereferences a `char *` with `asx`, whose shift
comes from the operand's exponent field: a plain address asks for a shift of −64 and the load
reads zero.

*How it turned out.* One defect was three, all the same mistake. Beyond the vector itself: the
string cursor was an explicit `(word, offset)` pair that started at offset **0** and counted
*up*, but offset 0 is byte #5 — the word's **last** — so every word went down LSB-first and
`argv[0][1]` would have come back as character 11 even with the marker added; and the *staging*
loop walked the caller's own `argv[i]`, already a fat pointer, with `ap++` on an `int`, stepping
the word address and reading one byte in six. The fix is not the anticipated `suword()`
one-liner but the removal of all hand-built pointer arithmetic: one real `char *` cursor, walked
with `++` (which is `b$pinc`) and stored into the vector as-is. What makes that work — and what
the rest of this library can now rely on — is that `b6cc` lowers `(caddr_t)(int *)w` to an `aox`
of marker + offset 5, `(char *)someInt` and `(int)somePtr` to a silent `COPY`, and `p ± 1` to
`b$pinc`/`b$pdec`. The cast table and the full account are in
[`../kernel/TODO.md`](../kernel/TODO.md); `mmutest` check 25 asserts the round trip, since
`exece()` itself cannot run until 18b.6 mounts a root filesystem.

**0.2 DONE — `break` disagrees about its argument.** The kernel's `sbreak()` did `btow(nsiz)` —
a byte count — while `b6sim` masks the argument to 15 bits and treats it as a word address. A
word address is the sensible reading on a word machine and it is what the simulator already
does, so the kernel is the side that moved.

*How it turned out.* Not a bare `(int)` cast but `ptrword()`
([`../include/sys/param.h`](../include/sys/param.h)), which is `& 077777` — `b6sim`'s
`& BITS(15)` spelled in C, so the two gates now implement one rule literally rather than two
that happen to agree. The consequence for this library is that **either spelling is legal at
the gate**: a fat `char *` (which is what `curbrk` naturally is) and a plain word address both
arrive as the same 15-bit value, and the byte offset is simply dropped. So the ABI is a
word-*aligned* pointer — a mid-word `char *` floors the break to its word — and the `brk`/`sbrk`
pair of phase 1 keeps `curbrk` as a real `char *`, converts its byte increment with `btow()`
itself, calls `brk` with a whole number of words, and hands back the old break as the fat
pointer it already holds. Everything else about the two gates already agreed and was left
alone: both round the new break up to a whole page, both refuse growth that reaches the stack at
`070000` (the kernel through `estabur()`'s `nt + nd > USTKPAGE * PGSZ`), and both return 0.
`btow()` stays everywhere else in [`../kernel/sys1.c`](../kernel/sys1.c): `struct exec`'s
segment sizes really are byte counts. `b6sim` is untouched, and its `Syscall.Break` test is now
the shared spec for both.

**0.3 DONE — `b6sim`'s exec ABI was a placeholder.**
[`../cmd/sim/machine.cpp`](../cmd/sim/machine.cpp) pushed `argc` and left `argv` in the
accumulator, saying so in its own comment; the initial program load built no block at all, and
`b6sim` had no way to be given program arguments in the first place. It now builds the kernel's
block at `070000`, so one `crt0` serves the simulator and the real machine.

*How it turned out.* The register hand-off was **deleted, not replaced**: `setregs()`
([`../kernel/sys1.c`](../kernel/sys1.c)) zeroes ACC and `r1`–`r14`, and `load_program()` already
calls `Processor::reset()`, so matching the kernel meant removing the `set_acc`/`set_m(14,…)`
lines and letting the block at the fixed address be the whole ABI. Two more defects came out of
the same read. The old builder word-**aligned** every string, so every fat pointer it wrote had
offset field 5; `exece()` packs them contiguously, six to a word, so all but `argv[0]` normally
begins mid-word and carries a real byte offset — the pointer has to be taken from the live
cursor (`5 - byte_index`), which is what `Cpu.ExecArgBlock` pins down with a string set chosen to
straddle. And `exec()` ended with `program_break = top + 1`, parking the heap break on top of the
stack region; the break belongs to `load_program()`, above the bss, and `exec()` no longer
touches it. Once `r15` starts *above* the block, `break`'s ceiling could no longer be the current
`r15` either — it is `STACK_BASE` now, matching `estabur()`, or the heap would have grown into
the arguments as the stack climbed.

The initial load and a guest `exec` are one code path: `Session::run()` calls `Machine::exec()`
rather than `load_program()`. `b6sim` takes program arguments — `b6sim [options] program
[arguments...]`, with a `+` on the getopt string so parsing stops at the program name and options
after it reach the guest — and passes a **whitelisted** environment (`LANG`, `LC_ALL`, `TERM`,
`SHELL`, `PATH`, `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `EDITOR`, `PAGER`, `MAKEFLAGS`, verbatim,
skipping the unset), since a v7 program has no use for a modern shell's hundreds and the block
competes for the few thousand words below the stack guard. An arg list past `NCARGS` is refused
rather than scribbled over the stack. Five `Cpu.Exec*` tests in `../cmd/sim/test/sim_test.cpp`
cover the layout, the empty block, the cleared registers, the untouched break and the ceiling;
[`../doc/Aout_Simulator.md`](../doc/Aout_Simulator.md) §3 now carries the block diagram.

**0.4 DONE — the build skeleton.** `lib/Makefile`, `lib/libc/Makefile`, `lib/test/Makefile`,
and the link recipe in one place — `lib/rules.mk`, which every library and the test harness
include after setting `TOP` and declaring their default target.

*How it turned out.* The two-archive recipe this file first carried could not link anything:
`b6ld` keeps a single global `-L` list searched first-match-first, so the second `-lc` re-found
*our* `libc.a` and every `b$*` helper went undefined. Naming the external archive by path fixes
it, and the section above now says so — spelled the old way, `b6ld crt0.o hello.o -L../libc -lc
-L$HOME/… -lc` still fails on demand, which is how one can check that the path is load-bearing
rather than cosmetic. The skeleton also could not be
content-free: `b6ar cr libc.a` with an empty member list is a usage error, so a Makefile with
nothing to archive cannot even be run, let alone verified. It therefore lands with the smallest
real content that proves it — `csu/crt0.s`, `sys/exit.s`, `sys/write.s` and `test/hello.c` — and
phase 1 starts at `cerror`/`errno` rather than at `crt0`. Two smaller things fell out of the
writing. `crt0` needs no address extension to reach the argument block: a short address field
covers `[0..07777]` **and** `[070000..077777]`, the segment bit being worth exactly `+070000`,
so `xta 070000` is one instruction and only the `environ` reference takes a `<sym>` escape. And
the link rule's `$^` must not see `crt0.o` or `libc.a`, which `$(link)` names itself — they are
order-only prerequisites, the same trap [`../kernel/test/Makefile`](../kernel/test/Makefile)
documents. `make test` in `lib/` builds both products, links `hello` through the real recipe,
runs it under `b6sim` with arguments and diffs the output, which is what pins crt0's `argc`/
`argv` derivation.

**0.5 Header touch-ups** in [`../include/`](../include/): rewrite `varargs.h` in BESM-6 terms
(one word per argument, compatible with the compiler's `<stdarg.h>`), set `BUFSIZ` to 3072 in
`stdio.h`, and reconcile `errno.h` with the error numbers the kernel actually returns.

## Phase 1 — crt0, syscall stubs, errno

The milestone is a `hello` that runs under `b6sim`, prints, and exits with a status. Task 0.4
reached it: what is left here is everything a program larger than `hello` needs.

- ~~`csu/crt0.s`~~ — done in 0.4: reads `argc` at `070000`, derives `argv` and `environ` from
  the block above it, establishes the mode register, calls `main(argc, argv, envp)`, passes its
  accumulator to `exit`. (`csu/mcrt0.s` waits for profiling.)
- `sys/cerror.s` and the `errno` word. The two leaves 0.4 wrote — `sys/exit.s` and
  `sys/write.s` — ignore r14 and must be revisited once `cerror` exists.
- `sys/syscalls.tbl` plus a make rule generating the ~40 uniform stubs, one object each so link
  granularity survives. Everything with a different shape is hand-written: `pipe`, `fork`,
  `wait`, `getpid`, `getuid`, `getgid` (the r12 second result), `exit`/`_exit`, `brk`/`sbrk`
  (which keeps `curbrk`), `lseek`, and the `exec` family. `signal` registers a handler and no
  more — delivery is phase 6.
- ~~`test/hello.c` and a `make test`~~ — done in 0.4. Each program is linked against the real
  `crt0.o` and `libc.a`, given the arguments in its `.args` file, and diffed against its
  `.expected` file; adding a test is adding those three files and a name to `PROGS`.

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
`libc.a` and run with its output diffed: `argv`/`environ` as crt0 finds them, `errno` from a
failing `open`, the two-result syscalls, the string and memory routines, malloc churn, stdio
through a real file, and the printf/scanf conversions including floats. `b6size -w` on each
keeps the image below `070000` words.

Once [`../kernel/TODO.md`](../kernel/TODO.md) task 18b.6 lands a root filesystem, the same
programs are put on it and run under the real kernel on SIMH — which is the first time the
`$77` gate is exercised from user mode by anything but the kernel's own tests.
