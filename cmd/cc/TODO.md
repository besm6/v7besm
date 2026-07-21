# Retargeting `b6cc` onto this repo's own libc

A work plan, in the same spirit as [`../../kernel/TODO.md`](../../kernel/TODO.md) and
[`../../lib/README.md`](../../lib/README.md): it says what is to be done and in what order, and a
task is struck from this file as it lands, so what is written here is always the work that is
*left*. What a landed task taught is recorded in its commit, or — if the rest of the tree must
obey it — in [`README.md`](README.md) next door.

The driver itself barely changes. One line of [`cc.c`](cc.c) is wrong; everything else on this
list is the tree around it, which has been relying on files another project no longer ships.

## The problem

The external [c-compiler](https://github.com/besm6/c-compiler/) has stopped installing its Unix
`libc.a`, its `crt0.o`, and its C11 headers. It now installs exactly one thing for this
toolchain — **`libruntime.a`**, the `b$*` compiler-support helpers (`b$save`, `b$ret`, `b$mul`,
`b$div`, the relational and conversion routines) that every compiled function calls. Those are an
implementation detail of that back end and cannot come from anywhere else.

Everything else is ours now. **We ship the libc, the `crt0.o`, and the headers.** That is the
whole of the change, and this file is the consequence.

Nothing in this repo has caught up, so the toolchain is broken as it stands. Both halves fail
independently:

```console
$ b6cc t.c -o t.b6
b6cc: error: crt0.o not found in the standard library directories
      (~/.local or /usr/local share/besm6/lib); use -nostdlib to link without it

$ b6cc -c u.c                                   # u.c is `#include <stddef.h>'
b6cpp: u.c:2: error: Can't find include file stddef.h
```

`~/.local/share/besm6/` now holds `lib/libruntime.a`, `lib/libc.bin` and `lib/libbem.bin`, and no
`include/` at all. The second failure is the wider one: [`../../lib/libc/`](../../lib/libc/)
includes `<string.h>` seventeen times and `<stdlib.h>` five, and this repo's
[`../../include/`](../../include/) has neither — they were the compiler's. **Our own libc cannot
compile until task 1 lands.**

## Two things that got simpler, and one claim that was wrong

**The archive-name collision is gone.** [`../../lib/rules.mk`](../../lib/rules.mk) lines 44–54 and
[`../../lib/README.md`](../../lib/README.md) lines 200–207 carry a careful explanation of why the
external library had to be named by absolute *path* rather than with `-L… -lc`: both archives were
called `libc.a`, `b6ld` keeps one *global* list of `-L` directories and `-l` searches all of it
with first match wins ([`../ld/library.c`](../ld/library.c) line 40 onward), so a second `-lc`
found ours again and every `b$*` went undefined. With the helpers in `libruntime.a` the two names
no longer collide and a plain `-lc -lruntime` is unambiguous. **Delete that workaround and its
commentary — do not port it.**

**The undefined-symbol check gets better, not worse.** The same section documents relinking
*without* the external archive and reading the errors, since `b6nm` cannot tell whose `strcpy.o`
was pulled when both archives name their members alike. That ambiguity is gone too, but the check
is still the one that catches a routine we forgot to port, and it is now exact: drop `-lruntime`
and every undefined name must be a `b$*`. Keep it, reworded.

**The kernel never needed the strings.** [`../../lib/README.md`](../../lib/README.md) line 16 says
the kernel links the external `libc.a` "for its handful of string routines". It does not:
[`../../kernel/prf.c`](../../kernel/prf.c) defines the kernel's own `printf`, and no kernel source
references `str*`, `mem*`, `bcopy` or `bzero` at all. The kernel was reaching that archive for the
`b$*` helpers and nothing else, so its fix is a straight `-lc` → `-lruntime` — it acquires **no**
dependency on `lib/libc`, and there is no bootstrap cycle to worry about.

---

## Task 1 — the headers: one tree, and it is ours

Add to [`../../include/`](../../include/) the ANSI headers v7 never had, adapted from the
c-compiler's `libc/besm6/include/`. Required, because our own sources or the language need them:

> `stddef.h` · `stdarg.h` · `string.h` · `stdlib.h` · `limits.h` · `float.h` · `stdbool.h` ·
> `besm6.h`

Optional, the rest of the freestanding set, worth taking while the source is open:
`stdint.h` · `inttypes.h` · `iso646.h` · `stdalign.h` · `stdnoreturn.h`.

**Do not import the eight that overlap** — `assert.h`, `ctype.h`, `errno.h`, `math.h`, `setjmp.h`,
`signal.h`, `stdio.h`, `time.h`. Ours are the v7 ones and they stay; that is the whole reason this
repo has an `include/`.

`besm6.h` is the odd one and deserves its own note wherever it lands: it declares the nine
`__besm6_*` intrinsics that the back end lowers to single instructions (the c-compiler's
`docs/Besm6_Intrinsics.md` is the manual). It tracks the *compiler*, not v7 — a compiler that adds
a tenth intrinsic makes our copy stale, and nothing will say so.

Then install the tree, which nothing in this repo does today — there are no header install rules
at all. In the top-level [`../../CMakeLists.txt`](../../CMakeLists.txt):

```cmake
install(DIRECTORY include/ DESTINATION share/besm6/include
        FILES_MATCHING PATTERN "*.h")
```

`sys/` comes along with it, which is what we want: `<sys/syscall.h>` is named by every syscall
leaf under `lib/libc/sys/`.

Two consequences to write down as part of this task:

- [`../../lib/README.md`](../../lib/README.md) line 120, **"The v7 headers win"**, describes an
  ordering between two header directories that will no longer exist. Replace it with the simpler
  rule — there is one header directory and this repo owns it — and delete the corollary sentence
  about `<string.h>`, `<stdlib.h>` and `<stddef.h>` "coming from the compiler's tree".
- The `###` dependency block at the foot of [`../../lib/libc/Makefile`](../../lib/libc/Makefile)
  says in as many words that `<string.h>` and `<stdlib.h>` are *the compiler's* and are therefore
  deliberately **not** named as prerequisites, "they change with the toolchain, not with us".
  After this task they change with us, so they must be named there like every other header.

## Task 2 — install `libc.a` and `crt0.o`

Add an `install` target to [`../../lib/libc/Makefile`](../../lib/libc/Makefile), and a passthrough
in [`../../lib/Makefile`](../../lib/Makefile), copying `libc.a` and `crt0.o` into
`$(DESTDIR)$(PREFIX)/share/besm6/lib` — beside the `libruntime.a` the c-compiler already puts
there. `PREFIX` defaults by the rule the top-level [`../../Makefile`](../../Makefile) already
uses: `~/.local` if that directory exists, else `/usr/local`.

It cannot be a CMake install. `lib/` is built by the *installed* toolchain and is deliberately
outside the CMake build ([`../../lib/rules.mk`](../../lib/rules.mk) line 13) — `b6cc` has to exist
before `libc.a` can be compiled at all.

That makes the first build of a fresh checkout a three-step cycle, which this task must state
plainly somewhere a newcomer will find it (the top-level [`../../README.md`](../../README.md) and
[`../../CLAUDE.md`](../../CLAUDE.md)):

```sh
make && make install        # 1. host tools -- b6cc, b6as, b6ld ... -- and include/
make -C lib                 # 2. libc.a and crt0.o, built by those tools
make -C lib install         # 3. into share/besm6/lib, beside libruntime.a
```

Step 2 needs step 1's headers, and step 3 is what makes `b6cc` able to link. Until all three have
run, `b6cc` can compile and assemble but not link — which is exactly the state the tree is in
today, and why the driver's own test suite stayed green through the breakage (task 5).

## Task 3 — `cc.c`: name the second archive

The one code change in the driver. In `link_objects()` ([`cc.c`](cc.c) line 566), the implicit
library is pushed as a single `-lc` at line 605. It becomes two, under the same `!opt_nostdlib`
guard:

```c
    // The implicit C library, then the compiler's helper archive, unless
    // -nostdlib asked for a freestanding link.  libruntime.a is LAST: libc
    // calls the b$* helpers and no helper calls back into libc.
    if (!opt_nostdlib) {
        vec_push(&av, "-lc");
        vec_push(&av, "-lruntime");
    }
```

The order is not cosmetic and the comment must say why, because a traditional linker scans
archives once, in order. It is the same order the c-compiler's own harness uses.

`besm6_include_dir()` (line 275), `find_crt0()` (line 301) and `add_default_libdirs()` (line 321)
need **no change whatever** — their paths are already right, and become right in fact once tasks 1
and 2 put files there. Say so, so that nobody reads the current failures as a bug in them and
"fixes" the search order.

One diagnostic is worth improving while here. `find_crt0()`'s failure message (line 589) offers
`-nostdlib`, which is the wrong advice in the overwhelmingly common case: the cause is not that
the user wants a freestanding link, it is that `make -C lib install` has never been run. Point at
the bootstrap sequence instead, and keep the `-nostdlib` mention as the second clause.

## Task 4 — the stale link lines

Mechanical, roughly one line each, and collectively they are what unbreaks the tree:

| File | Change |
| --- | --- |
| [`../../lib/rules.mk`](../../lib/rules.mk) | drop `BLIB` (line 36); the `link` recipe (line 54) becomes `… -L$(LIBC) -lc -lruntime`; delete the by-path commentary at lines 44–54 |
| [`../../lib/README.md`](../../lib/README.md) | the same for the two documented link lines (183, 196) and the "named by **path**" section (202 onward); fix the string-routines claim at line 16 |
| [`../../kernel/Makefile`](../../kernel/Makefile) | line 36, `-lc` → `-lruntime` |
| [`../../kernel/test/Makefile`](../../kernel/test/Makefile) | line 49, the same; `LIBDIR` at line 15 is still correct |
| [`../../CLAUDE.md`](../../CLAUDE.md) | line 70, "links against the external c-compiler's libc" → `libruntime.a`, the helpers only |

The kernel lines take `-lruntime` alone. They must **not** grow a `-lc`: the kernel supplies its
own `printf` and uses no other library routine, and linking it against a user-mode libc would be
wrong even where it happened to work.

## Task 5 — README and a test that would have caught this

[`README.md`](README.md) §Linking (lines 69–80) still describes one implicit archive and locates
`crt0.o` under the c-compiler's prefix. Rewrite it for the two archives and the new owner, and
extend the `-nostdlib` row of the options table (line 52) to mention `-lruntime`.

[`test/cc_test.cpp`](test/cc_test.cpp) exercises `-S` and the `.S` path and nothing else. **No
test links**, which is precisely why the suite stayed green while `b6cc` could not produce an
executable at all. Add one that does:

- guard it on `crt0.o` and `libc.a` being present in the standard library directories, skipping
  with `GTEST_SKIP()` otherwise — the file already uses that idiom four times over for the
  c-compiler passes, so follow it rather than inventing a second convention;
- assert the link succeeds, and that the `b6ld` command line carries `-lc` before `-lruntime`.
  Capturing `b6cc -v` output is the cheap way to assert the order, and it keeps the assertion
  meaningful even on a machine where the libc is installed but stale.

---

## Done when

A fresh checkout, following the three-step bootstrap of task 2, can do this:

```sh
echo 'int main(void) { return 0; }' > t.c
b6cc t.c -o t.b6 && b6sim t.b6
```

and `cd lib && make test` runs the whole `b6sim` harness against a `libc.a` built with headers
from this repo alone. Nothing under `include/`, `lib/` or `kernel/` should then name a file the
c-compiler installs, other than `libruntime.a`.
