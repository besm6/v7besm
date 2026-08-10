# `cmd/lex` — the lexical-analyser generator

Task **C10b** ([`../TODO.md`](../TODO.md)): the host tool `b6lex`, the scanner skeleton it copies,
and the `b6_lex()` CMake helper. C10d puts the same sources on the image as `/usr/bin/lex` and
`/usr/lib/lex/ncform`.

**This directory is the second exception to [`../README.md`](../README.md)'s "the sources are
already here"**, [`../yacc/`](../yacc/) being the first. `header.c`, `lmain.c`, `sub1.c`,
`sub2.c`, `parser.y`, `ncform`, `ldefs.h` and `once.h` were fetched from RetroBSD
(`src/cmd/lex`), which is very nearly v7's lex — K&R and all — but carries the 4.xBSD
`<paths.h>` fixes and a few corrections. **`nrform`, the Ratfor skeleton, was not fetched.** The
manual page is v7's, converted with `b6man2umm`.

`parser.y` is compiled by `b6yacc`, so this is **the first consumer of C10a that is not a test,
and the first host one**.

## What the port had to change

Each item is a decision rather than a transcription, and two of them are the reason the tool
did not run at all.

### The character set that was never there

`ldefs.h` gated `CWIDTH`, `CMASK` and `ASCII` on `# ifdef unix`, with `gcos` and `ibm` arms
beside it, and `NCH` on `ASCII`/`EBCDIC`. `b6cpp` predefines no `unix`, so **`NCH` was undefined
and nothing here compiled**. That much was expected.

What was not: **`CWIDTH` and `CMASK` are dead code.** They appear at their own `#define`s and
nowhere else in 3,300 lines. So [`../TODO.md`](../TODO.md) was wrong to say this lex masks bit 7
off every input byte — it masks nothing, and the truth is worse. `NCH` was 128 while `gch()`
returns a byte, so `parser.y`'s `symbol[c] = 1` inside a character class **wrote past
`symbol[]` into `cindex[]` and `match[]`** on the first byte above `0177`. §11's worst shape is
not a mangling here, it is an overrun. The whole gate is gone, `NCH` is 256, and `CWIDTH`/`CMASK`
went with the arms that defined them.

`ZCH` went too. `%t` set it from its argument and then **clamped it up to `NCH`**, and every use
compared it against `NCH`, so once the alphabet is the whole byte `ZCH` can only ever equal
`NCH`. It was a variable with one possible value; `NCH` took its place, and `header.c`'s
`ZCH > NCH` arm and `lmain.c`'s dead `/share/lex/ebcform` went with it.

### An `int` is not a `char *`, and this program does it three ways at once

**This is the change the port exists for, and it is not a portability nicety: the imported lex
segfaults in `slength()` on the third scanner it is given.** §2, on a machine where a pointer is
wider than an `int`.

`YYSTYPE` is `int`, and `yylex()` assigned `yylval` a `char *` four different ways — `buf`,
`token`, a cursor into `ccl[]`, a cursor into `slist[]`. Those values reach `mn1()` and `mn2()`,
**whose parameters are `int`**, so the pointer is truncated *at the call* before `int *left` and
`int *right` ever see it. And `cfoll()` then **re-points `left[v]` from `ccl[]` into `pchar[]`**,
so the array holds two different kinds of pointer at two different times.

On the target it is §2's third arena hazard in its purest form: an `int` occupies bits 41–1, a
fat `char *` needs bit 48 and the byte offset in 47–45, so every one of them would be floored to
a word and lose the offset.

They are **offsets** now, and the invariant is written down in `ldefs.h`:

* `RCCL`/`RNCCL` — `left[]` indexes `ccl[]` until `cfoll()` compresses the class, and `pchar[]`
  after it;
* `RSCON` — `right[]` indexes `slist[]`;
* `STR` — the only value that was neither, and it is always one of exactly **two** buffers, so
  it is a selector (`STR_NAME`, `STR_TOKEN`) and `strval()` turns it back into the pointer.

