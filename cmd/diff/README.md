# `diff`, and a `#define` that read six characters where the thing it replaced read one

Task C5f, and the largest source of the seven at 647 lines plus `../diffh`'s 264. §1's C11
pass is mechanical and is not repeated here.

The brief predicted the expensive part would be `HALFLONG` — the 16-bit halves of a 32-bit
`long` that v7 folds its line hash in — and that was right about the place and wrong about the
cost. What cost the afternoon was a **replacement I wrote myself**.

## The finding: `isspace()` evaluates its argument once, and my replacement did not

v7 calls `isspace()` on a raw file byte at six sites in `diff` and four in `diffh`.
`lib/libc/gen/ctype_.c` is **129 entries** and says outright that only `isascii()` may be
applied to a byte above `0177`, so `diff -b` over a Cyrillic file read off the end of the
table — C5b's `uniq`/`look` finding, in two more programs. The obvious fix is to write the
blank set out:

```c
#define BLANK(c) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r'||(c)=='\f'||(c)=='\v')
```

and it is **wrong**, because every call site in this program passes an expression with a side
effect:

```c
} while (BLANK(c = getc(input[0])));      /* diff:  reads SIX characters */
while (BLANK(*++s)) ;                     /* diffh: advances SIX times   */
```

`isspace()` is a macro too — `(_ctype_+1)[c] & _S` — and it evaluates its argument **once**.
Six times is not a subtle degradation: `diff -b` reported two files differing only in trailing
blanks as *wholly* different, and printed truncated garbage under the `>` marker, because the
byte offsets `check()` collects for `fetch()` had run off with the stream. It looked like a
hash bug and it was not.

**A hand-written replacement for a library macro inherits the library macro's contract**, and
the part of that contract nobody writes down is how many times it touches its argument. Both
programs have a `static int blank(int)` function now. The general rule is worth carrying into
C10, where `awk`, `m4` and `dc` all lean on `<ctype.h>`: **when replacing a macro, look at
what the call sites pass, not only at what the macro tests.**

## The line hash, which is what the brief named

```c
#define HALFLONG 16
#define low(x)  (x&((1L<<HALFLONG)-1))
#define high(x) (x>>HALFLONG)
...
sum += (long)t << (shift %= HALFLONG);
...
return((short)low(sum) + (short)high(sum));
```

A one's-complement sum in 16-bit hunks of a 32-bit `long`, folded twice and truncated through
a `short`. Here `short == int == long == one 41-bit word`, so **both casts are no-ops and the
fold is a different function from the PDP-11's** — which does not matter at all: the hash only
has to be deterministic, and `check()` re-reads both files and verifies every match against
the real text. What *did* matter is that **`sum` had no bound of its own**. It grows by up to
2^23 a byte and wrapped a 32-bit `long` on a PDP-11 by construction; on a 41-bit word a line
past about 130 kilobytes overflows it.

The mask is written down now — `& 0xFFFFFFFF` inside the loop — which is C5a's `sum(1)`
finding from the other end. There, v7's checksum came out bit-for-bit right on this machine
*because* the mask was inside the loop rather than in the register width, and the port had
nothing to do. Here the same reasoning says the mask was missing and had to be added.

**And `readhash()` used `0` for two different things.** It returns the hash, and returns `0`
at end of file — but `0` is a hash a real line can produce, and `prepare()`'s
`for (j = 0; h = readhash(input[i]);)` stops on it. A file would have been silently truncated
at that line. End of file is the return value now and the hash comes back through a pointer.
It is `fgrep`'s C5c shape: a sentinel drawn from the value space it is supposed to be outside.

## A pointer below its own array, detected by a comparison that need not fire

```c
for (ai = &a[j]; ai > a; ai -= m) {         /* CACM #201 shellsort */
        aim = &ai[m];
        if (aim < ai)
                break;                      /* wraparound */
```

`ai -= m` steps **past the base of `a[]`** — with `j = 5, m = 3` it forms `a - 1` before the
loop test stops it — and the `aim < ai` guard exists to catch the address arithmetic wrapping
on a PDP-11. Both operands are `struct line *`, so both are **thin** pointers and neither is a
§2 lowering problem; what is wrong is forming the pointer at all, and that on a word-address
machine the wraparound the guard looks for cannot happen in the way it expects.

