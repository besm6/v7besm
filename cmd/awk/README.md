# awk, and the program whose ceiling is its own heap

Task C17. 2,428 lines of v7 C across nine files, a 272-line yacc grammar and a 173-line lex
scanner — **the only program in this tree that is both**, and the last of C10's risks. The
grammar and the scanner needed no change to *generate*: `b6yacc` still reports 95 shift/reduce
and 0 reduce/reduce over `awk.g.y`, and `b6lex` still reports the same seven numbers over
`awk.lx.l` that `cmd/lex`'s `besm6` profile was measured against. Everything below is about
what happened after they compiled.

## The heap is the ceiling, and it is not the checked one

`rootfs_awk_size` weighs `const + text + data + bss` against 28,672 words, and awk passes it
with room to spare. That number turned out not to matter. The program break starts at the first
page boundary past bss and grows to the stack at `070000`, so **what is left over is the entire
heap** — and awk puts its parse tree, its symbol table, every string value, every array and
every DFA state there.

The first version that linked was 26,571 words, which left **one page**. It parsed `{ print $2 }`
and died in `malloc` on anything larger. The arithmetic is brutal and granular: a word saved is
a word of heap, and 300 words saved at the right moment is a whole page.

| | image | heap |
| --- | --- | --- |
| first link | 26,571 | 1,024 (1 page) |
| v7's tables cut to a `besm6` profile | 24,358 | 4,096 |
| one `sscanf` removed | 23,447 | 5,120 |
| eight-bit tables (below) | 23,877 | 4,096 |
| profile trimmed again, `atoi` removed | 23,482 | 5,120 |
| the float gates | 23,574 | 4,096 |
| `MAXNODE` 192 → 168 | **23,518** | **5,120** |

The last row is the whole lesson: ninety words of range checking cost a page, and twenty-four
tree-stack slots nobody can reach bought it back.

Two of those rows are worth keeping. **`sscanf` cost 797 words** — `doscan.o`, linked entire
for the one `sscanf(yytext+1, "%o", &v)` that reads a three-digit octal escape in a regular
expression. Three subtractions replace it. `atoi` was another 93 for a run of digits after `$`.
Where a program calls one libc routine once, read what the routine weighs before keeping it.

And the tables are a **size profile**, in the sense `cmd/cpp`, `cmd/as` and `cmd/lex` use the
word: `MAXLIN` and `NSTATES` 112 rather than v7's 256, `MAXFLD` 40 rather than 100, `RECSIZE`
768 rather than 2,560. The effect is measurable in the only unit that matters — an awk program
holding distinct array keys managed 154 of them at four pages and 248 at five.

**The regular expression is heap-bound before it is table-bound.** `cgotofn()`'s `add()` calls
allocate a follow set per position, so a 60-character pattern compiles and an 80-character one
reports `regular expression too long` from a failed `malloc` rather than from any of the
bounds. That is the honest ceiling and the manual page says so.

## One fat pointer

`cmd/README.md` §2 warns that a cast to a thin pointer floors a fat one to its word. awk has
**exactly one** fat value in flight: `cclenter()`'s expanded character class, a `char *` that
v7 stored in `node *narg[1]` and read back with `(char *) right(cp)` in seven places. Every
other value on the parser's stack is a small integer or a `cell *`, which are word addresses
and survive.

A spike settled what actually happens, because it was worth knowing rather than assuming:

| what | result |
| --- | --- |
| `malloc` result → `node *` → `char *` | exact |
| `chars + 3` → `node *` → `char *` | **byte offset lost** |
| `chars + 3` → `int` → `char *` | exact — an `int` cast is a raw move |

So v7's code worked *by accident*: `cclenter()` returns `tostring(chars)`, always a fresh
`malloc` at byte offset 0, and the flooring loses nothing. `cmd/bc/README.md`'s judgement
applies — "happens to be exact" is the wrong footing for the one operation the regular
expression engine depends on. The tree now holds an `int` handle into a `cclstr[]` table in
`b.c`; `cclstash()` takes ownership from the scanner and `cclget()` hands the string back.
A table and not a single global, because yacc reads its lookahead before it reduces and
`/[ab][cd]/` has two classes live at once.

**`YYSTYPE` is `node *`**, which is what RetroBSD's ANSI-ised awk also concluded. It wants a
**typedef** behind it: yacc writes `YYSTYPE yylval, yyval;`, and with `#define YYSTYPE node *`
the second declarator is a bare `node`. That miscompiles as "Cannot convert type for
assignment" and is the only interesting thing about the change.

## Four walks that could not keep a ceiling

