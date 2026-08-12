# `grep`, and the sixteen bytes that were never wide enough

Task C5c: `grep` and `fgrep`, the fourteenth and fifteenth of [../README.md](../README.md)'s text
filters, and the two that carry the **`CCL` bitmap** the plan has been pointing at since task
C3. [../README.md](../README.md) §1's C11 pass over both files is in their own headers and is
not repeated here — what the port *taught* is below.

The task brief called these two cheap. Six of the eight things below were not in any table it
had; two are bugs a user would have met on the first Cyrillic pattern, and two more — a stack
overrun and a failure function that skipped a hop — were found only after the task had been
called done, by measuring instead of reading.

## The bitmap was not only narrow. It was also a wild store.

[../README.md](../README.md) §11 and [../README.md](../README.md) both described the hazard the same
way: `grep` packs a character class into 128 bits, sixteen bytes addressed `bittab[c & 07]` at
`c >> 3`, and it has to become 256 bits so that a class can hold a byte above `0177`. That is
true, and it is the *match* side:

```c
case CCL:
	c = *lp++ & 0177;			/* v7 */
	if (ep[c>>3] & bittab[c & 07]) { ep += 16; continue; }
```

The **compile** side has no mask at all, and nobody had looked at it:

```c
	ep[c>>3] |= bittab[c&07];		/* v7, and c is 0..255 here */
```

`char` is unsigned on this machine, so a pattern byte of `0300` gives `c >> 3 == 24` and the
store lands eight bytes past a sixteen-byte class, on top of whatever bytecode `compile()` had
already written into `expbuf`; `0377` reaches `ep[31]`. On the PDP-11 a signed `char` made the
same expression `ep[-16..-1]`, a store *before* the class — different rubble, same bug. So v7's
`grep` did not merely fail to match a Cyrillic class; it corrupted the compiled expression while
being asked for one.

**Widening the table to 32 bytes fixes both at once, and that is the whole of the change.**
`c >> 3` then lands in `[0,31]` by construction, so no mask is needed on either side and the two
sides stop needing to agree about one. The width is a single constant now — `CCLSIZE` — because
five places had `16` written into them and any one of them left behind is a class that is
half-right:

| | v7 | here |
|---|---|---|
| room check before compiling a class | `&ep[17] >= &expbuf[ESIZE]` | `&ep[CCLSIZE+1] >= …` |
| the negation loop | `cclcnt < 16` | `cclcnt < CCLSIZE` |
| step over a class, in `compile()` | `ep += 16` | `ep += CCLSIZE` |
| step over a class, `CCL` in `advance()` | `ep += 16` | `ep += CCLSIZE` |
| step over a class, `CCL\|STAR` | `ep += 16` | `ep += CCLSIZE` |

`ep[0] &= 0376` stays: a negated class must still not match the NUL that terminates the line.

**The assertion that says it works is a *negative* one.** `cmd_grep_utf8class` shows `[пм]`
matching a Cyrillic line, and that is the feature — but the case that fails on v7's code is
`cmd_grep_utf8nomatch`, because `& 0177` folds `320` (the lead byte of a Cyrillic capital) onto
`0120`, which is `P`. v7's `grep` would find `[п]` inside **ALPHA**. The general shape is
[../col/README.md](../col/README.md)'s: **a masked byte does not vanish, it turns into a
different plausible letter**, so the assertion has to be a case with a known answer rather than
an eyeball.

**`sed` has the same bitmap and it is still 128 bits** — `sed0.c:732` and `sed1.c:243`/`296`,
the identical `ep[c>>3] & bittab[c & 07]` — so task C5e inherits every line of this section.
`ed` turned out not to have one at all ([../ed/README.md](../ed/README.md)), which is the
correction that moved the warning here in the first place.

## `grep -c` printed the two characters `%D`

```c
	printf("%D\n", tln);		/* v7 */
```

§3 is exact about what that does here: `%D` is not a conversion, `doprnt()` echoes an unknown
one verbatim **and consumes no argument**. So the one flag whose entire output is a number
printed no number at all.

