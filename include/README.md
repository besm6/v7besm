# The system header tree

The headers a BESM-6 program compiles against — the Unix v7 ones (`sys/` included), plus the
C11 headers v7 never had. There is exactly **one** such tree and it is this one: the kernel
and `lib/libc` reach it as `-I../include`, and the top-level `make install` copies it to
`<prefix>/share/besm6/include`, which `b6cc` appends to every preprocessor run. So a source
with no `-I` of its own still finds `<stdarg.h>`, and one built in the tree gets the same
files it would after installation.

The C11 set was adapted from the external
[c-compiler](https://github.com/besm6/c-compiler/)'s `libc/besm6/include/`, which no longer
installs it: `stddef.h`, `stdarg.h`, `string.h`, `stdlib.h`, `limits.h`, `float.h`,
`stdbool.h`, `stdint.h`, `inttypes.h`, `iso646.h`, `stdalign.h`, `stdnoreturn.h`. Where the
two trees overlapped — `assert.h`, `ctype.h`, `errno.h`, `math.h`, `setjmp.h`, `signal.h`,
`stdio.h`, `time.h` — the **v7** header stays; that is the whole reason this directory exists.

`besm6.h` is the odd one out: it declares the nine `__besm6_*` intrinsics that the compiler
lowers to single instructions ([`../doc/Intrinsics.md`](../doc/Intrinsics.md)), so it tracks
the *compiler*, not v7. Re-copy it when the back end's intrinsic set changes; nothing else
will say that it has gone stale.

Types and macros follow the BESM-6 data model
([`../doc/Besm6_Data_Representation.md`](../doc/Besm6_Data_Representation.md)): every scalar
is one 48-bit word, `sizeof(int) == 6`, signed integers are 41-bit and unsigned 48-bit, and
`float` == `double` == `long double` with no infinities, NaNs or denormals.