`b.c`'s `penter()`, `freetr()`, `cfoll()` and `first()` recurse over the regular expression's
parse tree, and `awk.g.y:205`'s `r r %prec CAT` is left-associative, so *N* characters build an
*N*-deep spine. Measured here, before anything changed: `cfoll` 51 words a level, `first` 70,
`follow` 29 — and `cmd/egrep/README.md` had already run the same shape off the same stack at
80 characters. A `MAXDEPTH` that admitted an ordinary regular expression would already be over.

All four are iterative now. Three of them share one worklist, because `makedfa()` runs them one
after another and never overlaps them. `first()` needs a state machine, since it has a return
value and v7's `&&` in the `CAT` arm short-circuits — a left child that is not nullable answers
for the node and the right child is never visited; `cmd/egrep`'s `cstate()` is the worked
example and this is the same three phases. **`follow()` needed no stack at all**: every arm of
v7's recursion was a tail call up the parent chain, so it is a `for (;;)` with one assignment.

`cgotofn()` does not recurse, so §6's other move applies: its eight tables were **1,488 words of
automatic storage** and are file-scope now.

| | before | after |
| --- | --- | --- |
| `cgotofn` | 1,488 | 463 |
| `penter` | 43 | 53, flat |
| `cfoll` | 51 | 66, flat |
| `first` | 70 | 128, flat |
| `follow` | 29 | 30, flat |

`execute()` is the one recursion that **does** get a ceiling, and `cmd/make`'s `doname()` is the
precedent: its depth is the awk *program*'s nesting, which the person writing it chose. It costs
about 98 words plus the operator's own frame, and probing found the stack gone at 22 levels, so
`MAXDEPTH` is 15 and every deeper program is now a diagnostic rather than `Division by zero`
several instructions later.

## Bounds v7 did not have

Five, all of them reachable:

* **`penter()` had no bound at all** — `point[line++]` walks off the end of three arrays.
* `cgotofn()`'s `if (n >= NSTATES)` writes `state[NSTATES]`. `cmd/egrep/README.md:112` records
  the identical off-by-one.
* `cclenter()`'s closing `chars[i++] = '\0'` is outside the guard.
* `getrec()`, `recbld()`, `print()` and `format()` each test their buffer **after** the write.
* **A double free.** `cfoll()` shares one follow set between leaves whose sets are equal, and
  `freetr()` freed one per leaf; `cgotofn()` had already freed `state[0]`, which *is* `foll[0]`.
  Ownership is tracked in `follown[]` now and the sets are freed once, in `makedfa()`.

And two functions fell off the end while returning a struct by value — `nullproc()`, which
`notlegal()` gates, and **`instat()`, which runs on every normal exit from `for (v in a)`**.
That one is a live crash, not a theoretical one, and `cmd_awk_forin` is the case for it.

## The float, and the fault that is not a value

awk is the first program in `cmd/` to link `-lm`. `lib/libm/README.md` supplies the rule that
matters: **overflow is a fault and the program dies**, so every gate goes before the
arithmetic. Four things followed.

* `isnumber()` — renamed `isnumstr()`, per §1 — *is* the overflow gate, because
  `lib/libc/gen/atof.c` has an underflow gate and none for overflow. Its `MAXEXPON` was the
  PDP-11's 38 and is `LOGHUGE` now. It also **never tested the sum**: `99e18` passed the digit
  count and passed the exponent test and then faulted, so `d1 + expval` is tested too.
* The scanner ran `atof()` on a NUMBER literal with no gate in front, so `BEGIN { x = 1e300 }`
  killed awk before it read a byte of input. It is diagnosed.
* `%` was `i - j*(long)(i/j)`, which loses every bit of the quotient above 2^40, and `int()`
  was `(awkfloat)(long)f`, which is undefined there. They are `fmod()` and `trunc()` —
  `lib/libm/README.md` rewrote `fmod` from fdlibm for exactly this and pins it on `fmod(2^42, 3)`.
* `%.20g` for an integral value is silently `%.12g`, since `doprnt.c` clamps a float to twelve
  significant digits, so a 13-digit integer came back in e-notation. An integer below 2^40 is
  printed as an integer now.

**One divergence stands.** 2^40 exactly, and every integer above it, prints through `OFMT`
rather than in full: an `int` is 41 bits, so the value does not survive the cast, and `%g`
cannot carry thirteen digits anyway. Below 2^40 the output is the host's, digit for digit.

## Eight bits