What makes it worth a paragraph is where it was found. [../README.md](../README.md) §3 records
that thirteen filters had been grepped for `%D` and `%O` with no hit — a negative result stated
as a prediction, that the sources which spring the trap are the ones printing a number they did
not compute themselves. This is the fourteenth filter and it springs it, in a file that spells
the *same* quantity `%ld` two lines further down. **A negative result over thirteen files is not
a property of the fourteenth**, and the cheap habit — grep each new source, however many came
back clean — is what caught it.

## `-b` is a byte offset now, and that is a divergence

The fourth deliberate divergence in `cmd/`, after `touch`, `rev` and `col`, and it is recorded
in three places as §10 requires: the source header, `grep.1.umm`, and here.

v7's `-b` prints a **block number** — the offset divided by a disk block. Two things were wrong
with carrying that:

* **512 names nothing on this machine.** That is `dd`'s rule from task C4b: a constant is the
  user's business only while it still names something here.
* **The two programs disagreed.** `grep.c` divided by `BSIZE` out of `<sys/param.h>` — 512 in
  v7 and **3072** here, so it silently retuned itself — while `fgrep.c` had `512` written into
  the source and included no system header at all. Same flag, same manual page, same match,
  two different numbers.

Dividing by `BSIZE` in both would have made them agree, and would have made `-b` report this
filesystem's *blocking* rather than a position. **The division goes instead.** A byte offset is
exact, needs no unit, is what every later Unix means by `-b`, and it cannot drift apart between
two programs because there is nothing left to choose. `cmd_grep_offset` and `cmd_fgrep_offset`
both say `19` over the same fixture, and `filters.sh` has both say `35` over another; if one
ever moves without the other the divergence has been half-undone.

Neither program uses `ftell()` for it. `fgrep` reads with `read(2)` and has no `FILE`, and
`grep` must work when standard input is a pipe, which `ftell()` cannot answer — so each counts
the bytes it consumes and remembers the count at the start of each line.

## `fgrep` did not fit the machine, and a line count could not have said so

```c
struct words {
	char 	inp;
	char	out;
	struct	words *nst;
	struct	words *link;
	struct	words *fail;
} w[MAXSIZ];			/* MAXSIZ 6000 */
```

A `struct words` is **four words** here — measured, not assumed: the two adjacent `char` members
share one word and the three pointers take one each, where the PDP-11 packed the lot into eight
bytes. So v7's table is **24,000 words of bss**, and [../README.md](../README.md) §6 gives a user
program 28,672 words for `const + text + data + bss` together. `fgrep` was over the ceiling
before a byte of its own text, and `rootfs_fgrep_size` would have said so on the first link.

`MAXSIZ` is 3000, which is 12,000 words; the failure-link queue the section below is about is
sized from `MAXSIZ` and is 3,000 more, and the program measures 20,019 all told. Two
`_Static_assert`s hold it — one on `sizeof(struct words)`, one on the two arrays' word budget — so
that a change to either breaks the build rather than the image, which is
[../README.md](../README.md)'s rule for anything that encodes a layout. `overflo()` has always
diagnosed the limit, so the narrower table fails loudly.

This is C4e's `ncheck` finding again ([../icheck/README.md](../icheck/README.md)): **a fixed
table is a ceiling somebody chose against different hardware.** What is new is how it hid.
`ncheck`'s 2503-entry hash table was visibly enormous against an i-list of a thousand; `fgrep`'s
6000 looks like an ordinary generous constant, and the thing that makes it unaffordable is not
the count but `sizeof` — six words of struct definition that read identically on both machines.
**Multiply the count by the layout before believing a table is small**, and the layout is worth
measuring rather than deriving: this one came out at four words where the arithmetic in the
task's plan said five.

## And the second ceiling was a 400-word queue with a bound test on one arm

`MAXSIZ` is the ceiling this task wrote down. It is not the ceiling the program reached. §6's
unchecked stack is where `fgrep` fell over too, by a different route from `grep`'s recursion two
sections below — the same task found it twice, once by trying and once by counting.

`cfail()` walks the trie breadth-first to compute the failure links, and v7 walked it through a
`struct words *queue[400]` in `cfail`'s own frame. The wrap arithmetic had two arms:

```c
if (front < rear) {
    if (rear >= &queue[QSIZE - 1]) {        /* bounded */
        if (front == queue)
            overflo();
        else
            rear = queue;
    } else
        rear++;
} else if (++rear == front)                 /* v7: no bound test on this arm */
    overflo();
```