**`%union` was refused.** It would have typed `yylval` and left `left[]`/`right[]` exactly as
broken, which is the actual hazard; it would have cost forty `$n` edits and six `%type` lines;
and it would have made `cmd/lex` the first user of a `b6yacc` feature C10a never tested.

### `error()` and `warning()`

Three untyped parameters handed straight to `fprintf`, called with one, two or three arguments
across 69 sites. `<stdarg.h>` and `vfprintf`, as [`../yacc/y1.c`](../yacc/y1.c) already does.
`error()` is `_Noreturn`, which is what lets `cpyact()` end in a diagnostic.

The same shape twice more: `munput(t, p)` took a **character** when `t` was `'c'` and a
**string** when it was `'s'`, so it is `unputc()` and `unputs()` now; and `yyless(x)` took either
an index into `yytext` or a pointer to it, through one `int` parameter — an index only, clamped.

### The core probe, and the fabricated pointer

`parser.y` allocated one combined block purely to ask whether there was enough core, then freed
it — through an `int`, so **it freed a truncated pointer and aborted the program**. Deleted.
`myalloc()`'s test against `(char *)-1` (§2's first hazard) is deleted too, and `myalloc()` is
**fatal on failure** rather than returning 0 for each of nine callers to test.

### Bounds that were tested after the write

Five, each fixed by moving the test above the store rather than by adding one: the three tree
builders (`tptr > treesize` against `myalloc(treesize)`), `acompute()`'s `extra[left[*p]]` at
exactly `NACTIONS`, `packtrans()`'s `nptr` against `ntrans` (tested only after a whole state had
gone in — it reserves now), `layout()`'s guard band (`startup + NCH` could index one past
`verify[]`), and `token[]`, which `parser.y` filled from four places with no test at all.

### Sizes

`SMALL` and not-`SMALL` collapse to **one block**, as C10a did to `dextern.h`'s three profiles,
under six `_Static_assert`s for invariants that were nowhere written down — that `token[]` and
`ccl[]` each hold a whole character class, that `NOUTPUT` clears two of `layout()`'s guard bands,
that a start-condition number fits the byte of `slist[]` it is stored in. All five bounds stay
overridable per-`.l` with `%e %n %p %a %o` (and `%k`), which is the escape hatch a fixed profile
usually lacks. The `#ifdef besm6` arm is **C10d's**.

### The Ratfor half

`nrform`, `ratfor`, `ratname`, `rhd1`, `rtail`, `rprint`, `bprint`, `shiftr`, `upone`, the
`-r`/`-R`/`-c`/`-C` options, `%r`/`%c`, and every `ratfor ? … : …` ternary. `phead1`/`phead2`/
`ptail` lost the dispatch layer with them.

`DEBUG` went the same way — the dumps, `allprint()`, `strpt()`, `freturn()`, the `SIGBUS` and
`SIGSEGV` handlers. **The `default: warning("bad switch …")` arms stayed**, and that distinction
is the point: the dumps were output, those are diagnostics.

## The skeleton

`ncform` is **data to `b6lex` and source to whatever compiles `lex.yy.c`**, exactly as
`yaccpar.c` is to `b6yacc`; [`../yacc/README.md`](../yacc/README.md) under "The skeleton" is the
worked example. Four things.

**`FILE *yyin ={stdin}, *yyout ={stdout};`** — emitted by `header.c`, and **not a constant
initialiser**. `stdin` is a macro over a pointer *variable*, so this is three hard errors at
`-std=c11 -pedantic-errors`, which is why **no scanner v7's lex generated compiles on a modern
compiler**. `once.h` had the same shape for `errorf`. Both resolve at first use now: `errorf` in
`main()`, and `yyin`/`yyout` through `YYIN`/`YYOUT` in the macros that read them. It is a test
per character, and it is deliberate — `cmd/awk` uses `yyin == NULL` as a sentinel
(`awk.lx.l:159`) and would break if `yylook()` defaulted it instead.

