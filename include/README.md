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
(a six-entry non-POSIX set, where ours is the kernel's list and is load-bearing in three places),
its `malloc.h` (the claim-everything allocator `lib/README.md` phase 3 rejects), and its KOI7
case folding (this terminal is ASCII).

`complex.h`, `stdatomic.h` and `threads.h` are **not** here and never will be: no complex type,
no atomic instructions, no threads. A hosted implementation owes the program that news, so
`b6cpp` predefines `__STDC_NO_COMPLEX__`, `__STDC_NO_ATOMICS__`, `__STDC_NO_THREADS__` and
`__STDC_NO_VLA__` ([`../cmd/cpp/cpp.c`](../cmd/cpp/cpp.c)).

`lib/test/headers.c` is what keeps all of this true: it includes every header in the tree twice
and checks the handful of behaviours that would otherwise fail silently.

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
would not even compile. What stays that is not yet backed — `math.h`, `curses.h`, `unctrl.h` —
stays because [`../lib/README.md`](../lib/README.md) names it in phase 7 and will rewrite it
for this machine's float format.

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
`float` == `double` == `long double` with no infinities, NaNs or denormals.
