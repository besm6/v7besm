# `expr`, and what a grammar costs on this machine

Task C11, and the task C10 existed for: [`../TODO.md`](../TODO.md) named `expr` the smallest
consumer of `b6yacc` and the one that would prove it end to end. It does — `y.output` reports
**0 shift/reduce and 0 reduce/reduce conflicts** over 22 rules and 48 states, and the generated
parser compiled and ran with no change to the skeleton — but the interesting half is what the
grammar itself demanded, which is the first section below.

The C11 pass over [expr.y](expr.y) is described in its own header. Four things the port
*taught* are here, because none of them is about this program alone.

## `YYSTYPE` is a macro, not a type name

Every semantic value in this grammar is a `char *`. `b6yacc` emits

```c
#ifndef YYSTYPE
#define YYSTYPE int
#endif
YYSTYPE yylval, yyval;
```

([`../yacc/y2.c`](../yacc/y2.c)), so a grammar declares its value type by defining `YYSTYPE`
before the first `%%`. **Two of the three obvious ways to do that are wrong here, and neither
says so.**

`#define YYSTYPE char *` expands that second line to `char * yylval, yyval;` — `yylval` is a
pointer and **`yyval` is a plain `char`**. Every reduction then truncates its result to one
byte. Nothing diagnoses it; `yyv[YYMAXDEPTH + 1]` and `YYSTYPE *yypvt` in the skeleton both
survive the expansion, so it is exactly one broken declaration out of four.

Leaving `YYSTYPE` an `int` and casting — which is what v7 did, `yylval = p` with `p` a
`char *` — **cannot work on this machine at all**. A `char *` is a fat pointer with the byte
offset in bits 47–45 and an `int` occupies bits 41–1, so the cast floors the pointer to its
word ([`../README.md`](../README.md) §2). It would appear to work for every string that
happened to start on a word boundary, which is most of `argv`, and lose the offset on the rest.

What is left is a typedef:

```c
typedef char *charptr;
#define YYSTYPE charptr
```

`%union` is the fourth way and would also have worked, but at C11 **nothing in this tree had
exercised `b6yacc`'s union path** and `expr` has exactly one value type, so it would have bought
a `<s>` tag on every symbol and a `.s` on every `$$` in fourteen actions to buy no type checking
at all — while putting three aggregate assignments into a guest-compiled skeleton for the first
time. C11 was the wrong place to find that out. It was retired later and on a grammar of its own,
[`../yacc/rootfs/calcu.y`](../yacc/rootfs/calcu.y), ahead of C14 — whose
[`../make/gram.y`](../make/gram.y) is the tree's one real `%union` and now works.

## `\}` ends a `%{ … %}` block

Not in a string, not in an action — **in a comment**. `cpycode()` treats `\}` as a terminator
exactly as it treats `%}`, so this line inside the header comment

```c
 * of the engine is the only one with \{n,m\}, so the width appears in eight places.
```

closed the declarations block in the middle of a sentence and handed the rest of the comment to
the grammar parser, which reported `fatal error: syntax error, line 17`. The line it names is
the line the block *ended* on, not the line the mistake is on, and the mistake does not look
like one. It is v7 yacc's own behaviour and is left alone; the header says "an interval repeat"
in words instead.

## The third copy of v7's regexp engine, and the only one with an interval

[`../grep/README.md`](../grep/README.md) is §11's worked example and [`../sed/sed.h`](../sed/sed.h)
carries the constants; this is the same code a third time. Widening a character class from 128
bits to 256 costs **eight** edits here where those two needed five, and the two extra are the
ones no other copy has:

| | v7 | here |
|---|---|---|
| room for a class | `&ep[17] >= endbuf` | `&ep[CCLSIZE + 1] >= endbuf` |
| clear it | `for(i=0;i<16;i++) ep[i]=0` | `memset(ep, 0, CCLSIZE)` |
| negate it | `cclcnt < 16`, `ep[cclcnt] ^= -1` | `cclcnt < CCLSIZE`, `^= 0377` |
| step over it, three times | `ep += 16` | `ep += CCLSIZE` |
| **the interval's operand** | `getrnge(ep + 16)` | `getrnge(ep + CCLSIZE, …)` |
| **a class and its interval** | `ep += 18; /* 16 + 2 */` | `ep += CCLSIZE + 2` |

Four `& 0177` masks on the match side go away rather than being widened, `c >> 3` landing in
0..31 by construction once the class is 32 bytes.

**The failing assertion is not the one a reader expects.** The compile side never masked the
character at all, so with a sixteen-byte class a pattern byte of `0320` sets a bit at `ep[24]` —
*forward*, into bytecode `compile()` has not written yet. A class containing a multi-byte letter
therefore ends up **empty**, and matches nothing rather than matching something wrong. So the
case that pins the width is a positive one, `expr Ы : '[Ы]*'` answering `2`; and the mask needs
a negative control of its own, `expr привет : '[P]'` answering `0`, since `0320 & 0177` is `P`
and a masked match side would have said `1`.

Two more findings in the same 400 lines, both of them §2's:

