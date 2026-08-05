# libm — the v7 math library on the BESM-6 float format

`libm.a`: `sqrt`, `pow`, `exp`, `log`/`log10`, the trigonometric and hyperbolic families,
`erf`/`erfc` and the Bessel functions `j0`/`y0`/`j1`/`y1`/`jn`/`yn`, plus the C11 routines v7
never had — `trunc`, `round`, `copysign`, `fmin`, `fmax`, `fmod` and an exact `fma`. Twenty-four
sources, **1,884 words** of object code, declared by [`../../include/math.h`](../../include/math.h).

The algorithms are v7's, and they are Hart & Cheney rational fits — each named by its number in
the file that uses it (`#1069` in [`exp.c`](exp.c), `#2705` in [`log.c`](log.c), `#3370` in
[`sin.c`](sin.c), `#5077` in [`atan.c`](atan.c), `#5667` in [`erf.c`](erf.c), four apiece in
[`j0.c`](j0.c) and [`j1.c`](j1.c)). Seventeen files carry the `UNIX V7 source code:` provenance
line. The seven that do not — `trunc`, `round`, `copysign`, `fmin`, `fmax`, `fma`, `fmod` — have
no v7 ancestor and each says so at its head.

`j1.o` (298 words) and `j0.o` (279) are the two heaviest and the only members with any bss;
`trunc.o` is six words. One function family per file, so `b6ranlib`'s index lets a program that
calls `sqrt` pay for `sqrt`.

**Its only caller today is its own test.** [`../libtermcap/README.md`](../libtermcap/README.md)
could name libcurses as its first consumer; this library has none yet — no `cmd/` program links
`-lm`, and inside `lib/` only [`../test/matht.c`](../test/matht.c) and
[`../test/headers.c`](../test/headers.c) include `<math.h>` for anything defined here.

## Building and linking

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/lib/libm/libm.a among everything else
make install    # puts it in share/besm6/lib beside libc.a, libtermcap.a and libruntime.a
make run        # runs matht twice -- see Testing
```

**Link order is a contract, not a style:** `-lm` comes **before** `-lc`.

```sh
b6ld crt0.o prog.o -lm -lc -lruntime -o prog
```

`b6ld` scans each archive exactly once, in order. libm calls `errno`, `frexp`, `ldexp` and `modf`
in libc, and libc calls nothing back — the same one-scan reasoning
[`../README.md`](../README.md) gives for `-ltermcap`. `b6cc` needs no help: it places a user
`-l` ahead of its implicit `-lc -lruntime`, so `b6cc prog.c -lm` is already right. In the CMake
build, `b6_libtest(matht libm)` does it — and absorbing that job is why `b6_libtest()` took over
`b6_prog()`'s size check, since `b6_prog()` is hard-wired to `-lc -lruntime` and cannot name a
third archive.

## The format, and the one thing that follows from it

One 48-bit floating type: a **40-bit two's-complement mantissa**, a 7-bit exponent biased by 64
reaching **2^±63**, and **no infinities, no NaNs, no denormals and no negative zero**
([`../../doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md) §6).
`float`, `double` and `long double` are the same one word, which is why `<math.h>` declares no
`f`- or `l`-suffixed variants and `<tgmath.h>` collapses to a set of self-referential macros.

**Overflow is a fault; underflow is a silent zero, and the two are not symmetric.** An exponent
that rises past 2^63 raises `MSG_ARITH_OVERFLOW` and the program dies
([`../../cmd/sim/arithmetic.cpp`](../../cmd/sim/arithmetic.cpp), and the real machine does the
same). One that falls below 2^−63 quietly becomes machine zero.

So **`HUGE_VAL` is a value a routine *returns*, never one it *computes***: every range gate is
placed **before** the arithmetic that would overflow, and is checked strictly. That sentence is
the whole content of this port. The reason it is not merely a style rule is that a gate one ulp
too loose does not produce a wrong number — it produces no number at all, and the program is
gone.

