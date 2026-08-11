# `egrep`, and a recursion as deep as the pattern is long

Task C26, the last of the grep family and the second program here built from a **grammar**.
C5c put `grep` and `fgrep` on the image and left this one behind the yacc decision; C10 and C11
retired that, so what was left was the port. [../README.md](../README.md) §1's C11 pass is in
the source header and is not repeated here — what the port *taught* is below.

The task brief pointed at [../grep/README.md](../grep/README.md) as the worked example and named
three things to expect: the `CCL` bitmap, a table whose size is written down nowhere, and a bound
test that is not on every path. **The first does not exist in this program** — a class here is a
counted list of bytes, not a bitmap — and the third arrived in a shape the brief did not have:
not a bound test that is missing from one path, but two that are *present on every path and off
by one.*

## One constant, two out-of-bounds, and neither is what `grep` had

`NCHARS` is 128 in v7 and the alphabet on this machine is 256. That single constant was wrong in
two places at once, and the two are different bugs in different storage:

```c
cstat = gotofn[cstat][*p&0377];     /* a READ, past the end of a 128-byte row */
symbol[chars[pc++]] = 1;            /* a STORE, past the end of a 128-byte FRAME array */
```

The first is in `execute()` and it is the one a user meets first: `gotofn` is `[128][128]`, the
index is 0..255, so **any byte above `0177` takes the next state's transition.** It never faults
— the read stays inside `gotofn` — and it needs no Cyrillic *pattern* to happen. A plain ASCII
`egrep alpha` over a UTF-8 file is enough, and a search program that gets UTF-8 input wrong
whatever it was asked for is worse than one that gets Cyrillic patterns wrong.

The second is in `cgotofn()`, and `char symbol[NCHARS]` is an **automatic** array. Where `grep`'s
wild store landed in `expbuf` — bss, forward, into bytecode not yet written, so an undersized
class came out empty — this one lands on `cgotofn`'s own locals, 80 bytes past a 22-word array,
on top of the loop counters building the machine. `grep`'s produced an empty class; this one
produces *a corrupted state machine*, which is the same class of failure one level further from
the cause.

**Neither is visible to AddressSanitizer,** which is worth writing down because the plan for this
task assumed it would be. Both accesses stay *inside* the enclosing object: one inside a 2-D
array, one inside a stack frame, and ASan's redzones sit between objects. The tool that sees them
is UBSan's `array-bounds`, which knows the declared inner extent:

```
egrep.y:333:25: runtime error: index -48 out of bounds for type 'char[128]'   <- the store
egrep.y:605:17: runtime error: index 208 out of bounds for type 'char[128]'   <- the read
```