**The unchecked arm is the one a chain takes.** Dequeue advances `front`, enqueue advances
`rear`, and when the trie has no branches — one long keyword — they advance in lockstep, so
`front == rear` holds at *every* enqueue and control never enters the checked arm at all. For a
single keyword the bounded arm is dead code. `rear` walks off the end of the array and
`*rear = (q = s->nst)` writes past the frame. A host build of this source under
AddressSanitizer, on one 450-character keyword:

```text
ERROR: AddressSanitizer: stack-buffer-overflow
WRITE of size 8 ... in cfail
  [32, 3232) 'queue' <== Memory access at offset 3232 overflows this variable
```

That build is the only thing in this tree that could ever have said so. `rootfs_fgrep_size`
weighs `const + text + data + bss` and a frame is none of them; the 4,096-word stack is
[../README.md](../README.md) §6's third ceiling, the one nothing checks; and `b6cc` guest
programs never see cppcheck or `-Wall -Werror`, which are the host half of the build.

**It was also a second ceiling, tighter than the documented one, wearing the documented one's
diagnostic.** `overflo()` says `fgrep: wordlist too large` whether it is out of states or out of
queue. Measured, N distinct keywords of length L:

| keyword length | keywords accepted | states used of 3000 | what stopped it |
|---|---|---|---|
| 3 | 399 | 815 | the queue |
| 5 | 399 | 1,613 | the queue |
| 10 | 331 | 2,993 | the table |
| 20 | 157 | 2,991 | the table |
| 40 | 76 | 2,968 | the table |

Below about eight characters a keyword, the `3000` in `grep.1.umm` and in the section above
described a limit the program never reached: what a user hit was 399 keywords, or about 400
characters of one. `QSIZE` appeared in no `.md` file in this tree, and it is what
`cmd_fgrep_toomany` had been asserting all along — 400 keywords of `keyword%08d` build **856
states of 3000** and die in `cfail`, never in `cgotofn`. The case passed, with the right
message, for the wrong reason, for the whole of task C5c.

**The fix is a proof and not a larger constant.** Every state is enqueued exactly once: a state
is the `nst` of exactly one node, `nst` is a fresh `++smax` each time, and the root is nobody's
`nst`. So at most `MAXSIZ - 1` things are ever enqueued, a *linear* queue of `MAXSIZ` cannot
wrap and cannot overflow, and all of the circular arithmetic goes — with both of `cfail`'s
`overflo()` calls, the one that was there and the one that was missing. It is bss and not an
automatic, because 3,000 words in a frame is precisely §6's unchecked ceiling, whereas in bss
`rootfs_fgrep_size` weighs it on every build. The `floop`/`qloop` logic underneath is v7's, byte
for byte; only the queue moved.

Two things generalize. **A bound test that is not on every path is not a bound test, and reads
exactly like one** — the code above looks careful, and the `overflo()` call in the checked arm
is what makes it look careful. And **when a scratch array's occupancy is provably bounded by a
table that is already bounded, size it from that table and delete the check: a check you can
delete cannot be half-written.** That is the `-b` lesson two sections up applied to a bound
instead of a divisor — there, two commands agreed once the division went rather than the
divisor changing; here, the two arms agree once there is nothing to wrap.

And one for the harness. **A test that asserts a diagnostic asserts the string, not the cause.**
`toomany` is now 250 keywords of fifteen characters, which really does fill the table, and it
has three companions: `toolong`, one keyword of 3001 characters, so the same ceiling is reached
from the length side where it cannot be misread as a keyword count; and `manykeys` (500
keywords) and `longkey` (450 characters in one), the two lists v7's code rejected and this one
takes. Four cases, two directions, one accepted and one refused in each — which is
[../README.md](../README.md) §9's rule that a stated bound gets a program run against it, and
the reason `grep.1.umm`'s BUGS can now say there is no limit on the number of keywords at all.

`longkey`'s text file carries the assertion that matters: the line above the match is the
keyword less its last character, 449 of the 450. A corrupted failure-link queue shows up as a
**false positive**, and a case that only demonstrates a match cannot see one.

## And underneath both of those, the failure function was computing the wrong links

The queue was how `cfail()` walked the trie. What it computed while walking was also wrong, and
this is the one that cost answers rather than keywords.