`HUGE_VAL` is `DBL_MAX` — 9.223×10^18 — and it is not a distinguished value; nothing can test a
result to find out that it saturated. v7's `HUGE` was 1.701×10^38, the PDP-11's largest float,
which cannot be written as a literal here at all; the name is kept as an alias carrying this
machine's value, because the v7 sources use it. `LOGHUGE` is 19 where v7's was 39.

### `exp` is the worked example

v7 tested against a single `maxf` of `DBL_MAX_10_EXP * 2.5` = 45. But `exp(45)` is ≈3.5×10^19,
past the largest finite 9.223×10^18, and **computing it faulted**. It is two gates now, and each
sits before the arithmetic:

| End | Gate | Why there |
| --- | --- | --- |
| overflow | `x > 43.6` | `ln(DBL_MAX)` is 43.668; 43.6 is the last tenth that lands `ent` at 62 and keeps the closing `ldexp` in range. `ERANGE`, `HUGE_VAL`. |
| underflow | `x < -44.3` | `ln(DBL_MIN)` is −44.36; at −44.3 the result has already flushed to machine zero. No `errno` — an underflow to zero is what the hardware does to any small value anyway. |

**The lower gate is not cosmetic.** `ldexp` is six instructions
([`../libc/gen/ldexp.s`](../libc/gen/ldexp.s)) and does no range check whatever: it ORs the
biased exponent into the field and multiplies, so it keeps only the low seven bits of its
argument. Without the gate, `ent` runs off the bottom, wraps to a huge exponent, and **a tiny
`x` comes back enormous**.

`sinh` and `cosh` reach the same ceiling through `exp`, and the spelling of their large branch is
chosen for it: v7 writes `exp(x)/2`, which would return a saturated `HUGE_VAL/2` for `x` in
(43.6, 44.3] where the true `sinh` is still perfectly representable. Written `exp(x - ln2)` —
identical arithmetic — `exp`'s gate moves out to 44.3 = `ln(2·DBL_MAX)`, exactly where `sinh`
itself overflows, and the ERANGE is raised at the right place.

`hypot` is the one routine whose care v7 already took for the same reason, and it is unchanged:
squaring both arguments overflows for any |a| above 2^31.5 even when the answer is representable,
so it divides by the larger first and only the closing multiply can reach the top of the range.

## The PDP-11's magic numbers were widths, and had to be rederived

None of these is a mathematical constant. Each is a statement about a machine, and the machine
changed.

| Where | v7 wrote | Now | What the number was |
| --- | --- | --- | --- |
| `sinh.c`, `tanh.c` | `21.` | `14` | where `exp(-x)` stops contributing to a **56-bit** mantissa; here 40 bits, so `20·ln2 ≈ 13.86` |
| `sin.c` | `32764` | `two40` | the last quarter-turn below **2^15**, past which a PDP-11 `int` overflowed; an `int` is 41 bits here and the real limit is the mantissa's 2^40 — the same constant `modf` keys on |
| `sqrt.c` | a `1L<<30` scaling loop | `ldexp` | an `ldexp` written out because the PDP-11's library had none; ours is in the library, is a handful of instructions, and is exact |
| `exp.c` | `DBL_MAX_10_EXP*2.5` | 43.6 / −44.3 | see above |
| `tan.c` (BUGS) | "garbage above 2^31" | 2^40 | its own integer width speaking |

## The sign is not a bit, and there is nothing to classify

The mantissa is **two's complement**, so a number's sign is not a separable field — negating
rewrites the whole mantissa — and there is no negative zero.

- **`fabs`** needs no mask, and has no comparison that could get `-0.0` wrong.
- **`copysign`** *cannot* be the usual bit graft and does not need to be: `y < 0` answers the
  question completely. On an IEEE machine it would not — `copysign(1, -0.0)` must be −1 there.