`b.c`'s `symbol[]`, `isyms[]` and `ssyms[]` are indexed **by a character** and written through
(`isyms[*p] = 1`), so a Cyrillic byte stored eighty entries past the end of a 128-entry table —
`cmd/README.md` §11's `m4 type[]` shape, with `cgotofn()`'s frame underneath it. And the
`for (k = 1; k < NCHARS; k++)` loops gave `.` and `[^…]` no transition above `0177` at all.
`NCHARS` is 257, and `fatab` is sized `2 * NCHARS + 1` because it always was — v7's `257` is
`2*128 + 1`, a bound that widening would have turned from latent into active.

**`HAT` moved from `0177` to `256`.** It is not a flag bit but a stolen code point: awk compiles
`^` inside a regular expression to a pseudo-character and excludes it from every enumeration.
`0177` is a byte a regular expression may contain, so §11's answer is to move the mark, and 256
is outside the byte range where nothing can collide with it. `.` matches a literal `0177` now,
which v7 could not do. `cclstr[]`'s strings are C strings, so 0 is not available for the mark:
`match()` compares the transition character against `*p` on the iteration where `*p` is the NUL.

## The three generated files

v7's makefile built awk out of three things that are not in the source tree:

* **`awk.h`** was a copy of `yacc -d`'s `y.tab.h`. The sources include `y.tab.h` directly now,
  through `CFLAGS -I${AWKGRAM_SRC_DIR}`, and the name `awk.h` was free for what used to be
  `awk.def`. That generated header must be named in `KHDRS` — it is `b6_obj()`'s only
  dependency list — or the hand-written units compile before `b6yacc` has run.
* **`token.c`**'s 77-row name table was regenerated from `awk.h` by an `ed` script. `ptoken()`
  was called only under `DEBUG` and `tokname()` only from `proc.c`, so with both gone the table
  had no reader and the file is deleted. If a diagnostic ever wants to name a token,
  `proctab.c`'s rows already carry one per token and v7's third column was exactly that string.
* **`proctab.c`** was the output of compiling `proc.c` into a host program and running it.
  It is hand-written now and filled by a loop at startup. It cannot be a sparse static
  initializer: **`b6lower` ignores designated initializers** and fills positionally, silently,
  so `[TOK - FIRSTTOKEN] = fn` would put every handler in the wrong slot.

The dispatch itself was the port's biggest unknown, and a spike retired it before any of the
work: `obj` is two words, so every one of the twenty-odd handlers returns through a hidden
pointer, reached **through a function pointer**. Nothing in this tree had done that. It works.

## What was cut

`-S` and `-R` with `freeze.c`: they dumped and reloaded the data segment with
`write(fd, (char *)0, len)` over `sbrk(0)`/`brk()`, and neither address 0 nor a break value
means what it meant on a PDP-11. `-d` and the whole `dprintf` scaffolding, which was never
compiled here and cost a four-argument macro at ninety call sites. And `logit()`, which appended
every command line to `/crp/pjw/awkhist/awkhist` — Peter Weinberger's usage log at Bell Labs —
through a two-word `time_t` this machine has not got. None of the three is in `awk.1`.

`print | "cmd"` is **kept**: libc has `popen()` and `/bin/sh` is on the image. It is also the
first thing in the next section.

## What this harness cannot say

77 cases under `b6_progtest`, of which **69 agree with the host's own awk byte for byte** —
`cmd/README.md` §9's third oracle, replayed over the whole suite. The eight that do not are the
diagnostics, and each is one this port introduced or reworded. Every case's program is a `-f`
file, because `.args` is split on whitespace with no quoting and an awk program without a space
in it is not a representative one.

What is left over, and there is no second world to put it in — `kernel/test/` has three tests
and none of them runs a filter:

* **`print | "sort"`.** `popen()` execs `/bin/sh`, and the `/bin/sh` b6sim reaches is the build
  machine's, not a BESM-6 `a.out`. A case would pass while asserting nothing.
* **The content of a redirected file**, for the same reason a case cannot name one usefully.
* **A program or a file name containing a space**, which is how awk is actually used.
* **The 4,096-word stack.** `rootfs_awk_size` weighs four segments and a frame is none of them;
  the table above is the measurement, read out of the `.dis` that `b6_prog()` writes.

Checked by hand under the booted kernel:

```
# echo 'alpha beta' | awk '{ print $2, $1 }'
beta alpha
# awk 'BEGIN { for (i = 1; i <= 3; i++) printf "%d squared is %d\n", i, i*i }'
1 squared is 1
2 squared is 4
3 squared is 9
```

## What did not need saying

`awk.def` was §1's classic trap by reputation and was not one in fact: every object in it was
already `extern`, and `make`'s `defs.h` had the same clean bill. `%D` and `%O`, which are still
turning up in v7 sources, are absent from all nine files. And the grammar's 95 shift/reduce
conflicts are v7's own and did not move — the whole point of C10a having pinned them.
