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
the v7 headers, plus `string.h`, `stdlib.h` and `inttypes.h` adapted from the compiler's tree
(it ships those but does not install them). Where the two trees overlapped — `assert.h`,
`ctype.h`, `errno.h`, `math.h`, `setjmp.h`, `signal.h`, `stdio.h`, `time.h` — the **v7**
header stays; that is the whole reason this directory exists.

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

Types and macros follow the BESM-6 data model
([`../doc/Besm6_Data_Representation.md`](../doc/Besm6_Data_Representation.md)): every scalar
is one 48-bit word, `sizeof(int) == 6`, signed integers are 41-bit and unsigned 48-bit, and
`float` == `double` == `long double` with no infinities, NaNs or denormals.