- **`sqrt`**'s exponent halving works because `dexp & 1` answers for a negative `dexp` too; v7's
  comment warns that it would not on a ones-complement machine.
- **`fmin`/`fmax`** are the comparison and nothing else. C11 asks them to treat a NaN operand as
  missing data and return the other one; with no NaNs there is nothing to treat.

The same absence is why `<math.h>` defines no `INFINITY`, no `NAN`, no `FP_*` numbers, no
`fpclassify`/`isnan`/`isinf`/`isfinite`/`isnormal`, no `signbit` and no `math_errhandling` — and
why `<fenv.h>` is degenerate, `FE_ALL_EXCEPT` being 0.

## Three routines are not v7's

### `fmod` was thrown away and rewritten

v7's libm had no `fmod`. What this tree inherited was an **fdlibm IEEE import**: it takes a
`double` apart through a union of `uint32_t` halves and reassembles it. Every line of it is about
a format this machine does not have — no 32-bit word to alias a `double` onto, no sign-magnitude
sign bit, no biased-by-1023 exponent field at bit 20 of a high half, no subnormals to
renormalize.

It is the classic scale-and-subtract loop instead, which needs no bit access at all — only
`frexp` and `ldexp`, both exact here. It is exact by **Sterbenz's lemma**: each step subtracts the
largest `y·2^n` not exceeding the running value, and if `t ≤ a ≤ 2t` then `a - t` is representable
with no rounding whatever, so no bit is ever lost. It terminates in at most 127 steps, one per
exponent.

That is what `x - y*trunc(x/y)` cannot do: that form has already lost the low bits of the
quotient before the multiply, and for `x/y` much above 2^40 it has lost all of them.
[`../test/matht.c`](../test/matht.c) pins it on `fmod(2^42, 3)` and `fmod(2^42, 5)`, where 2^42 is
exactly representable but 2^42/3 is not.

### `fma` is exact, and its split is not Dekker's

The external c-compiler's own libc has an `fma.c`, and it gives up: *"BESM-6 carries no extra
internal precision, so this is an ordinary `x*y + z` with two roundings."* This one does not.

There is no wider type to compute in. What makes the routine possible anyway is that **the
mantissa is 40 bits and 40 halves evenly**: split each operand into two 20-bit pieces and all
four cross products are 20×20 = 40-bit values, every one exactly representable, so `x*y` is
recovered as a sum `p + e` of two machine numbers with nothing dropped — and `z` is added to
*that*, rather than to a product whose low half has already been rounded away. A caller who
wanted `x*y + z` could have written `x*y + z`.

**The split is not the textbook one.** Dekker multiplies by `2^20 + 1` and relies on the multiply
rounding to nearest to place the boundary; this machine rounds by **forcing the low mantissa bit**
when anything is discarded (the `round_flag` arm of `Processor::arith_normalize_and_round`,
[`../../cmd/sim/arithmetic.cpp`](../../cmd/sim/arithmetic.cpp)), which is faithful but not
nearest, and the theorem does not survive it. The split here assumes nothing about rounding at
all: `ldexp` moves the binary point, `modf` cuts at it, both exact.

The operands are scaled into [0.5,1) before the split rather than cut where they lie, because the
split shifts left by 20 and an unscaled operand above 2^43 would run off the top of the exponent
range — **and running off the top is a fault here, not an infinity.**

What is and is not promised: the product is exact, and its low half reaches the addition instead
of being rounded away before it. The residual accumulation and the closing sum are *faithful*
rather than provably single-rounded, for the same reason the split could not be Dekker's.

### The C11 handful

`trunc`, `round`, `copysign`, `fmin`, `fmax` — a few lines each. Only `round` is worth a note: it
is written over `modf` and deliberately **not** as `floor(x + 0.5)`, which is wrong twice. That
shortcut rounds halfway cases toward +∞ rather than away from zero, and for an `x` just below 0.5
the addition itself rounds up to 1.0 and the answer comes back 1 instead of 0.