This is `comm.c`'s `lb1 - 1` from C5b, and it takes the same fix: `for (idx = j; idx > 0;
idx -= m)`, where `idx > 0` is exactly `ai > a` and no pointer outside the array is ever
formed. The wraparound test disappears with it — §6's rule that a check you can delete cannot
be half-written.

## `char c` against a function that answers EOF

`check()` declared `char c, d` and `skipline()` looped `for (i = 1; getc(input[f]) != '\n'; i++)`.
A `char` here is **unsigned**, so `getc`'s EOF becomes `0377` and never equals `'\n'`; on a
PDP-11 it was `-1`, which also never equals `'\n'`. **Neither machine terminated that loop**,
and a file whose last line has no newline was enough to hang v7's `diff` — except that
`prepare()` had already refused such a file for a different reason, which is why nobody met
it. Both are `int` and both test for `EOF` now, and `cmd_diff_nonewline` is the case.

## The temp file, and the two things wrong with eleven characters of it

```c
*pa1 = tempfile = mktemp("/tmp/dXXXXX");
...
done() { unlink(tempfile); exit(status); }
```

`mktemp(3)` **writes into its argument**, and the argument is a string literal. And `done()` is
the *normal* exit path as well as the error one, so every ordinary run of `diff` called
`unlink((char *)0)`. The name is a `static char[]` now and the unlink is guarded.

Under `b6sim` that path is the **build machine's `/tmp`** (§9's whose-is-it rule, cited for the
seventh time), so the `-` form is asserted under the booted kernel and nowhere else.

## Why `diffh` is ported rather than dropped

[../README.md](../README.md) offered the choice and the argument for porting is not completeness.
`diff` holds both files in core — one `struct line` per line plus three integer vectors over
them, about `6n` words — and when `malloc` fails it prints **`files too big, try -h`**.
`diffh`'s window is bounded by a *constant*: `RANGE` is 30 lines of each file, resynchronised
on `C` (3) successive matches. So on a machine whose whole user address space is 28,672 words
(§6), the message is *nearer* than it was on a PDP-11 and `-h` is the escape hatch of a
machine that needs one more, not less.

`diff -h` reaches it with `execv("/usr/lib/diffh", args)` — it **replaces itself**, passing its
own `argv` with the `-h` still in it, which is why `diffh`'s argument loop looks for a `b` and
ignores every other letter. That is v7's arrangement and it is left alone; what it costs this
port is a `dir /usr/lib` stanza in `root.manifest` and the rule that the path in `diff.c` and
the path in the manifest are one string.

**`/usr/lib/diffh` is the first entry this image has ever had in `/usr/lib`**, and it is the
only program of task C5f that does not go in `/bin` — so it is also the only one that does not
touch the two `ls /bin` listings.

## The oracle: a diff is not unique, so the property is checked and not the answer

Host `diff` cannot be this suite's oracle, and finding that out is worth as much as the cases
it produced. v7 finds a longest common subsequence by **Hunt–Szymanski** over Harold Stone's
k-candidates; GNU `diff` uses **Myers**. Both are correct and both are minimal, and they pick
different scripts: over 150 generated file pairs and four option sets they disagree textually
**79 times in 600 runs**, and not once about *whether* the files differ.

So the `.expected` files assert this program's answer, and the oracle that says the answer is
*right* is a property rather than a text:

> **`diff -e A B` emits an `ed` script, `ed` is on the build machine, and applying the guest's
> own script to `A` must produce `B` byte for byte.**

150 out of 150 do, and `run-diff-test.sh` re-checks 40 of them on every run. It exercises the
same `J` vector that drives the plain and `-f` outputs, so a wrong alignment could not hide in
one of the three. This is C4d's rule — the best oracle is one that can disagree — reached from
a third direction: not a second implementation (C5b's `od`), not a designed fixture (C5d's
`sort`), but **an invariant the output must satisfy**.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `diff` | 93 | 5,639 | 205 | 1,057 | **6,994** |
| `diffh` | 87 | 3,751 | 213 | 1,100 | **5,151** |

Out of the 28,672 words §6 allows — and for `diff` that number is the least interesting one in
this file, because everything the program actually works on is above the break. `bss` is 1,057
because stdio's two buffers are 1,024 of it: `diff` has no fixed table at all. `diffh`'s 1,100
is the same 1,024 plus the 60-word `text[2][30]` pointer array, and *its* heap is bounded by
`RANGE * (LEN+1)` — 15,360 bytes, whatever the input — which is the whole reason it exists.
