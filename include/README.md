# The system header tree

The headers a BESM-6 program compiles against — the Unix v7 ones (`sys/` included), plus the
hosted C11 headers v7 never had. The kernel and `lib/libc` reach them as `-I../include`, and
the top-level `make install` copies the tree to `<prefix>/share/besm6/include`, which `b6cc`
appends to every preprocessor run. So a source with no `-I` of its own still finds
`<string.h>`, and one built in the tree gets the same files it would after installation.

**The tree is also on the disk image, as `/usr/include`** (task C9e, [../cmd/README.md](../cmd/README.md)),
because the machine's own `/usr/bin/cc` appends *that* directory for the same reason. It is
staged from the **source** tree rather than from an installed copy — the top-level
`CMakeLists.txt`'s `B6_STAGE_INC` — so an edit here reaches the image without an install; the
ten freestanding headers below come from where the external compiler installed them, since
nothing here can produce them. Both halves go, because one without the other preprocesses
nothing.

**Two owners share that installed directory.** The *freestanding* set — `stddef.h`,
`stdarg.h`, `limits.h`, `float.h`, `stdbool.h`, `stdint.h`, `iso646.h`, `stdalign.h`,
`stdnoreturn.h`, and `besm6.h` — belongs to the external
[c-compiler](https://github.com/besm6/c-compiler/), which installs those ten from
`libc/besm6/include/` and nothing else. They describe the *compiler*: its data model, its
`<stdarg.h>` ABI, and the `__besm6_*` intrinsics it lowers to single instructions
([`../doc/Intrinsics.md`](../doc/Intrinsics.md)). This directory once carried copies of them
and no longer does — a copy could only drift out of step with the back end that defines it.

What stays here is everything a *hosted* implementation adds, which is what `lib/libc` backs:
the v7 headers, plus the C11 headers v7 never had, adapted from the compiler's tree (it ships
those but does not install them, for exactly this reason). Where the two trees overlap —
`assert.h`, `ctype.h`, `errno.h`, `math.h`, `setjmp.h`, `signal.h`, `stdio.h`, `time.h` — the
**v7** header is the one that stays; that is the whole reason this directory exists.

**The hosted set is C11 now**, which it was not: eight of those overlapping headers were still
the untouched pre-ANSI v7 originals, declaring nothing or declaring it with empty parens, and
`stdio.h` did not declare `printf` at all — a wall in front of `lib/`'s phase 4, since the front
end has no implicit declarations. They were rewritten in place: v7's constants, layout and
extensions kept, every declaration a prototype, and whatever C11 mandates added beside them.
What that cost is written down in each header, but the four worth knowing here are

- `assert()` is an expression, not a brace block, so it survives an `if`/`else`, and the file is
  the one header in this tree deliberately left **unguarded** — C11 §7.2 re-examines `NDEBUG` at
  every inclusion.
- `toupper`/`tolower` became functions, because the conditional fold C11 requires cannot be a
  macro that evaluates its argument once. v7's unconditional pair survives as `_toupper`/`_tolower`.
- `isprint(' ')` is true, which under v7 it was not; the free bit `_B` in `lib/libc/gen/ctype_.c`
  is what separates `isprint` from `isgraph`.
- `signal()` takes and returns C11's `void (*)(int)`. That commits phase 6's `sendsig()` to
  passing the signal number — see `kernel/README.md`.

