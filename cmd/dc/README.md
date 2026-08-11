# `dc`, and what a program costs when its numbers are not the machine's

Task C15, and the engine [`../bc/`](../bc/) drives at C16. `dc.c` is 1,943 lines over a 119-line
header, and it is unlike every port before it in one way that decides all the rest: **its numbers
are its own representation.** A value is a `struct blk` — a heap buffer of `char` cells holding
decimal digit *pairs* 0–99, least significant first, topped by a cell of −1 when the value is
negative, and ending in a scale byte. So [`../../lib/libm/`](../../lib/libm/) never comes into it,
and instead the whole port turns on what a `char` is.

The C11 pass is described in the two sources' own headers and is not repeated here. Six things the
port *taught* are, because none of them is about this program alone.

## An unsigned `char` costs every negative number at once

Plain `char` is unsigned on this machine, so the −1 sign cell reads back as 255 and every
`sbackc(p) < 0` and `== -1` in the program is quietly false. `add()`, `divide()`, `mult()`,
`chsign()` and `cond()` all lose their sign handling together, and the program does not fail — it
answers wrongly.

v7 had met this before, on the Interdata 8/32, and the answer was already sitting in `dc.h` behind
`#ifdef interdata`: cell readers that sign-extend a byte at or above `0200`. That arm is now the
only one there is. **The lesson is the smaller one: a v7 source that was ever ported to a machine
unlike the PDP-11 may already carry the fix, under a name nobody here would grep for.** It cost one
`#ifdef` where finding it from first principles would have cost a day.

Two halves of that decision are worth stating because they are not symmetrical:

* `sgetc`, `slookc` and `sbackc` sign-extend — they read cells, and a cell may be the sign word.
* `sunputc` must **not** — it reads the scale byte alone, which is 0–99 and never negative.

## The one place sign extension must not go, and why the fix is not v7's

A block carries no type. `p` decides between a string and a number by scanning for a cell above 99
— and that heuristic breaks in *both* directions here:

| read | `_5p` | `[привет]p` |
|---|---|---|
| sign-extended | correct | `0xD0` reads as −48, so the string prints as a **number** |
| raw | `0377` reads as 255, so the number prints as a **string** | correct |

Neither pure choice works, which is why `isstring()` exists. It reads **raw** and allows `0377`
only where a sign cell can sit: the top magnitude byte, the scale byte being last. That is exact
for numbers, and it repairs the eight-bit half.

What it deliberately does not repair is v7's own wart: no byte of `abc` exceeds 99, so `[abc]p`
still prints a number — a very long one, since the block's last byte becomes a scale of 99. The
`strascii` case pins that, `strutf8p` pins the divergence, and `../README.md` §11's rule is the
reason the second was worth having: **a masked or misread byte does not have to vanish to be
wrong.**

It also decides how half the test suite is written. A macro that must print its own name is
spelled `[lt]`, `[gt]`, `[eq]` and never `[a]` — a lowercase letter from `d` on is the cheapest
way to get a byte above 99 into a block.

## The `blk`/`wblk` alias does not survive, and the reason is one bit

An array register (`:` and `;`) holds block pointers where a number holds digits. v7 aliased the
four-word header with a second struct of `struct blk **` and moved its cursors through that view.
Both structs are four words here, `PTRSZ` is 6 which is both `sizeof(struct blk *)` and the char
units in a word, and a `+1` on the word view and a `+PTRSZ` on the byte view move the same
distance. Every reason to expect it to work is present, and it does not.

A cursor is a **fat** `char *`: a byte offset above a 15-bit word address, with the marker in bits
48–45. An `int` is 41 bits. So the same word, read through a `struct blk **`:

* **increments wrongly** — `*wp->rdw++` stored a value that never reached `hptr->rd`; and
* **orders wrongly** — `wp->rdw > wp->wtw` compares the marker as data.

Equality is safe, because both sides carry the same marker, which is exactly what made the failure
hard to see: `getwd()` returned elements fine and the *cursor* never advanced, so a one-element
array read back as empty and `0;a` printed 0. It looked like a storage bug and was an arithmetic
one.