* **A permanently null pointer compared against a live one.** `locs` is set nowhere and the
  `star:` label tested `if (--lp == locs) break;`. A fabricated pointer matches no real fat
  pointer here, so the test was dead on this machine before it was dead on any other. It is
  gone, and `step()`, `circf`, `loc1` and `sed` with it — all of them written and never read
  once the one caller `expr` has is accounted for.
* **A `'\0'` stored through a pointer that is sometimes a literal.** `substr()` terminated its
  result in place, and its first argument is not always `argv`: `conj()` and `rel()` return
  `"0"` and `"1"`. A non-`PURE` program has a writable const segment here, so it corrupted
  silently. It returns a copy now, which also removed v7's other defect in the same four lines —
  `while (--si) if (*v) ++v;` does not terminate for a starting position of zero.

## `<=` computed `>=`

```c
	case GEQ: i = i>=0; break;
	case LEQ: i = i>=0; break;
```

`expr 1 '<=' 2` answered `0`. It is a one-character fix and it is written down here because of
what it took to *find*: nineteen hand-written cases over the ordinary surface had all passed,
and the operator that was wrong is the one nobody thinks to check twice. Its companion turned up
only because the first sent somebody reading the operand tests — `rel()` asked
`ematch(r1, "-*[0-9]*$")` of the left operand and `ematch(r2, "[0-9]*$")` of the right, so
`expr -2 '<' -1` fell through to `strcmp` and answered `0`, and `arith()` asked the second
question of both operands, so `expr -1 + 1` was `non-numeric argument` — which is this
program's single most common use, on a shell variable that has gone negative. One `isnum()`
replaces all four, and the regular expression engine leaves the arithmetic path entirely.

Both are divergences from v7 and are declared in [expr.1.umm](expr.1.umm) as
[`../README.md`](../README.md) §10 requires, with the other two: a paren is an operator only
when it is the whole argument (v7 tested `*p`, so `expr '(3)'` was a syntax error), and division
by zero is diagnosed. That last is not defensive coding — `b6sim` answers an unguarded `1 / 0`
with `error: Division by zero` and the machine raises the exception under the kernel, so v7's
unchecked `/` faults here rather than returning a wrong number.

## The measurements

**Stack.** `advance()`'s counted wrapper is `grep`'s shape and `MAXDEPTH` is measured, not
copied. `b6disasm` puts `match_re()`'s frame at 206 words (`15 utm 0316`) and the wrapper at 4,
so one level costs 210. Underneath sit `yyparse()` at 400 — `short yys[YYMAXDEPTH + 1]` is 151
words, there being no sub-word storage — plus `match()` and `ematch()` at 21, and **the
diagnostic itself needs `_doprnt`'s 281**. So the arithmetic is `(4096 − 421 − 281) / 210`, or
sixteen levels with nothing to spare.

A probe with the ceiling lifted to 60 agrees and shows why the margin matters: `expr aaaa :`
against a pattern of *n* `a*` groups is right through **16**, returns a **silent wrong answer**
— the null string — at **18**, and only faults from **20**. Twelve levels leave about 865 words
of margin and allow eleven stars in a pattern, where the manual page's own example uses two.

**Size.** 6,722 words of the 28,672 a program may have — 119 const, 4,575 text, 814 data,
1,214 bss — against `grep`'s 5,656 for the same engine with more flags. Rather more than half
of it is stdio: `expr` runs once per `` `expr $a + 1` `` in a shell loop and pays ~2,500 words of
common text and ~1,030 of bss to print one line. Converting its four output sites to `write(2)`
the way `test` does is a real option and is not taken here; the free blocks are there and the
`sprintf` in `numstr()` would have to be hand-rolled to go with them.

**The name check.** `b6nm` over the finished binary reports no `index` at all: the program's own
is `idx()` now, so `index(3)` is not pulled out of libc and there is nothing for `b6ld` to pick
between. It would not have diagnosed a choice.

## What this harness cannot say

`b6_progtest` splits an `.args` line on whitespace with no quoting, so **an empty argument is
not expressible** — and there is no second world to put one in, `kernel/test/` having only
`boot`, `multi` and `core` left and none of them a filter. Three answers are therefore checked
by hand and recorded here instead:

```
expr '' = ''        1               both operands are strings, and equal
expr abc : ''       expr: RE error  status 2
expr '' '|' x       x
```

The second is a fix in its own right. v7 decided an empty pattern by looking at `expbuf`, which
is `static` and reused across calls, so whether it was diagnosed depended on what had been
compiled before it.

## What did *not* need saying

The rest is ordinary. `%D` was in `arith()` and nowhere else — the whole output of `expr a + b`
was the two characters `%D`, since an unknown conversion is echoed and consumes no argument. The
`long`s are `int`s and `atol` is `atoi`. `compile()`'s bound test named `&expbuf[512]` against a
256-byte buffer and so could never fire inside the array it was bounding; `ESIZE` is 512 now and
`ematch()` passes `&expbuf[ESIZE]`. `advance()` had no `default:` arm, so a bad opcode walked
`ep` forward through the compiled expression until it landed on something legal. None of these
is a finding; they are what §1, §3 and §6 say to look for, and they were all there.