(`-48` rather than `208` because the host's `char` is signed; see below.) So the general rule
[../grep/README.md](../grep/README.md) states about assertions applies to *tools* as well: **an
intra-object overrun is invisible to the sanitizer everybody reaches for first**, and the
assertion still has to be a case with a known answer. `cmd_egrep_utf8nomatch` is that case.

Widening `NCHARS` to 256 closes both. `NSTATES` stays 128: widening the alphabet creates no
states, and `gotofn` costs `NSTATES × NCHARS` bytes. The widening is asserted rather than
remarked on, because 256 is the *largest value that can work* —

```c
_Static_assert(NCHARS <= DOT && NCHARS <= CCL && NCHARS <= NCCL, ...);
```

`cgotofn()` tells a literal byte in `name[]` from a grammar token by `c < NCHARS`, and `b6yacc`
hands out token numbers from `0401` up. So the alphabet may grow to exactly 256 and no further,
and it happens to need exactly 256. That assertion can be written at all only because `%token`
stands above `%{` in this file: `defout()` runs at the end of the declaration section, so `DOT`,
`CCL` and `NCCL` are already `#define`d by the time the block is copied out.

## The same line fails differently on the two machines

`yylex` held its character in a `char`, and that is a §11 hazard with **two** answers rather than
one:

* On the BESM-6 a `char` is unsigned, so a literal `п` arrives as `208`, is not `< NCHARS`, is
  not `DOT`/`CCL`/`NCCL`, and falls out of `cgotofn`'s dispatch into v7's `printf("something's
  funny\n")` — on *standard output*, without exiting, and the machine goes on being built.
* On the host a `char` is signed, so the same byte arrives as `-48`, `cgotofn`'s
  `if ((c = name[curpos]) >= 0)` drops the position silently, and a literal Cyrillic letter
  matches nothing at all.

One source line, two silent failures, one per machine — which is the argument for the host oracle
being a *guide* and not a substitute, and the reason `nextch()` reads `*(unsigned char *)input++`
rather than `*input++`. With that, both machines see 0..255 and the oracle is worth having: 1,200
generated patterns × 8 flag combinations, diffed against GNU `grep -E`, **0 differences**. On
v7's code the same corpus differed on 100 of 400.

(The host's own `grep` is `ugrep` here, which answers differently from POSIX about a nullable
expression. `ggrep -E` is the oracle. That cost an hour of chasing a difference that was the
oracle's.)

## `nextch()` could not see EOF, so `-f` did not work at all

```c
register char c;
if ((c = getc(stdin)) == EOF) return(0);        /* v7 */
```

A `char` here is unsigned, so `c == EOF` is `255 == -1` and never true. `egrep -f patfile` never
reached the end of the pattern file: it read `0377` for ever, `yylex` handed back `CHAR` after
`CHAR`, and `enter()` tripped `MAXLIN` — so **a whole flag was disabled, and the diagnostic named
the wrong thing**, `regular expression too long` for an ordinary two-line file. On a PDP-11's
signed `char` it worked by accident. `int c`, and it is `cmd_egrep_fromfile`.

## Two bound tests that are off by one

`grep`'s lesson was *a bound test that is not on every path is not a bound test, and reads
exactly like one.* These are the next shape along — on every path, and wrong by one:

```c
if (n >= NSTATES) overflo();     /* then: add(state, ++n); ... out[n] = 1;   */
if (nxtpos + count > MAXPOS) overflo();   /* then: writes count + 1 words    */
```

The first stores `state[NSTATES]` and `out[NSTATES]` when `n` reaches `NSTATES-1`. `out[]`
follows `state[]` in the source, so the overrun most likely lands on state 0's accepting flag —
**a machine that matches every line or none, with no diagnostic**, exactly where the manual page
promises `regular expression too long`. `cmd_egrep_toomany` is the case: seven `(a|b)` groups.
The second writes one word past `positions[]`. Both are `>= NSTATES - 1` and `>= MAXPOS` now.

There is a third thing in those four lines that is *not* a change and is commented as such:
`add()`'s loop starts at 3, but `count` can include position 1, so `foll[1]` is written with a
length one larger than the entries that follow it. Nothing reads it — position 1 is excluded
from every state set by `cgotofn`'s `count--` — and a reader who "fixes" it will change what the
machine does.

## `-b` was a third answer

The divergence is the same one `grep` and `fgrep` took in C5c, and `egrep` differed from them
**twice over**: it divided by a hard-coded 512, *and* it divided the position of the **end** of
the line where the other two divided the start.

```c
printf("%ld:", (blkno-ccount-1)/512);       /* v7 */
```

So the three commands gave three different numbers for one match. `blkno` is gone and `coff`/
`loff` are `fgrep`'s, a byte counter and the offset of the current line's start — `cmd_egrep_offset`,
`cmd_grep_offset` and `cmd_fgrep_offset` all say **19** over the same fixture now, and if one
ever moves without the others the divergence has been part-undone.

One decision inside it. `egrep`'s ring has a lap fix-up `fgrep`'s has not — `if (nlp > p && nlp
<= p+ccount) nlp = p+ccount;`, which drags the line start forward when a read overwrites it. The
obvious thing is to drag `loff` with it, and it is wrong: `-b` would then report where the
surviving *fragment* starts, which is neither what the manual says nor what `fgrep` and GNU
print. `loff` is left alone, so **`-b` reports where the line began even when the beginning is
no longer there to print.** `cmd_egrep_offsetwrap` is the case, and nothing else in the suite
reaches that code.

## A recursion whose depth is the length of the pattern

This is the finding worth the most, and it is [../README.md](../README.md) §6's *sometimes no
ceiling will do*, reached from a third direction after `lex` and `fgrep`.

`cfoll()`, `cstate()` and `follow()` walk the parse tree, and v7 wrote all three as recursions.
It looks like `grep`'s `advance()` — depth set by how deeply some operator nests — and it is not.
`r r %prec CAT` is **left-associative**, so *N* literal characters build a left spine of *N*
`CAT` nodes, and `follow()` nests a second walk of the same height inside `cfoll()` at every
leaf. **The depth is the length of the pattern**, and `egrep 'a fairly long sentence somebody
wants to find'` is already fifty deep.

Measured on this machine, `b6nm` + the `15 utm 0NNN` prologue in the `.dis` that `b6_prog()`
writes:

| | recursive | iterative |
|---|---|---|
| `cfoll` | 22 words *per level* | 30 words, flat |
| `follow` | 29 words *per level* | 31 words, flat |
| `cstate` | 50 words *per level* | 96 words, flat |

At roughly 51 words a character against a 4,096-word stack, that is about eighty characters. And
it is not arithmetic — the recursive build was linked and run under `b6sim`:

| pattern | v7's recursion, on this machine |
|---|---|
| 60 characters | the right answer |
| **80 characters** | `b6sim: error: stack protection violation @00705` — inside `cfoll` |
| 100 characters | `b6sim: error: Jump to zero @00000` |
| 120 characters | `b6sim: error: Division by zero @11337` |

The last two rows are §6's own description of how a blown stack surfaces here: not a fault at the
point of failure, but the heap under the stack overwritten and something unrelated dying later.

**And the tables accept 127 characters.** So the recursion failed on patterns comfortably inside
what the program itself admits, which is what rules out the cheap fix: a `MAXDEPTH` like `grep`'s
would have to sit near 80, and a search program that refuses an ordinary long phrase is worse
than one with a limit nobody reaches. So all three walks are iterative, as `lex`'s tree walk is,
and there is no depth limit left to reach. The same probes on the iterative build are clean at
127.

The stacks are sized from `MAXLIN` with **no bound test and none needed**, which is `grep`'s
other lesson used deliberately: a node is the right child of at most one parent, so at most one
entry per node is ever pushed, and the tree's height is under `MAXLIN` because its nodes are.

`cstate()` is the only one of the three that is real work, because it returns a value and its two
binary arms differ **on purpose**: `CAT` visits its right child only when the left one answered 0
— that short-circuit *is* the initials-of-a-concatenation rule — while `OR` visits both, which is
why v7 wrote `b = cstate(right[v]);` first. Flattening that wrongly changes answers without
changing anything visible, so it was gated on the differential run rather than on reading:
**7,200 comparisons of the iterative build against the recursive one, 0 differences.**

## What the harness cannot say, checked by hand

[../README.md](../README.md) §9: `b6_progtest` splits its `.args` line on whitespace with no
quoting, and there is no second world left to put the rest in. So these were run by hand and the
answers are here:

* **A pattern containing a space.** `egrep 'вет мир' text.txt` → `привет мир`, status 0.
* **An empty pattern**, and this is the interesting one — **the three commands give three
  different answers**, all of them v7's own:

  | | `egrep ''` | `grep ''` | `fgrep ''` |
  |---|---|---|---|
  | | `egrep: syntax error`, status 2 | every line, status 0 | nothing, status 1 |

  `egrep`'s is the grammar: `nextch()` returns 0 at once, `yylex` returns the end token, and
  `t: b r` cannot reduce without an `r`. GNU matches every line, as `grep` does here.
* **`-cs`**, which the plan for this task expected to be a fourth three-way disagreement and is
  not: all three print the count. Checked rather than assumed, and the note that would have
  claimed otherwise is not in the source.
* **A last line with no trailing newline** is not examined, by `grep` or by `egrep`; `fgrep` does
  examine it. v7's behaviour in all three, now in `grep.1`.
* **The 4,096-word stack.** `rootfs_egrep_size` weighs `const + text + data + bss` and a frame is
  none of them. `cmd_egrep_longpat` shows that *one* 120-character pattern is walked without a
  fault; it cannot show the walk is bounded. The table above is what shows that.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `egrep` | 109 | 4,889 | 595 | 15,064 | **20,657** |

Out of the 28,672 words §6 allows — `fgrep`'s league, and for the same reason: one table is most
of it. `gotofn[128][256]` is 32,768 bytes = **5,462 words**, of which the widening bought 2,731;
`positions[4000]` is another 4,000; the seven `MAXLIN` arrays and the three new walk stacks are
2,800 between them. **14 blocks of the disk**, 378 free before and 364 after; the two manual-page
links cost nothing.

## Two other things worth knowing

**`y.output`'s conflict count is not zero here**, where `expr`'s was. `egrep.y` has two
shift/reduce conflicts, both on the `error` token in `r: error` (states 14 and 20), both v7's own
and both resolved by shifting. [../yacc/README.md](../yacc/README.md) already recorded them,
because `yacc_agree(egrep …)` has been building this grammar on every `ctest` run since C10c. So
a non-zero conflict count is no longer by itself a sign of trouble — a *third* one would be.

**`man egrep` used to find nothing.** `grep.1`'s NAME line is `grep, egrep, fgrep`, as v7 wrote
it, but §10 makes the name of the *page* decide where `man` looks. Two `link` stanzas in
[../../root.manifest](../../root.manifest) fix it for `egrep` and for `fgrep` at zero disk cost,
rather than two more copies of one text.

## One thing this port found and did not fix

`fgrep` has **no lap fix-up at all**, and past 1024 characters it prints from a ring position the
reader has already reused — so what comes out can include text from *later lines*, not merely the
tail of the long one. Measured under `b6sim` while establishing what `grep.1` should say about
the ring; the page says it now. It is `fgrep`'s bug, it is v7's, and closing it is not C26's —
but it should not go unrecorded, and `egrep`'s fix-up is the model for it.