`struct wblk` is gone. **Only the dereference goes through the word view** — `wdat(q)` casts a byte
cursor to a word pointer, which floors it, and at byte offset 0 that is exact — **and every cursor
motion and comparison stays in the byte view.** The general rule is worth carrying to the next
port that aliases two struct types over one buffer: *on this machine, the safe operations through
an aliased pointer type are dereference and equality; arithmetic and ordering are not, whenever the
value being aliased is a `char *`.*

## An array element is one word, and three things changed size with it

`PTRSZ` was 2 on the PDP-11. Everything that counts in it moved:

* An unset element used to print as `0` **because** a pointer was two bytes; six null bytes print
  as nine zeros. `;` now pushes a real zero, as `load()` does for an unset register.
* A full array is `(2047+6)*6` = 12,318 char units = **2,053 words**, about a tenth of the heap,
  where v7's was 684. `MAXIND` stays 2048 — it is `bc`'s bound and C16 depends on it — so this is
  a number to know rather than a thing to change, and `arraybig` pins that the top index works.
* `S` on an array name pads to `PTRSZ` from the copy's write cursor, which for anything but the
  value 0 leaves a length that is **not** a whole number of slots. v7 walked such a block with
  `rd == wt`, which never comes true, and read off the end for ever.

That last one is why `morewd()` and `isblkptr()` exist. The walk demands a whole slot and a word
that could be a header address — bits 15–1 and nothing above, which a digit byte in bits 48–41
cannot pass. Without them, `sя` followed by `lя` read a number's digits as pointers and took the
heap with it. **In ASCII that path is unreachable; every Cyrillic lead byte is `0321` or `0320`,
and `ARRAYST` is `0241`, so it is one keystroke away here.** The same fact has a plainer face,
now in `dc.1`: a register name is one *byte*, so a Cyrillic letter is two commands, and the first
of them names an array.

## `31` was the top bit of a `long`

`log2v()` counted down from 31 and returned the bit length. On 41 bits it returns a *negative*
number, and `logo` feeds the fractional digit count of every print in a base but ten — so
`16o 4k 1 3/p` printed one hex digit instead of four, silently. `obasefrac` is the case.

It is the cheapest kind of §3 bug to write and the most expensive to notice: nothing overflows,
nothing faults, and the answer is merely short. Grepping for `long` finds it; grepping for a
wrong answer does not.

## The measurements

```
	const	text	data	bss	dec	oct
	  110  13422	 380   2053  15965  37135
```

15,965 words against the 28,672 ceiling — `rootfs_dc_size` is the check, and for this program it
weighs the thing that matters least. **dc's whole subject is the heap**, which `check-size.sh`
cannot see: the arena runs from `end` to the stack at `070000`, so roughly 12,700 words, and one
full array is a tenth of it.

**The stack, which nothing checks.** Read out of the `15 utm 0NNN` prologues in
`build/cmd/dc/dc.dis`, the deepest chain is

```
main 11 + commnds 1460 + prtblk 224 + bigot 206 + divide 706 + add 297
     + salloc 21 + garbage 30 + redef 29 + realloc 47 + malloc 62  =  3093
```

of 4,096, leaving 1,003. That is comfortable, and it is *bounded*, which matters more: dc's call
graph is a DAG. Macro nesting lives in `readstk[]` and array growth in the heap, so **no input can
make the C recursion deeper**, and 3,093 is the true maximum rather than a sample.

Which is exactly what v7's interrupt handler broke. `onintr()` called `commnds()` and never
returned, so every `^C` left a frame behind — and `commnds()` is **1,460 words**, more than a third
of the whole stack. One interrupt taken during a print would have put the program at 4,553 and over
the edge, into the top of the heap, silently. The handler now re-arms and `longjmp`s to a `setjmp`
in `main`, where the read-stack unwind runs at constant depth. The behaviour at the keyboard is
v7's; the depth is not.

**Output.** v7 asks for `setbuf(stdout, NULL)` — one `write(2)` per printed character. Nothing is
asked for instead: `_flsbuf()` line-buffers stdout when `isatty(1)` and buffers it fully when it
does not, which is what each case wants, and `readc()` and `command()` flush before they block so
a prompt and a `P` still reach the terminal on time. Everything else held identical:

| | instructions | total words |
|---|---|---|
| v7, `setbuf(stdout, NULL)` | 182,001 | 16,020 |
| here, no `setbuf` | **176,442** | **15,965** |

on `50k 1 97/p`. And that understates it, because **b6sim services a syscall on the host for
almost nothing** — the fifty `write(2)` traps per line that this removes cost the real kernel far
more than they cost the measurement.