Six headers that were missing outright came over whole: `locale.h`, `fenv.h`, `wchar.h`,
`wctype.h`, `uchar.h`, `tgmath.h`. Three are refused from that tree on purpose — its `errno.h`
(a six-entry non-POSIX set, where ours is the kernel's list and is load-bearing on both sides),
its `malloc.h` (the claim-everything allocator `lib/README.md` phase 3 rejects), and its KOI7
case folding (this terminal is ASCII).

`complex.h`, `stdatomic.h` and `threads.h` are **not** here and never will be: no complex type,
no atomic instructions, no threads. A hosted implementation owes the program that news, so
`b6cpp` predefines `__STDC_NO_COMPLEX__`, `__STDC_NO_ATOMICS__`, `__STDC_NO_THREADS__` and
`__STDC_NO_VLA__` ([`../cmd/cpp/cpp.c`](../cmd/cpp/cpp.c)).

`lib/test/headers.c` is what keeps all of this true: it includes every header in the tree twice
and checks the handful of behaviours that would otherwise fail silently.

**Every header in this tree stands alone**, `sys/` included, and that is new. v7's `sys/`
headers assumed the caller had included the right ones first, in the right order — `types.h`,
`param.h`, `systm.h`, then the rest — so `<sys/user.h>` would not compile without `<sys/dir.h>`
in front of it and `<sys/inode.h>` needed `NADDR` from somewhere else. Every one of them now
includes what it uses, which is the only form of the requirement a compiler checks, and the
only one that survives a formatter: clang-format sorts an include list alphabetically, which
puts `sys/dir.h` ahead of both headers it depends on. The kernel's sources used to carry their
include blocks inside a `// clang-format off` bracket for exactly that reason; none of them
does now. `<sys/param.h>` is the single exception to the "includes what it uses" rule, and it
proves it: it is `#define`-only so that `kernel/*.S` can include it too, emits no C text at
all, and so needs nothing to be order-insensitive. Its head comment says why the obvious
`#ifndef __ASSEMBLER__` escape is not available.

**Standing alone is not the same as composing, and there is exactly one pair that does not.**
`<sys/mount.h>` declares the kernel's table, `extern struct mount mount[NMOUNT]`; `<unistd.h>`
declares the system call, `int mount(const char *, const char *, int)`. They are the same
identifier in the same namespace, and `b6parse` refuses the second one it sees — *"Variable
mount redeclared with different type"*. It is v7's collision, not this port's, and nothing had
met it because nothing had ever wanted the mount *table* from user space: `cmd/mount` wants the
call, and the kernel wants the table and links no libc. Task C8's `pstat` wants both and is
the one source in the tree that cannot include `<unistd.h>`; it declares the three calls it
needs by hand, and [`../cmd/pstat/README.md`](../cmd/pstat/README.md) says why. **Neither
header may be changed to dodge it** — the `extern` is what kernel sources include
`<sys/mount.h>` for, and the prototype is what user sources include `<unistd.h>` for — so the
resolution belongs in the caller, and any future one should copy `pstat`'s.

**The errno numbering has one home, `<sys/errno.h>`.** v7 wrote it out twice — in `<errno.h>`
for the user and in `<sys/user.h>`'s `u_error codes` block for the kernel — and this port
inherited both. They had already drifted in the only way that matters here: `b6cpp` rejects a
macro redefinition unless the replacement text is *character*-identical, and clang-format's
`AlignConsecutiveMacros` had given `EDOM` and `ERANGE` different columns in the two files, so a
translation unit naming both headers did not compile. Now `<sys/errno.h>` holds the numbers and
nothing else — `#define`-only, so either side of the `KERNEL` gate may include it — `<errno.h>`
adds the `errno` object C11 §7.5 wants and includes it, and `<sys/user.h>` reads the same file
the kernel does. The one remaining copy is not a header: `guest_errno()` in
[`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp), which maps a *host* errno onto these
numbers.

**The signal numbering has one home too, `<sys/signal.h>`**, on that precedent. v7 wrote it out
twice as well — in `<signal.h>` for the user and in `<sys/param.h>` for the kernel — and here the
two copies agreed character for character, so `b6cpp` had no complaint and nine of the macros
were simply written twice. The other seven were worse than duplicates: they were a second *name*
for a number that already had one, v7's kernel spellings `SIGINS`, `SIGTRC`, `SIGFPT`, `SIGKIL`,
`SIGSEG`, `SIGCLK` and `SIGTRM` beside the standard `SIGILL`, `SIGTRAP`, `SIGFPE`, `SIGKILL`,
`SIGSEGV`, `SIGALRM` and `SIGTERM`. Those seven are **gone rather than aliased**: the kernel now
names a signal by the same spelling the user does, so no signal in this tree has two names.
`SIGFPE` is worth one line of its own, because it was inert for a long time and no longer is:
the kernel raises it now on a floating overflow or divide by zero (`kernel/trap.c`, task C18),
and `/bin/units` is the one program on the image that installs a handler for it.
`<sys/signal.h>` holds `NSIG`, the fifteen numbers and `SIG_DFL`/`SIG_IGN`/`SIG_ERR` and nothing
else — `#define`-only, like `<sys/errno.h>` — while `<signal.h>` adds `sig_atomic_t` and the
prototypes and `<sys/user.h>` takes `NSIG` from the same file `kernel/sig.c` does. The one
remaining copy is again not a header: the numbers b6sim answers `signal(2)` with, in
[`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp).

**And the terminal interface has one home, `<sys/ttyio.h>`** — the last pair of this kind, and
the largest: `<sgtty.h>` and `<sys/tty.h>` wrote out the same sixteen mode flags, the same thirty
`ioctl` command numbers, and the same two structures *under a second set of member names apiece*
(`struct ttiocb` for `sgttyb`, `struct tc` for `tchars`). Three separate things stopped the two
compiling together, and v7 itself is the source of the first two. `XTABS` was `06000` in one and
`006000` in the other, a token difference and not just spacing. `('t' << 8) | 16` had two *names*
— `TIOCTSTP` in `<sgtty.h>`, `TIOCFLUSH` in `<sys/tty.h>` — and only `TIOCFLUSH` survives, the
one the kernel implements and `tty(4)` documents, on the same "gone rather than aliased" rule the
signal spellings got. The third left no diagnostic to read at all: `<sys/tty.h>`'s accessor macros
`t_intrc`…`t_brkc` rewrote the *member declarations* of `<sgtty.h>`'s `struct tchars`, whose
members are spelled exactly that. Now `<sys/ttyio.h>` holds the structures and the numbers and no
prototypes, so either side of the `KERNEL` gate may include it; `<sgtty.h>` adds the three gates
`ioctl`/`stty`/`gtty` and nothing else, and no kernel source names it — those three are also the
names of the kernel's own handlers, in `<sys/systm.h>`, and it is `<sys/tty.h>` the kernel
includes; and `<sys/tty.h>` keeps the `clist`, the `tty` and the driver interface. What is
left elsewhere is not a copy of the numbering — b6sim answers every `ioctl` with a bare success —
but it does hard-code the *shape*: `gtty` there zeroes five words, which is `struct sgttyb`
([`../cmd/sim/syscall.cpp`](../cmd/sim/syscall.cpp)). Both headers are in
[`../lib/test/headers.c`](../lib/test/headers.c) now, which is the only thing that keeps them
honest.

**The dead v7 headers have been pruned**, and two rules say what may come back. A file format
this toolchain has already replaced is described *once*, under `cross/besm6/` — so `a.out.h`
and `ar.h` went, because `b.out.h` and the 30-char `ar_hdr` are the real ones and a second,
contradicting copy on the default include path is worse than none. A header for hardware this
machine does not have, or for a utility nobody has ported, gets re-imported from the v7 sources
*when that work happens* and refitted then — it is not kept as a stub in the meantime. That
took out the PDP-11 and VAX addresses (`core.h`, `execargs.h`, `saio.h`), the Datakit and uucp
`pk` driver (`dk.h`, `pack.h`, `sys/prim.h`), DECtape `tp` (`tp_defs.h`), the two dump formats
(`olddump.h`, `dumprestor.h`), `libmp` (`mp.h`), a stray `symbol.h`, and the two identity
placeholders (`ident.h` said `research 11/70`, `whoami.h` said `where I am`). Several of them
would not even compile. `math.h` used to be on this list; it is backed now, by `lib/libm`
(phase 7), and so are `curses.h` and `unctrl.h`, by
[`../lib/libcurses/`](../lib/libcurses/). **`curses.h` was replaced rather than kept**: what
stood here was v7's `1.7 (4/17/81)` and the sources that landed are 4.3BSD's, which differ in
the shape of `struct _win_st`, in every window flag bit, and in whether the tty-mode macros go
through `stty()` or `ioctl()`. Nothing had ever compiled against the old one, so nothing broke;
the account is in [`../lib/libcurses/README.md`](../lib/libcurses/README.md).

**`dirent.h` is the other header this repo added and v7 had none of**, and the one thing to know
before touching it is why it is not part of `<sys/dir.h>`. `<sys/dir.h>`'s `struct direct` is the
format **on the disk** — exactly four words, `_Static_assert`ed against `BSIZE` and `DIRPB`, with
`d_name` being `DIRSIZ` characters and **no room for a terminator** when a name fills the field.
`<dirent.h>`'s `struct dirent` is what a *program* wants: `d_name[DIRSIZ + 1]`, NUL-terminated by
`readdir()`. The two coexist in one translation unit by design — `lib/libc/gen/readdir.c` includes
both — so neither may be defined in terms of the other.

It is deliberately **not** under `sys/`. Thirty-odd kernel sources include `<sys/dir.h>` (and
`<sys/user.h>` pulls it in for `u_dent`), the kernel's header dependency is the whole of
`include/sys/` at once, and nothing kernel-side wants a user-space library declaration. And it
defines **no macro at all** — not `DIRSIZ`, which has one home in `<sys/param.h>` and is read from
there; `b6cpp` rejects a redefinition whose replacement text is not character-identical, and the
one way to be certain of that is to have nothing to redefine.

Adding it is what turned up the tree's **last order-dependent header pair**, which was already
there and had simply never been named: `<sys/param.h>` defines `HZ` as the clock rate and
4.xBSD's `<curses.h>` declared `extern bool HZ`, the Hazeltine capability. Any source naming both
failed, in either order — `<sys/dir.h>` had the same effect and nobody had put it before
`<curses.h>`. Nothing in `lib/libcurses` ever *read* the flag, so it is that library's private
`_HZ` now, on the `_PC` precedent already in `cr_tty.c`; `<curses.h>` says so where the name was.

**`term.h` is a header this repo added and v7 had none of.** termlib shipped as three `.c` files
and nothing else, so every caller wrote its own `char *tgetstr();` beside the call — which is what
the v7 `curses.h` did before it was replaced. It is here because
[`../lib/libtermcap/`](../lib/libtermcap/) backs it, and because on this machine a missed
`char *` return is a fat pointer truncated to a word address rather than a value that merely
looks wrong, so a prototype the definition is checked against is worth more than v7 fidelity.
It deliberately declares no `PC` and no `ospeed`: this `tputs` emits no padding, and the names
stay free for `lib/libcurses`, which defines both and reads neither.

The `a.out.h` rule looked as though it had cost something, and in the end it cost nothing.
`nlist()` is the one routine of `lib/`'s phase 5 that did **not** land, because a caller of it
needs `struct nlist` and there is no guest-visible spelling of the b.out format to give it.
`cross/besm6/b.out.h` is the real description and it is the toolchain's — `cross/` is not
installed, `b6cpp` predefines no `besm6`, and `<stdint.h>` here has no `int64_t`, so the native
branch of `cross/besm6/types.h` does not compile yet.

**That question is now closed, and not by answering it.** The three programs named as `nlist`'s
first callers wanted it to read `/unix`, and **there is no `/unix` on the root filesystem** —
`root.manifest` names no kernel image, the simulator loading one off the build host — so the
routine would have had nothing to open however its header was spelled. `<sys/kctl.h>` is what
went in instead: the kernel publishes a table of its own variables and `kctl(2)` reads it
([`../doc/Unix_V7_System_Calls.md`](../doc/Unix_V7_System_Calls.md) §2.5). It is a header of
this tree's own, in the "one home" family below, and the b.out format stays described once,
under `cross/besm6/`, where it belongs.

`<sys/kctl.h>` also follows the `#ifndef KERNEL` idiom that `<sys/stat.h>` and `<sys/times.h>`
established, and for their reason: the kernel's handler is `void kctl(void)`, declared in
`<sys/systm.h>`, and the two spellings must never both be in scope. `-DKERNEL` reaches every
kernel-side translation unit, `kernel/test/`'s programs included.

Types and macros follow the BESM-6 data model
([`../doc/Besm6_Data_Representation.md`](../doc/Besm6_Data_Representation.md)): every scalar
is one 48-bit word, `sizeof(int) == 6`, signed integers are 41-bit and unsigned 48-bit, and
`float` == `double` == `long double` with no infinities, NaNs or denormals. `size_t` is one
place this bites: the freestanding `<stddef.h>` makes it **signed** here — a deliberate
departure from C11 §7.19 — because unsigned arithmetic is a library call and no addressable
object needs the extra bit, so `<unistd.h>`'s `read`/`write` read as the plain POSIX
prototypes without cost.

`unistd.h` is a hosted header this tree adds (v7 predated it): the low-level I/O and process
primitives that `lib/libc/sys` backs, gathered from the per-file syscall declarations each
caller used to carry. `open`/`creat` are **not** in it — they live in `<fcntl.h>`, a hosted
header this tree also adds, alongside the three v7 `O_RDONLY`/`O_WRONLY`/`O_RDWR` modes (and
nothing more: this kernel has no `fcntl` system call, and `open()` honours no flag above
`O_RDWR`). `chmod`/`stat`/`fstat`/`mknod`/`umask` are not in either — they are in
`<sys/stat.h>`, beside `struct stat` and the mode bits they are about. `wait()` likewise has
`<sys/wait.h>`, a header this tree adds for the `W*` macros that take a v7 status word apart
(`kill()` is in `<signal.h>`, where C11 puts it). `utime()` **is** in `<unistd.h>`, with the
other file-system calls, and there is no `<utime.h>`: it takes a two-element `time_t` vector
rather than POSIX's `struct utimbuf`, because that is what the kernel `copyin`s
(`kernel/sys4.c`), and `lib/libc/man/utime.2` has said so since it was corrected. It had no
declaration anywhere until `cmd/mv` needed one.

**Not everything in it is a system call**, and that is the header's second job: `execlp`/`execvp`
are libc's own `$PATH`-searching forms, and task C6 added `crypt`, `setkey`, `encrypt`, `getpass`
and `ttyslot` beside them. Each of those had been declared by no header at all, so every caller
carried a prototype of its own — four copies of the same block, and C6's nine programs were about
to make it thirteen. The rule that decides such a case is **how many callers there are**: `getpw`,
`ecvt`/`gcvt`, `cfree`, `timezone` and `tell` are still the caller's to declare, because each has
one caller or none.

Those three `sys/` prototypes carry a guard the rest of the tree does not: `#ifndef KERNEL`.
`stat`, `chmod` and `wait` name *two* different functions in this repo — the libc leaf and the
kernel's system-call handler (`void stat(void)`, `<sys/systm.h>`) — and both sides include
`<sys/stat.h>` for the struct. `KERNEL` means **"this translation unit is kernel-side"**, not
"this object goes in the kernel image", which is why the standalone programs in `kernel/test/`
are compiled with it too: they link kernel objects and include `<sys/systm.h>`. The guard used
to carry a second condition, `!defined(_SYS_SYSTM_H)`, precisely because they were not — and
that was an include *order* requirement wearing another header's guard macro as a disguise,
which held only for as long as every kernel source happened to include `<sys/systm.h>` ahead
of `<sys/stat.h>`. `sys/stat.h` sorts first.