## j0/j1's leading coefficients did not fit, so they are scaled

Both Bessel fits open with values near 5×10^20 … 5×10^23. The largest finite value here is
9.223×10^18, so **they cannot be written as literals at all** — this is not a precision question
but a lexical one.

Each numerator/denominator **pair** is divided by a power of ten, which is a pure shift of every
literal's decimal exponent and leaves the ratio `n/d` — the only thing either polynomial
contributes — unchanged. The factor is recorded on each array's first line.

| File | Pair | Divided by |
| --- | --- | --- |
| `j0.c` | `p1`/`q1`, `p4`/`q4` | 10^10 |
| `j1.c` | `p1`/`q1` | 10^11 |
| `j1.c` | `p4`/`q4` | 10^13 |

`p2`/`q2` and `p3`/`q3` already fit in both files and are v7's outright. The Horner loops of
`erf`, `j0` and `j1` needed no unrolling: `b6cc` emits an initialized file-scope `double` array to
the constant pool.

## `long` is `int`, and it bit `pow`

`pow` decides whether a negative base has a real power by rounding the exponent to a `long` and
comparing it back. `long` *is* `int` here — one word, 41 bits signed — while `arg2` ranges to
2^63, so **the conversion itself is undefined for a large exponent**. It is gated by
`fabs(arg2) < two40` first; above 2^40 every value is already integral, a 40-bit mantissa having
no fractional bits left, and the parity that decides the sign is taken from the low bit.

Two smaller collisions with the front end, both mechanical and both worth knowing before editing
anything nearby:

- **`sqrt.c` and `log.c` rename a local `exp` to `dexp`.** There is one namespace for objects and
  functions, and `<math.h>` has declared `exp()`. [`../libc/gen/atof.c`](../libc/gen/atof.c) had
  already done the same.
- **`atan.c`'s first line was `double static sq2p1`**, which the parser rejects — the storage
  class has to precede the type.

## `frexp`, `ldexp` and `modf` are in libc, not here

They are the exponent surgery the **conversions** need — `atof`, `ecvt` and the printf engine —
so they live in [`../libc/gen/`](../libc/gen/), where v7 keeps them too (`modf.s`, `frexp.s`,
`ldexp.s` in its `gen/`). A program gets them without `-lm`; `<math.h>` declares them under a
banner saying so.

They are small and exact, which is what several of libm's decisions lean on:
[`ldexp.s`](../libc/gen/ldexp.s) is **six instructions** and [`frexp.s`](../libc/gen/frexp.s)
twelve, both b6as ports of the external compiler's Madlen originals; [`modf.c`](../libc/gen/modf.c)
is C and owns the `two40` constant that `sin` and `pow` key on.

`ldexp`'s six instructions include **no range check** — `aex #0100` and a shift, then a multiply —
which is precisely what `exp`'s lower gate stands between a caller and.

## What is absent

- **`gamma`.** The one routine of v7's libm whose source never reached this tree. Nothing here
  calls it, it is not C11, and `<tgmath.h>` does not name it. It comes back with the first program
  that wants it, and the choice between v7's log-gamma-plus-`signgam` and C11's `tgamma`/`lgamma`
  gets made then.
- **`cabs`** and the `struct complex` it took, which came in `hypot.c` wrapped in an `#if 0`.
  There is no `<complex.h>` here — `__STDC_NO_COMPLEX__`, see [`../../cmd/cpp/cpp.c`](../../cmd/cpp/cpp.c).
- **`float sqrtf(float)`**, which the inherited `sqrt.c` carried. One floating type, no suffixed
  variants.
- **Much of C11 §7.12**: `exp2`, `expm1`, `log1p`, `log2`, `logb`, `ilogb`, `scalbn`, `cbrt`,
  `asinh`/`acosh`/`atanh`, `tgamma`/`lgamma`, `nearbyint`/`rint`/`lrint`/`lround`,
  `remainder`/`remquo`, `nextafter`/`nan`, `fdim`. None is declared, so a caller finds out at
  compile time. `<tgmath.h>` names a strict subset of what *is* here — no `fma`, no `erf`, no
  Bessel.

