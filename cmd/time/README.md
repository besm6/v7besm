# `time`, and what a tick is worth here

Task C2b. The C11 pass over [time.c](time.c) is in its own header; this file is about the two
things the port had to *decide* rather than translate, and about the shape of the test, which
is different from every other program in C1–C2.

## The whole program is built on 60

v7's `time` never mentions `HZ`. It writes the number out twice instead, in two places that do
not look related:

```c
printt("real", (after-before) * 60);
char quant[] = { 6, 10, 10, 6, 10, 6, 10, 10, 10 };
```

The first converts a difference of two `time(2)` results — seconds — into the same unit
`times(2)` reports in. The second is a radix table that decodes that unit back into printable
fields, least significant first, and its leading `6` is doing one job only: turning **60ths of
a second into tenths**. Everything after it is 10/10 for seconds, 6/10 for minutes and three
10s of hours, and none of that depends on the clock rate.

**HZ is 250 here** ([`<sys/param.h>`](../../include/sys/param.h), and `CLOCKS_PER_SEC` in
[`<time.h>`](../../include/time.h); [`times.2`](../../lib/libc/man/times.2.umm) says so outright).
So both constants are wrong, and wrong in different directions: the multiplier makes `real`
come out four times short, and the radix makes every printed field wrong from the tenths
upward. Ported as

```c
static const int quant[9] = { CLOCKS_PER_SEC / 10, 10, 10, 6, 10, 6, 10, 10, 10 };
```

with a `_Static_assert` that the tick count divides into whole tenths, and `* CLOCKS_PER_SEC`
in place of `* 60`. The printed layout is byte-for-byte v7's — the sub-tenth digit is still
computed and then discarded, exactly as v7 discarded its 60ths.

**The reusable lesson is that a magic number can be spread across two expressions that share
no variable.** Grepping for `60` finds them; grepping for `HZ` finds neither, and reading
`printt` alone gives no reason to think its first table entry is a clock rate. `od` in task
C5b is flagged for the same kind of trouble, and `sort` and `dd` will have their own.

## It wrote NUL bytes into its output

`sep[]` and `nsep[]` hold `'\0'` where two digits should have nothing between them, and v7
printed it anyway:

```c
c = nonzero ? sep[i] : nsep[i];
fprintf(stderr, "%c", c);
```

On a terminal that is invisible, which is the only place v7 ever looked. Redirect the
diagnostic output — `time cmd 2>log`, which is exactly what
[../../kernel/test/utils.sh](../../kernel/test/utils.sh) does — and the file gets real NULs,
about six per line. Fixed rather than carried, per [../README.md](../README.md)'s rule, and
recorded in both the source and [time.1.umm](time.1.umm). Nothing visible changes.

## `execvp` had no prototype, so `<unistd.h>` got one

This was the fourth caller in the tree to open with a declaration of its own —
[execvp.c](../../lib/libc/gen/execvp.c) and [execlp.c](../../lib/libc/gen/execlp.c) declare
what they call, and [execs.c](../../lib/test/execs.c) declared all five — because v7 put the
two `$PATH`-searching forms in no header at all, while
[exec.2](../../lib/libc/man/exec.2.umm)'s NAME line has always named them. They are declared in
[`<unistd.h>`](../../include/unistd.h) now, beside `execl`/`execle`/`execv`/`exece`, and
`execs.c` dropped its copies. The two implementation files keep their self-contained style,
which is `execl.c`'s and `execv.c`'s: nothing in `lib/libc/` includes `<unistd.h>` to define
what it exports.

## The test is a masked diff, and it says so

`time` is the first program on this image whose output *cannot be asserted*: it reports how
long a command took, on a simulator, on whatever machine ran the build.
[run-utils.sh](../../kernel/test/run-utils.sh) therefore masks the three interval lines whole,
beside the mask it already applies to `date`'s seconds, and the comment there says which
columns survive and why.

What is left is not nothing, and choosing it was the work:

* **the failed exec's diagnostic** — `time /no/such/command` prints
  `/no/such/command: No such file or directory`, which is `strerror()`'s and is the assertion
  that the `sys_errlist[errno]` replacement works;
* **the exit status**, three times. `time` returns `status >> 8`, so `time kill` reporting 2
  is the assertion that a child's status passes through unaltered;
* **that three intervals were printed at all**, in the right order, with the right labels —
  which is what says `printt` ran nine times per line and did not fall over.

**What this does not cover is the arithmetic**, and that is stated rather than glossed: a
`time` whose radix table were wrong again would still pass, because the numbers are masked.
The `CLOCKS_PER_SEC/10` is held by the `_Static_assert` and by reading, not by a test. If that
ever needs a real oracle, the way to get one is a fixture that burns a *known* number of ticks
— which nothing on this image can currently do to better than a second.