One thing measured and *not* taken: replacing `printf("%c", x)` with `putchar` in `OUTC`, the
obvious §2 saving. It is worth 0.3% of instructions and costs 800 words of text, because
`include/stdio.h` holds a line-buffered stream at `_cnt == 0` so that every `putc` misses into
`_flsbuf` — **the inline fast path that makes `putchar` worth having never fires on the stream dc
actually writes to.** v7's `printf` is kept, and the program is smaller for it.

**On the disk** `dc` costs **32 blocks**, and the image went from 321 free to **289** — the largest
single addition since the manual, and worth weighing against `../TODO.md` before the next one. The
manual page was already staged: `B6_STAGE_MAN` globs `cmd/*/*.umm` and `../../root.manifest` has
carried `/usr/man/man1/dc.1` since long before there was a program.

## What this harness cannot say

71 cases under [test/](test/), and `../README.md` §9's four oracle shapes are all here: designed
fixtures for the operators, the invariant `x y * y / x -` → `0` for forty-digit arithmetic nobody
can check by eye, and the host's `dc` replayed over the arithmetic subset — 18 of 19 expressions
agree byte for byte with GNU dc 6.7.6, including 50-place division and `2^100`. The nineteenth is
`0.99995` against GNU's `.99995`, which is v7's `tenot()` printing a leading zero when the digit
count is odd, and is correct here.

What has no home:

* **`!` and the shell escape.** b6sim cannot exec `/bin/sh`. `!<x`, `!>x` and `!=x` never reach
  the fork and *are* tested, in `cond`.
* **That a `P` or a prompt reaches the terminal before dc blocks.** The flushes are asserted for
  content, never for timing.
* **`garbage()` and `ospace()`.** Exhausting the arena takes minutes and ends in `abort()`; the
  compaction is exercised only by `arraybig` coming near it.
* **`Y`.** Its counters are allocator-dependent. Run it; do not assert it.

Two things were checked by hand instead, in two different worlds, and it is worth saying which
went where.

**Under the kernel**, driving `kernel/unix.ini`'s configuration from a scratch `.ini` — the shell
escape, which b6sim cannot exec at all, and everything else at a real terminal:

```
Single-user mode -- type ^D to run /etc/rc and go multi-user
# dc
[la1+dsa*pla6>y]sy 0sa1 lyx
1
2
6
24
120
720
_5 5*p
-25
20k 2 v p
1.41421356237309504880
16o 4k 1 3/p
.555
[abc]P
!abcecho hi from a shell escape
hi from a shell escape
!
q
#
```

That fifth-from-last line is not a typo and is the buffering working: `P` writes `abc` with no
newline, so it sits in the line buffer until `command()` flushes before `fork()` — which is
exactly where the flush was put, and it lands between the `!` the terminal has echoed and the
rest of the line.

**The `^C` unwind was checked under b6sim and not under the kernel.** b6sim runs a guest handler
at the next syscall return, which is enough: a `SIGINT` sent during `2 9000^` was caught, the
`longjmp` returned to `main`, and `f` then showed the operand stack `3 2 1` untouched with
`17 3+p` still answering `20`. The same dialogue under SIMH is what could *not* be got: the
harness fails at its first `send` more often than not, before dc is even running, and a check
that flaky is worth less than saying so. What the measurement above does establish without a
terminal is the reason for the change — `commnds()` is 1,460 words, and v7's handler pushed
another copy of it per interrupt.

## What did not need saying

Two of the plan's worries were not worries. **`realloc` after `free` works** — `seekc()`,
`sgrow()` and `redef()` are all built on v7's "reallocate a block freed since the last `malloc`",
and `lib/libc/gen/malloc.c` is v7's allocator algorithm for algorithm, down to that contract.
And **`malloc(0)` returns a real one-word block**, so `salloc(0)` — which `init()` calls three
times before anything else happens — needed nothing.

The `readc()` typo is the third. v7 wrote `readptr != &readptr[0]`, which is `readptr != readptr`,
so the branch that pops a `?` frame at end of file never ran. It is written as intended now,
because the code plainly means it — but **no case pins it**, and that is honest rather than
regrettable: both paths converge on `exit(0)`, so the fix is a repair to what a reader will
believe and not to what the program does.
