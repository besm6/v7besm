# `pr`, and a ring buffer whose one invariant nobody had written down

Task C5f, the eighteenth of the text filters and the last of the four "deciding" programs
[../README.md](../README.md) §2 has been counting byte cursors in. §1's C11 pass is mechanical
and is not repeated here; what follows is what the port *taught*.

The brief predicted two things about this program and got one and a half of them right. §2's
table said "two, both `>= &buffer[BUFS]`"; the count is **three** and the missed one is the
`<`. And [../TODO.md](../TODO.md) said `pr` is the one program left with `od`'s property —
output no reviewer can check by eye — which is true, but not for `od`'s reason.

## The three §2 relationals, and why they were rewritten anyway

```c
if (colp[ncol] < buffer)        colp[ncol] = &buffer[BUFS];   /* print()  */
if (rbufp >= &buffer[BUFS])     rbufp = buffer;               /* nexbuf() */
if (*p >= &buffer[BUFS])        *p = buffer;                  /* tpgetc() */
```

All three lower correctly since the compiler's fix of 2026-06-17, so **none of them was a bug**
and this is the fifth consecutive source where §2's table undercounted. They are `int` offsets
now for the reason `sort`'s two loop bounds became `int` counts in C5d and for no other: the
second and third run **once per byte** of every file `pr` prints, and a fat-pointer relational
is two out-of-line calls (`b$pdiff` then `b$lt`) where an `int` test is a register compare.
`colp[]` went with them.

The first one had an off-by-one that the rewrite made visible: `&buffer[BUFS]` is one past the
ring, so the next `tpgetc()` read the byte after the array. It is `BUFS - 1`.

## The finding: a look-ahead that laps its own buffer, silently

`pr -N` cannot print column 1 without knowing where column 2 begins, and column 2 is
`length - margin` lines further on — 56 lines on a default page. So `pr` holds **up to 72 read
cursors inside one 6,720-byte circular buffer**, refilling it 512 bytes at a time behind
whichever cursor is furthest ahead.

Nothing in v7 checked that the writer had not overtaken the *slowest* cursor. When it had, that
cursor read bytes belonging to a later part of the file, and the column simply started part way
down it:

```
$ pr -t -l800 -2 lap.txt          # 24,000 bytes; the two cursors are 6,400 apart
L000840                 L000800
L000841                 L000801
```

The first column should begin at `L000000`. No diagnostic, no truncation, no short page — the
listing looks entirely ordinary and 840 lines of it are missing. It is `col`'s and `sort`'s
failure mode again, and it needs nothing exotic: any two-column listing of a long page over a
file bigger than the ring does it.

`nexbuf()` measures it now, and the measurement is exact rather than conservative:

```c
for (i = 0; i <= ncol; i++) {
        d = colp[i] - bufp;
        if (d < 0) d += BUFS;
        if (d > 0 && d <= n) { ... refuse ... }
}
```

Every live cursor sits at or behind `bufp`, so its *forward* ring distance is either 0 — it is
waiting on the sentinel — or large. A **small non-zero** distance means the writer has come all
the way round to it, and `colp[i]` is the first byte the writer will destroy. `cmd/pr/test`
brackets it from both sides, which is C5c's `fgrep` rule: `-l700 -2` over the same file is
accepted *and its first line really is the first line*, `-l800 -2` is refused.

**The general form joins `col`'s heap and `grep`'s recursion.** `rootfs_pr_size` weighs
`const + text + data + bss` and the ring is in `bss`, so the size ctest can see it — what it
cannot see is that the ring's capacity is a function of the *arguments*. A bound that depends on
what the user typed is a fourth kind of ceiling and it has to be checked by the program.

## The sentinels did not have to change, and that is the interesting part

`pr` stores two markers **inside the character stream**: `0375` means "the ring wants refilling
here" and `0376` means "the input ended here", both written into `buffer` as ordinary bytes and
read back through `& 0377`. That is §11's third and worst shape — a program that steals a byte
value — and it is exactly what `col` had its Model 37 half-shift deleted for.

It is kept, because `0375` and `0376` are `0xFD` and `0xFE` and **no valid UTF-8 sequence
contains either byte**: lead bytes stop at `0xF4` and continuation bytes at `0xBF`. So a
Cyrillic file goes through this ring unharmed and the port had nothing to do. `cmd_pr_utf8`
asserts it.

The reason is worth stating precisely because it is a property of **UTF-8** and not of `pr`. The
same code over a KOI-8 or CP1251 image would corrupt text on the first `ý` or `þ`. C5a's rule —
what did not have to change is exactly what a diff cannot show — is why this paragraph exists.