`fail(q)`, for `q` the `c`-successor of `s`, is the first state on **`s`'s failure chain** that
has a `c`-transition — `fail(s)`, then `fail(fail(s))`, and so on down to the root. v7 tried
`fail(s)`, walked its `link` chain of alternative transitions out of that one state, and then
went **straight to the root**:

```c
} else if ((state = state->link) != 0)  /* alternatives from THIS state */
    goto floop;
else if (bstart == 0) {
    state = 0;                          /* v7: ...and then straight to the root */
    goto floop;
}
```

`state->fail` never appears. One hop would have sufficed if the goto function were a filled-in
DFA, where every state answers every character; this one is the plain trie goto, so the chain
has to be walked. Any link needing two hops or more came out pointing too shallow — and because
`out` is propagated along `fail`, a keyword ending at such a state was **never reported**.

Three keywords and a four-character line are the whole of it. `bd`, `debdb`, `ebb`; the line
`debd`:

| | |
|---|---|
| correct | `fail(debd)`: `fail(deb)` = `eb`, no `d`; **`fail(eb)` = `b`**, which has a `d` → `bd`, a keyword. Match. |
| v7 | `fail(deb)` = `eb`, no `d`, chain exhausted → root → `fail(debd)` = `d`, not a keyword. **No output.** |

The skipped hop is `fail(eb) = b`. v7's `fgrep` printed nothing for a line containing `bd` while
`bd` was in its keyword list.

**This is a silent wrong answer from a search program**, which is the failure this file and
[../col/README.md](../col/README.md) and [../od/README.md](../od/README.md) keep arriving at
from different directions — worse than a fault and worse than a diagnostic, because nothing
distinguishes it from an honest absence of matches. And it is not exotic: **about one random
keyword set in a hundred hits it**, measured by generating 2,000 sets over small alphabets and
diffing against `grep -F`. Two or three short keywords sharing suffixes is all it takes, which
is what a real `-f` list of words looks like.

The fix walks the chain — `for (state = s->fail;; state = state->fail)`, stopping at the root —
and the corrected program agrees with `grep -F` on **12,000 comparisons**: 2,000 random keyword
sets × six flag combinations (plain, `-x`, `-v`, `-c`, `-n`, `-v -x`). v7's code disagreed on
1% of them. `cmd_fgrep_failchain` is the case, and it is the minimal one above.

**How it survived the port is the part worth keeping.** Nineteen hand-written cases passed, and
they pass unchanged now — because every one of them was built from keywords a person chose to
illustrate a *flag*, and such keywords do not overlap each other in the way that makes a fail
chain two hops long. The bug lives in the interaction between keywords, which is precisely the
thing hand-written cases do not vary. **A case built to demonstrate a feature exercises the
feature and not the machinery underneath it**; what found this was 2,000 keyword sets nobody
chose, checked against an oracle. Where a program has a known-good reference implementation and
a cheap input generator — and `fgrep` has both — differential testing is worth more than the
next ten hand-written cases, and it is what task C5e should point at `sed`'s regex engine.

## And one claim in `grep.1.umm` that was wrong twice over

v7's BUGS said "Lines are limited to 256 characters; longer lines are truncated." Neither number
is right for either program, and the first draft of the corrected page was wrong too — it said
`fgrep`'s limit was 512, from reading the two 512-byte halves of the ring.

Measured: `grep` truncates at **511** and treats the remainder as a *new line*, so a long line is
reported more than once (`grep -c x` says 2 for one 605-byte line). `fgrep` loses nothing until
**1024**, the whole ring, because `nlp` holds the start of the line being scanned and the head is
lost only when the reader laps it — a 1,025-byte matching line prints exactly one byte.

[../README.md](../README.md)'s standing rule is that claiming more than the fix does is worse
than carrying the bug. It applies to *limits* too, and the cheap check is the one that was
missed: a page that states a bound should have a program run against that bound before the
sentence is written.

## What else was fixed rather than carried

* **An unknown flag was not an error.** Both programs printed a diagnostic and went round the
  loop *without consuming the argument*, so `grep -Q pattern file` searched for `pattern` and
  reported success — and `fgrep --` could not terminate at all. Both exit 2 now, which is the
  status `grep.1.umm` has always promised for "syntax errors".