**`yystoff` is a signed `int` offset into `yycrank[]`** — positive for an ordinary state,
negative for one packed by character, zero for one with no transitions. v7 made it a
`struct yywork *` and `sub2.c` emitted **`yycrank+-5`**, a pointer below the base of its own
array, which the skeleton recovered by casting both operands to `int` and reflecting it back with
`yycrank + (yycrank - yyt)`. Three §2 problems in two lines, and the minimal fix would have
removed only the casts. `header.c` emits the struct, `sub2.c` the data, `ncform` walks indices,
and all three had to change together. It also pays off on the target: the walk runs **once per
input byte per fall-back level**, and a relational between two `char *` lowers through `b$pdiff`
where an index is a register test.

**`YYU(x)` is gone.** The skeleton defined its own identity macro and used it everywhere, so
`header.c`'s `U(x) ((x)&0377)` — emitted only when `ZCH > NCH`, which never happened — **had
never applied to anything**. `U()` is unconditional now, and the skeleton calls it. Where `char`
is signed this is the whole of the fix: `yymatch[*yylastch]` with a byte above 127 indexes from
below zero. `char yytext[]` and `char yysbuf[]` stay `char`, because they are the user interface
(`yytext[0]+'a'-'A'` is in the manual page's own example) — **the mask belongs at the index, not
at the storage.**

**`yymatch[]` is `unsigned char` and `YYTYPE` is `unsigned char`.** `verify` and `advance` hold a
state number *plus one*, so they are never negative and one byte reaches 255 rather than 127; v7
keyed the choice at `NCH` and so overflowed a signed `char` at the boundary. The two table
initialisers are also **braced per element** now — a flat list into an array of structs is legal
and `-Wmissing-braces` diagnoses it.

## The contract

The prototypes cannot live in `ncform`. The skeleton is appended **last**, after the tables and
after the scanner's own subroutines, while `phead2()` emits `yylex()`'s call to `yylook()` and
`yywrap()` in the **middle** of the file. So `phead1()` emits them with the header block, which
makes them a contract on every `.l` in this tree:

```c
int  yylook(void);
int  yyback(int *, int);
int  yywrap(void);
int  yyreject(void);
int  yyracc(int);
void yyless(int);
int  yyinput(void);
void yyoutput(int);
void yyunput(int);
```

**`yywrap()` is the scanner's**; the other eight are the skeleton's and a scanner must not define
them. `input`, `output` and `unput` are macros a scanner may `#undef`, which is what `cmd/awk`
does.

## No support library

v7 shipped `/lib/libl.a`: `main.c`, `yywrap.c`, `allprint.c`, `reject.c` and `yyless.c`. This
port adds no archive to [`../../lib/`](../../lib/), so each had to go somewhere:

* `main()` and `yywrap()` — **the scanner's**, as `liby.a`'s were for yacc. `cmd/awk/main.c`
  already defines one.
* `yyreject()`/`yyracc()` and `yyless()` — **folded into `ncform`.** `header.c` emits
  `# define REJECT { nstr = yyreject(); goto yyfussy;}` into *every* scanner and `ptail()` emits
  the `case -1:` that exists for nothing else, so leaving `yyreject` undefined gives a program
  that compiles clean and fails at link naming a symbol the user never wrote. A documented
  feature of the language `lex.1` describes should not be a link error.
* `allprint()`/`sprint()` — **deleted.** They were reachable only under `LEXDEBUG`, which
  nothing defines.

So `cc lex.yy.c` is the whole recipe, and the manual page says so.

## Finding the skeleton

`once.h` hardcoded `_PATH_SHARE "lex/ncform"` and this tree has no `<paths.h>`. `find_form()` in
`lmain.c` is a path profile on `find_parser()`'s model, which is `cc.c`'s:

1. `$B6LEXFORM`, if set and non-empty — **not** keyed on `besm6`, because it is how a test, and
   `b6_lex()`, reach a skeleton that has not been installed;
2. `/usr/lib/lex/ncform` on the machine itself (C10d's location);
3. `~/.local/share/besm6/lex/ncform`, then `/usr/local/share/besm6/lex/ncform`, on the host.

It returns the last candidate when none exists, so the diagnostic names a path a user can act on.

## Building

`b6lex` is a host tool: `add_executable`, `install(TARGETS)`, and one `install(FILES)` putting
`ncform` at `<prefix>/share/besm6/lex/`.

`parser.y` goes through the **in-tree** `b6yacc` in a custom command of this directory's own —
`b6_yacc()` is inside the `libruntime` guard and hands its output to `b6_obj()`, which
cross-compiles. The three load-bearing details are `b6_yacc()`'s, for its reasons: a working
directory of its own, the output renamed off `y.tab.c`, and **both `b6yacc` and `yaccpar.c` in
`DEPENDS`**.

`lexparse.c` is **the only machine-written C this build compiles**, and it is the one source here
that is not held to the full warning set: `-Wno-unused-label` and
`-Wno-unused-but-set-variable` are properties of yacc's output rather than of this grammar
(`cmd/yacc/test` turns off the same pair), and `SKIP_LINTING` keeps cppcheck off it. Everything
hand-written here builds `-Wall -Werror -Wshadow` and passes cppcheck.

One trap this uncovered: **`parser.y`'s `%{ … %}` block after the first `%%` lands at file
scope.** `b6yacc`'s `cpycode()` copies it there, and v7 declared `i`, `j`, `k`, `g` and `p` in
it — four of which `yylex()`, in the same translation unit, declares as locals. They are
`act_i`, `act_j`, `act_k`, `act_g` and `act_p` now. clang's `-Wshadow` does not warn about
shadowing a global, so this was invisible here and fatal elsewhere.

**`b6_lex()`** ([`../../scripts/BesmCross.cmake`](../../scripts/BesmCross.cmake)) sits beside
`b6_yacc()` and is the same shape, `lex.yy.c` being another fixed name.

## Tests

**`test/`** is a GoogleTest suite driving the binary as a subprocess (`cmd/yacc/test`'s shape),
twenty-eight cases in four kinds:

* **what it emits** — the nine prototypes above, and that each *precedes* the definition it
  describes; `int yystoff` rather than a `struct yywork *`, and no `yycrank+-` anywhere; the
  unconditional `U()` and a 256-entry `yymatch[]`; `FILE *yyin, *yyout;` with no initialiser;
  one statistics line rather than v7's two; the six bound overrides reported back by `-v`; and
  that `-r` and `%r` are now unknown, which is the only assertion that the Ratfor half *left*
  rather than merely became unreachable;
* **what it refuses**, including `MissingSkeletonNamesThePathItTried`, which is `find_form()`'s
  last-candidate contract;
* **`cmd/awk/awk.lx.l`**, the only `.l` in the tree and the one scanner this machine has to
  compile. It generates, and its `-v` line is **pinned from a measured run** the way C10a pinned
  six conflict counts: 618/1000 nodes, 1345/2500 positions, 202/500 states, 9,663 transitions,
  64/1000 packed classes, 530/2000 packed transitions, 455/3000 output slots. Every number is
  comfortably inside the host profile, and **C10d sizes the `besm6` arm from exactly these**;
* **a generated scanner compiled by `HOST_CC` and run**, at `-std=c11 -pedantic-errors -Wall
  -Werror` — the only host-side proof that `ncform` produces code that works. Five of them: the
  manual page's own example, start conditions, a **char-compressed state** (so the negative-offset
  arm actually executes, asserted both in the table and in the output), `REJECT`, and `yyless()`.

**Eight bits gets a case with a known answer and a negative control.** A rule containing
`D0 AB` — Cyrillic `Ы` — is written twice, once with the bytes and once as `\320\253`, because
those are two different paths through `yylex()`. Fed those bytes it must print `cyr`; fed `P+`,
which is what seven-bit folding turns them into, it must print `80,43,` **and not** `cyr`.
Without the second half a lex that masked both the rule and the input would pass the first.

The test source is **pure ASCII** and the bytes reach the `.l` through a C escape: cppcheck
cannot parse a source that is not, and cppcheck runs over everything here.

### The native case

**`rootfs/scant.l`** is a small tokeniser, built through `b6_lex()` + `b6_prog()` and run under
`b6sim` from `build/rootfs/test/`. It is **not on the image** — `root.manifest` names every file
explicitly — and it is the only thing in C10b that says **`b6parse` accepts the rewritten
`ncform`** and that a generated scanner scans on a BESM-6. It also gives `b6_lex()` a caller
instead of leaving it dead until C17.

Its two cases exercise what the port rewrote: keywords, identifiers and numbers through a
comment-eating start condition (the `yylook()` walk, `BEGIN`, and a class large enough to be
packed by character), and an eight-bit case that reports the byte **by value**. It measures
**5,788 words** — 80 const, 3,741 text, 649 data, 1,318 bss — against the 28,672-word ceiling,
which is the scale a generated scanner costs; `calct` is 4,903 for a grammar.

`rootfs/` rather than `test/` because `cmd/lex` is added to the build outside the `libruntime`
guard, where `b6_prog()` does not exist — `cmd/yacc/rootfs` is the same shape, and C10d adds
`/usr/bin/lex` to this directory beside it.

**Every rule in `scant.l` is qualified with a start condition, `<INITIAL>` included**, and that is
not decoration: **an unqualified lex rule is active in every start condition.** A bare `"if"`
matched inside the comment and the keyword count came out 5 instead of 3. It is in the manual
page's BUGS now.

## What C10d still has to measure

The heap is what binds there, not `rootfs_lex_size`: nearly every table lex owns is `calloc`'d.
Computed from the allocation list at the shipped host sizes and `awk.lx.l`'s shape (618 tree
nodes, 202 states), the peak concurrent heap is about **12,400 words** — the parse tree 4,167
(`name`/`left`/`right`/`parent` 1,000 ints each plus `nullstr`), phase 2 about 7,600
(`positions` 2,500, `nexts` 2,000, four `NSTATES` arrays, `foll`), and phase 1 about 850. §6's
uncheckable ceiling, and a `besm6` profile is not optional.

**The static arrays are this port's own addition to that sum, and they are deliberate.** `NCH`
256 costs about 60 words in `symbol[]`, `cindex[]` and `match[]` together — `char` packs six to a
word — and 256 in `ctable[]`. Rather more went from stack to bss on purpose: `cgoto()`'s
`tch`/`tst`, `packtrans()`'s `go`/`temp`/`swork`/`cwork` and `acompute()`'s `temp`/`neg` are
**`static`** now, some 1,700 words moved off a 4,096-word stack that `cgoto → packtrans` would
otherwise have carried ~1,150 of. None of the three recurses. What is left on the stack is the
`cfoll`/`first`/`follow` recursion — small frames at a depth the input chooses, §6's last bullet,
and a ceiling C10d has to give it.

## Traps

* **A range wholly above `0177` no longer draws "Non-portable Character Class".** That warning is
  about the ASCII-adjacency assumption in `[A-z]`; `[\200-\377]` is the documented way to name the
  high half, so warning about it told a user to stop doing the right thing.
* **Do not run `clang-format` over `ncform`.** It is not standalone C: `YYLMAX`, `struct yysvf`,
  `yycrank`, `yytop`, `yymatch` and `yyextra` are all emitted above it.
* **`ldefs.h` and `once.h` are §1 blind spots** — `b6_obj`'s header dependency is the *system*
  header tree, so editing either rebuilds nothing. Touch a `.c`.
* **Editing `header.c` or `sub2.c` changes every scanner this machine will ever compile.** The
  emitted-text assertions in `test/` are the only thing that notices; add one when you add
  emission.
* `YYSTATE` and `yybgin-yysvec-1` are **`ptrdiff_t`**, one word on the target and a `long` on the
  host, so a scanner printing either with `%d` needs a cast. §3's shape, in the generated
  interface rather than in lex.
