# The system header tree

The headers a BESM-6 program compiles against — the Unix v7 ones (`sys/` included), plus the
hosted C11 headers v7 never had. The kernel and `lib/libc` reach them as `-I../include`, and
the top-level `make install` copies the tree to `<prefix>/share/besm6/include`, which `b6cc`
appends to every preprocessor run. So a source with no `-I` of its own still finds
`<string.h>`, and one built in the tree gets the same files it would after installation.

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
  passing the signal number — see `kernel/TODO.md`.

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
numbers. `<sgtty.h>` and `<sys/tty.h>` are still a pair of this kind, and worse — `XTABS` is
`06000` in one and `006000` in the other, a token difference and not just spacing — but nothing
includes both.

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

**`term.h` is a header this repo added and v7 had none of.** termlib shipped as three `.c` files
and nothing else, so every caller wrote its own `char *tgetstr();` beside the call — which is what
the v7 `curses.h` did before it was replaced. It is here because
[`../lib/libtermcap/`](../lib/libtermcap/) backs it, and because on this machine a missed
`char *` return is a fat pointer truncated to a word address rather than a value that merely
looks wrong, so a prototype the definition is checked against is worth more than v7 fidelity.
It deliberately declares no `PC` and no `ospeed`: this `tputs` emits no padding, and the names
stay free for `lib/libcurses`, which defines both and reads neither.

The `a.out.h` rule has already cost something, and it was worth it: `nlist()` is the one routine
of `lib/`'s phase 5 that did **not** land, because a caller of it needs `struct nlist` and there
is no guest-visible spelling of the b.out format to give it. `cross/besm6/b.out.h` is the real
description and it is the toolchain's — `cross/` is not installed, `b6cpp` predefines no `besm6`,
and `<stdint.h>` here has no `int64_t`, so the native branch of `cross/besm6/types.h` does not
compile yet. Nothing in `lib/` calls `nlist`; the first program that does — `nm`, `ps`, `pstat` —
is what should settle whether the cross headers become reachable from guest code or a guest
description is written beside them.

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