## Columns are counted in bytes, deliberately

A two-byte Cyrillic letter occupies two columns of the layout, so a Cyrillic column aligns
against an ASCII one by byte and not by letter. This is [col/README.md](../col/README.md)'s
decision, taken again and for the same reasons: making it character-aware means decoding UTF-8
in `put()` and `pgetc()`, and it invents a policy for the truncation at a column boundary — a
lead byte that fits where its continuations do not produces mojibake, which is worse than a
misaligned column. `pr.1` says so in a section of its own.

## What else was fixed rather than carried

* **The heading was `sprintf`'d from an unbounded `-h` argument** into `char linebuf[150]`. §6's
  recurring finding, and the sixth port in a row to have one.
* **`mopen()` drove `nofile` negative.** On a file it could not open it did `isclosed[nofile] =
  1; nofile--;` — from zero, on the first argument, so `isclosed[-1]` was written — and it
  tested `if (++nofile >= 10)` *after* storing at index `nofile`. Both are gone; the loop tests
  the ceiling first and simply skips a file it cannot open.
* **`ncol` could reach the `width/ncol` divide as zero.** v7 tested `ncol > 72 || ncol > width`
  and not `ncol < 1`, so `pr -x` — `atoi` of a letter — divided by zero, as did `pr -m` with no
  file it could open.
* `extern char *sprintf();` at the head of `print()` is a hard conflicting declaration against
  `<stdio.h>`, which is §1's rename-on-sight from the other end: the fix is to delete it.

## Left alone, deliberately

`pr` **exits 0 after every diagnostic**, its own `done()` being both the error path and the
normal one. That is a real defect — a script cannot tell a refused listing from a printed one —
but it is not a defect *of this machine*, and the new look-ahead diagnostic follows the same
rule rather than being the one exception. `pr.1` says so under BUGS.

A NUL byte in the input is dropped rather than printed (`tpgetc()`'s `goto loop`), and a page
is padded out to `length` lines whether or not there is that much input. Both are v7's and both
are what `pr.1` now describes; BSD `pr` does neither, which is two of the three dialect
differences the host cross-check turned up.

## The oracle, and why it is not `od`'s reason

`od` needed a second implementation because its output is sixteen-digit octal. `pr`'s output is
ordinary readable text — what nobody can check by eye is the **whitespace in it**: which run of
spaces `put()` compressed back to a tab, which column a truncation fell in, how many blank lines
padded a short page. So every `.expected` came from a Python model of v7's algorithm written
from `pr.1` plus the six things `pr.1` does not state, and fourteen of the nineteen cases are
within the model's reach and agree with the program byte for byte.

**And two things about `pr` make its harness different from every other filter's.**

The page heading contains a **clock** — the file's mtime, or the time of day for standard input
— so *no run of `pr` without `-t` has a reproducible standard output*, and no such case can
exist under `b6_progtest` at all. That would leave the heading, the trailer, the page numbering,
`+n` and `-h` asserted nowhere. `run-pr-test.sh` fixes an mtime with `TZ=UTC0 touch -t` first
and asserts the heading as a literal. C5a's four file oracles exist because the output is a
*file*; this is the first one that exists because the output contains a *clock*.

The second is that **the terminal `chmod` is unreachable under either harness**, and that is the
point. `fixtty()` takes the tty to 0600 so that `write(1)` cannot interleave a message into a
listing; under `b6sim` that tty would be the *build machine's* (§9's whose-is-it rule). Every
case redirects standard output, so `ttyname(1)` answers NULL and the whole path is a no-op. The
mechanism is kept rather than cut — unlike `col`'s half-shift it still has a producer, `write(1)`
and `mesg(1)` being task C6 — and this file is the record that it is carried untested.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `pr` | 115 | 4,474 | 241 | 2,771 | **7,601** |

Out of the 28,672 words §6 allows, and `bss` is where all of it is. `b6nm` puts the four
objects that matter at

| | words |
|---|---|
| `buffer[6720]` — the ring | 1,120 |
| `obuf[BUFSIZ]` — `pr`'s own `setbuf` on standard output | 513 |
| libc's `_sibuf` and `_sobuf` | 1,024 |
| `colp[72]` | 72 |

which is 2,729 of the 2,771. **`BUFSIZ` is 3,072 here and was 512 on a PDP-11**, so `obuf`
alone is six times what it was upstream and is half again the size of everything else `pr`
declares — §6's "what a program prints with dominates what it does", from a program that
declares its own buffer and then gets libc's two anyway.