The reverse case, for the record: **`erf`/`erfc` gained declarations.** The sources were always in
the tree and v7's own `<math.h>` declared both; ours silently did not.

## Testing

One program, [`../test/matht.c`](../test/matht.c), and **it runs twice** — under `b6sim` (ctest
`lib_matht`, label `lib`) and off the disk image under the booted kernel (ctest `libtest_matht`,
label `kernel`), both diffed against the same
[`../test/matht.expected`](../test/matht.expected). Under `b6sim` every system call is the host's,
so a kernel bug cannot show; the two disagreeing means one of the harnesses is wrong. Both runs
are of **one linked image**, staged onto the disk by a copy, so a difference between them can only
be the harness and never the compiler.

Two things about its design are the point, and both are deliberate departures from what a math
test usually looks like.

**The expectation is 65 `ok` lines and a closing error count — not one computed value.** Pinning
`sin(0.7)` to twelve
printed digits pins the last-bit rounding of a polynomial, and breaks on a codegen change that is
not a bug. So each function is checked **against another** — `sin²+cos² = 1`, `exp(log x) = x`,
`tanh = sinh/cosh`, `cosh²−sinh² = 1`, `erf+erfc = 1`, `atan2` in all four quadrants, the
three-term Bessel recurrence — with a mixed tolerance of `1e-9·|want| + 1e-11` against a machine
epsilon of ~1.8×10^−12, three decimal digits of headroom. A real error moves these by orders of
magnitude; a rounding difference does not move them at all.

Two families are checked **exactly**, because they promise exactness. `fmod` against the true
remainder on values a naive `x - y*trunc(x/y)` gets wrong. And `fma` twice over: once for its
value, and once as a **negative** assertion — `fma(x, x, -1)` must *differ* from `x*x - 1`, and
that difference is the bit the routine exists to preserve. `jn(0,x) == j0(x)` and `jn(1,x) ==
j1(x)` are exact too, being literal delegations.

**The range block is the port's real content.** Every routine that can reach the top of the range
is driven just below its limit (finite, `errno` untouched) and just past it (`HUGE_VAL` and
`ERANGE`), and every domain error is exercised. As the test's own header puts it: *if any guard is
wrong this program does not print a wrong answer; it dies, and the missing `ok` lines say where.*
That is the failure mode a table of digits could not have caught.

A second test touches the header rather than the code: [`../test/headers.c`](../test/headers.c)
checks that `HUGE_VAL` is this machine's largest finite value and that `LOGHUGE` is 19, and it
includes `<tgmath.h>` **last and alone**, because every one of that header's macros shadows a
`<math.h>` function name.

## The manual pages

Six, and they are v7's own — [`exp.3m.umm`](exp.3m.umm), [`floor.3m.umm`](floor.3m.umm), [`hypot.3m.umm`](hypot.3m.umm),
[`j0.3m.umm`](j0.3m.umm), [`sin.3m.umm`](sin.3m.umm), [`sinh.3m.umm`](sinh.3m.umm) — **corrected in place** on the
precedent [`../libtermcap/termcap.3.umm`](../libtermcap/termcap.3.umm) set. Each correction is marked
**Note:** and says what v7 did as well as what happens now; the SYNOPSIS of each is ANSI, since
`b6parse` accepts nothing else. What they had to be told, mostly: that `log(0)` returns
−`HUGE_VAL` and not 0, that an overflow is a fault and not an infinity, that the thresholds moved,
that `cabs` is gone, and that `floor.3m.umm` now owns seven C11 routines v7 had none of.

Nothing installs them — no `CMakeLists.txt` in this tree has a man rule yet — so they are read
with `nroff -man exp.3m.umm`.