* **`-c` ignored `-h`.** The count path printed a file-name prefix on `nfile > 1` where the
  ordinary path also asked `hflag`. One line, and the two paths agree now.
* **No usage line.** With no expression, v7 exited 2 in silence. The status is unchanged, so
  nothing that reads `$?` can tell the difference.
* `long ftell();` — §1's re-declaration of a library function, in `succeed()`.
* `fgrep`'s unused `register char ch;`, its `#include "stdio.h"` in quotes, and its complete
  absence of declarations for `open`, `read`, `close` and `exit`.

## The harness grew a `set -f`, which nothing had needed

`scripts/run-prog-test.sh` expands a `.args` line unquoted, which is how a case gets its
arguments split. It also globbed them, and nobody had noticed because no argument in thirteen
filters' worth of cases was a pathname pattern. **A regular expression is one**: `[bg]`,
`g*amma` and `^[^abg]` are all patterns the shell would happily replace with a file name that
happened to be sitting in the test's working directory. One word of `set`, and it is the same
shape as task C3's finding — [../README.md](../README.md) §9's *when a limit is stated of a
harness rather than of the machine, check which it actually is.*

What is still not possible there is a pattern **containing a space**, the line being split with
no quoting available. That is a real gap and it is paid where such gaps are paid: `kernel/test/
filters.sh` runs `grep 'вет мир'` under a shell that quotes, which is a sixth item on that
test's list of things `b6sim` cannot say.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `grep` | 100 | 3,983 | 220 | 1,323 | **5,626** |
| `fgrep` | 86 | 3,670 | 212 | 16,051 | **20,019** |

Out of the 28,672 words §6 allows. `fgrep` is **the largest program on the image**, by more than
`fsck` and `sh` together, and every word of the difference is `w[]` and now `queue[]` — 15,000
of the 16,051 words of bss between them. It was already the largest at 17,048, before the queue
was sized from `MAXSIZ`; the sentence that used to stand here said "second-largest, after
`fsck`" and was wrong when it was written.

## And §6's ceiling that nothing checks is the one this port actually hit

The three ceilings §6 names are the image (28,672 words), the pointer reach (32,767) and **the
4,096-word stack, which nothing checks.** `rootfs_grep_size` clears the first two with room to
spare. The third is where `grep` fell over, and it was found by trying rather than by reading.

`advance()` recurses once per star operator that consumes something, so its depth is bounded by
the number of stars in the *compiled pattern* — not, as it looks at first, by the length of the
line, because a star that consumes nothing takes the `continue` and does not recurse. `b6disasm`
puts one frame at **153 words** (`15 utm 0231` in its prologue), so about twenty-six of them fill
the stack. A pattern with twenty-six stars is odd but it is not absurd, and `ESIZE` going from
256 to 512 — `ed`'s reasons: a class costs 32 bytes now and a UTF-8 letter in a pattern costs
four — doubled how many the compiler will accept.

**What made this worth fixing rather than documenting is what the failure looked like.** Run
`grep 'a*b*a*b*…'` over a line it matches and step the star count up:

| stars | v7's code, on this machine |
|---|---|
| ≤ 20 | the right answer |
| ~40 | **the wrong answer — no output at all, for a line that matches** |
| ~52 | `b6sim: error: Jump to zero`, or a stack protection violation |

The middle row is the whole argument. A machine fault is a bad failure and a diagnostic is a good
one, but a **silent wrong answer from a search program** is the failure this tree keeps writing
down: `od`'s truncating `putn()`, `col`'s `P?QP8P2P5Q`, `sh`'s dropped eighth bit. So `advance()`
is a counted wrapper around v7's body now, with `MAXDEPTH` 16 and a `grep: expression too
complex` beyond it; `cmd_grep_deep` is the case. A real expression recurses two or three deep.

The general form, and it is the reason to try a bound rather than reason about one: **a recursion
whose depth is set by the input is a fourth ceiling of its own, and the region just past the
ceiling need not fault.** [../col/README.md](../col/README.md) found the heap the same way one
task earlier — the size ctest weighs `const + text + data + bss` and can see neither. And
`fgrep` reached the same ceiling from the other direction, with a fixed array rather than a
recursion: the section on `cfail()`'s queue is the second half of this finding.
