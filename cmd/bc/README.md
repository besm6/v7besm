# `bc`, and a program that carries its own types in a pointer

Task C16, and the other half of C15. `bc.y` is 600 lines and **it is a compiler, not a
calculator**: it parses this language, emits [`dc(1)`](../dc/)'s postfix commands, and `exec`s
`/bin/dc` to run them, with `-c` printing them instead. Nothing here does arithmetic — a `+` in
the source becomes the two characters `+` in the output, and every question about precision is
[`../dc/README.md`](../dc/README.md)'s.

The C11 pass is mechanical and the source's own header says what it is. Four things the port
*taught* are here, because none of them is about this program alone, and one of them is a bug the
program has had since 1979.

## `bundle()` read its own argument frame

Two thirds of the grammar's actions are one call to `bundle(n, …)`, which collects *n* things —
string literals, register names, handles to other bundles — into an arena and hands back a handle.
v7 wrote it before `<varargs.h>` existed:

```c
bundle(a){
	int i, *p, *q;
	p = &a;			/* the first argument */
	i = *p++;		/* ...and everything after it, upward */
	...
}
```

That is the PDP-11's calling convention used as an interface. It cannot survive here:
[`../../doc/Besm6_Calling_Conventions.md`](../../doc/Besm6_Calling_Conventions.md) puts arguments
in direct order with **the last one left in the accumulator** and a negative count in `r14`, so
there is no argument vector at `&a` to walk — and `&a` on a non-variadic function is a frame slot
the compiler chose, not the start of anything.

It is `char *bundle(int n, ...)` now, over `<stdarg.h>`, and the one thing worth copying is *how*
it reads a `char *` argument. Not `va_arg(ap, char *)` but

```c
w = va_arg(ap, int);
b_space[b_nxt++] = *(char **)&w;
```

which is [`lib/libc/stdio/doprnt.c`](../../lib/libc/stdio/doprnt.c)'s own idiom, and that file's
comment says why: **reading a pointer back through `char **` re-decorates the word**, which is
harmless for a real pointer and turns a null into a nonzero one. Taking the raw word and
reinterpreting it is exact for both. `printf("%s")` in this libc does the same thing for the same
reason, so there is one idiom here and not two.

## An `int` arena cannot hold a `char *`, and the range test that read it still can

`bundle()`'s arena is where the port's real design work went. v7's is

```c
int b_space[3000];
```

holding, in one array of `int`, both `char *` string literals and the addresses of nested bundles
— and `routput()` tells the two apart **by testing the pointer against the arena's own address
range**:

```c
routput(p) int *p; {
	if( p >= &b_space[0] && p < &b_space[b_sp_max]) ...  /* a bundle */
	else printf( p );                                    /* a string */
}
```

Two separate things break. The first is [`../README.md`](../README.md) §2's headline: a `char *`
here is a **fat pointer** carrying a byte offset above the word address, an `int` is 41 bits, and
the pointer does not fit. `../expr/expr.y` met that first and the answer is the same one —
`YYSTYPE` becomes a pointer type, and every `$$`, every `yylval` and every arena slot with it.

The second is subtler and is the reason the arena did not simply change to `char *b_space[3000]`.
With the elements pointer-typed, a handle would have to be `(char *)&b_space[i]` and come back as
`(char **)p` — and §2's third arena hazard is that **a cast to a thin pointer floors a fat one to
its word**. The round trip happens to be exact here, every handle being word-aligned by
construction, but "happens to be exact" is the wrong footing for the one operation the whole
program depends on.

So the elements and the *name* of a bundle were separated:

```c
static char *b_space[BSPMAX];	/* the elements */
static char b_mark[BSPMAX];	/* one addressable char per slot */
```

A handle is `&b_mark[i]` — a genuine `char *` into a genuine `char` array. `isbundle(p)` is v7's
range test unchanged, now between two pointers of the same type into the same object, which is
exactly what `b$pdiff` is for; the index is `p - b_mark`, one subtraction per descent and not per
element; and **there is no cast in either direction**. The cost is 500 words of bss, which is what
`b_mark` weighs, and it buys a mechanism a reader can check by looking at it.

The one semantic value that genuinely *was* an `int` — `yylval = c` for a DIGIT — became a
pointer into a sixteen-entry table of one-character strings, `dig[]`, which is the treatment
`letr[26]` already gave letters. A per-token scratch buffer would not do: yacc reads its lookahead
before it reduces, so two DIGITs are live at once.

## The output was a `printf` format, and the grammar was built around it

`routput()` ended `else printf(p);` — a **non-literal format**, on strings that reach it straight
from the user:

```
$ bc
"50% off"
```

`%` is a conversion and there are no arguments; on this machine that is a `va_arg` off the end of
an empty list. It is v7's, it is thirty years old, and it is visible in the grammar itself: two
productions spell dc's remainder operator `"%%"` —

```
	|  e '%' e	= bundle(3, $1, $3, "%%" );
```

— because the doubled percent had to survive `printf` to come out as the one character dc wants.
That is the whole tell. `fputs(p, stdout)` replaces it, both literals are `"%"`, and the quoted
string works; `qstr` and `run.str` are the cases. It is the same shape as `m4`'s `errprint`
(C13) and `make`'s `sprintf`-into-`fatal()` (C14): **a v7 program that prints a string it did not
write is printing a format until somebody looks.**

## The walk is iterative, and five bounds were not bounds

`routput()` recursed once per bundle element, and bundle nesting is the input's depth: a
left-associative `a+b+c+…` nests once per term, and the 3,000-slot arena reaches about 1,500. The
walk is an explicit stack in bss now (`rstack[256]`), which is what `egrep` and `lex` did for the
same reason, and after it **`bc` has no C recursion at all** — the parser's depth lives in
`yys[]`/`yyv[]` and the calculator's in dc.

Five of v7's own bounds were reachable, and all five are cases:

| | v7 | here |
|---|---|---|
| `bstack[10]` | `bstack[bindx++] = lev++` with no test — the eleventh nested `while` writes past it | diagnosed (`nestwhile`) |
| `cary[1000]` | `*cp++` with no test anywhere — a long constant walks off it | diagnosed (`longconst`) |
| `break` outside a loop | `numb[lev - bstack[bindx-1]]`, and `bindx` is 0 | diagnosed (`breakout`) |
| the arena guard | called `yyerror` and then **wrote anyway**, on the next line | stops |
| a file that will not open | diagnosed with `ss` still unset, then read through a null `FILE *` | named, and fatal (`nofile`) |

Note that the `bstack` bound is *not* nested `if`s: `_IF CRS BLEV …` decrements at `BLEV`
immediately, so only `while` and `for` accumulate. `nestif` pins eleven of those working, and the
register walk skipping `[` and `a` with them.

## Two divergences, both in `bc.1`

**End of input exits 0.** v7 reached `getout()` from three places — end of the last file, `quit`,
and any internal error — and all three `exit(1)`. So `bc -c prog.b > prog.dc` reported failure on
every successful run. `getout()` takes a status here: 0 for end of input and `quit`, 1 for the
five bounds above.

**Flags are parsed in a loop.** v7 read exactly one, and `-l` worked by overwriting `argv[1]` with
the library's name — so `bc -c -l` opened a file called `-l`, and `bc -l prog.b` read `prog.b`
*instead of* the library rather than after it. The page has always documented
`bc [-c] [-l] [file …]`; it does that now, the file list being built in `fv[]` with `/usr/lib/lib.b`
spliced in front when `-l` is given.

## The math library

`/usr/lib/lib.b` — what `-l` loads, and the six functions `bc.1` lists — **was not in this tree**.
It is not under `cmd/bc/`, it is not on the v7/x86 image the rest of this port came from, and
nothing in the repository mentioned it but the manual page. The copy here is 2.11BSD's
`share/misc/lib.b`, stamped `lib.b 4.1 83/04/02`, which is the Bell original unchanged. It is
data, not C, and is staged rather than compiled, on the same footing as `/usr/lib/yaccpar`.

It is also the port's hardest end-to-end exercise, and it passed first time: nested `define`s, ten
`auto`s apiece, `scale`/`length`/`sqrt`, `while(y--)`, `for(i=1;1;i++)`, `1.` as a number, and —
the reason it is worth saying — `if(n%2!=0)` in `s(x)`, which goes straight through the `printf`
format above.

```
e(1)      2.71828182845904523536
l(1)      0
s(0)      0
c(0)      1.00000000000000000000
a(1)*4    3.14159265358979323844
j(0,0)    1.00000000000000000000
```

at the default scale of 20. The last digit of `a(1)*4` is π truncated rather than rounded, which
is `scale`'s definition and not an error.

## The measurements

```
	const	text	data	bss	dec	oct
	  188	 5675	2385	5335  13583  32417
```

13,583 words against the 28,672 ceiling; `rootfs_bc_size` is the check. About 4,240 of the 7,720
words of data and bss are the arenas — `b_space` 3,000, `b_mark` 500, `rstack` 256, `cary` and
`sary` 167 each, and yacc's `yyv[151]` — and most of the rest is stdio's.

**The stack, which nothing checks.** From the `15 utm 0NNN` prologues in `build/cmd/bc/bc.dis`,
the deepest chain is

```
main 105 + yyparse 964 + conout 18 + printf 3 + _doprnt 281 + _flsbuf 112  =  1483
```

of 4,096. It is **bounded, and that is the point**: with `routput()` iterative the call graph is a
DAG, so no input can make it deeper. `yyparse` alone is 964 words — a quarter of the stack, and by
far the largest frame in the program — because `yaccpar.c` puts `short yys[YYMAXDEPTH + 1]` in the
frame while `yyv[]` is a global. Raising `YYMAXDEPTH` from its 150 would be paid for out of the
stack, once, whether or not a grammar needs it.

**The grammar's conflicts are v7's, exactly**: `12 shift/reduce, 30 reduce/reduce`, and running
`b6yacc` over the unmodified upstream `bc.y` gives the same two numbers. That is a lot, and it is
the price of a grammar where `stat` and `e` both derive `LETTER '=' e`; what matters is that the
count does not move. It has not.

**On the disk** `bc` costs **20 blocks** and `lib.b` **1**, and the image went from 291 free to
**270**. The manual page was already staged — `B6_STAGE_MAN` globs `cmd/*/*.umm` and
`../../scripts/root.manifest` has carried `/usr/man/man1/bc.1` since long before there was a program.

## What this harness cannot say

35 cases under [test/](test/), in two shapes, and the reason there are two is that a compiler and
its output fail differently: 24 `b6_progtest` cases diff the **dc source** `bc -c` emits, and 11
`run.*` cases run that source through the staged `/bin/dc` and diff the **numbers**. The second is
the only way `lib.b` could be tested by a program at all.

Two of the expectations hold raw bytes and are meant to: a dc register name is one byte, functions
being `01`–`032` and arrays `0241`–`0272`, so `array.expected` and `define.expected` carry the
names themselves. They are the standing assertion that `getf()`/`geta()` still hand out what
`../dc/dc.h` reads.

What has no home:

* **The pipe.** `bc` with no `-c` forks and `execl`s `"/bin/dc"`, and b6sim resolves that against
  the *host* filesystem — where the first `execl` finds nothing on a Mac and the second finds the
  host's own `dc`, so a case without `-c` would assert nothing or, worse, assert the host's
  arithmetic. Feeding the source across by hand runs the same two programs over the same bytes and
  leaves out only the `fork`.
* **`SIGINT`.** `yyinit()` sets it to `SIG_IGN` in the child, and only in the child, so `^C`
  reaches dc and not bc — v7's arrangement, and the reason `dc.c`'s
  `if (signal(SIGINT, SIG_IGN) != SIG_IGN)` still installs a handler here. Nothing in this suite
  delivers a signal, and driving one in at a scripted Consul has not been made to work; **it is
  worth a keyboard.**
* **The terminal.** That a result reaches the screen before bc blocks for the next line is timing,
  not content.

Checked by hand instead, under the booted kernel — the whole pipeline, `fork` and `execl`
included, which is the part b6sim cannot reach:

```
# bc
2+3
5
define f(x){
auto a
a = x*x
return(a+7)
}
f(6)
43
quit
# bc -l
scale=20
4*a(1)
3.14159265358979323844
e(1)
2.71828182845904523536
s(0)+c(0)
1.00000000000000000000
quit
#
```

`bc -l` there is reading `/usr/lib/lib.b` off the image, and `quit` returns to the shell with the
status the page now documents.

## What did *not* need saying

* **No `%D` and no `%O`** — §3's cheapest bug is simply absent from this source, which is unusual.
* **No directory, no `long`, no `struct`, no `malloc`** — §5, §12, the 4,096-word struct ceiling
  and the heap all miss it. Every arena is static and now bounded.
* **No 128-entry table indexed by a character** — §11's shape is absent: `yylex` uses ranges and
  everything above `0177` falls to `default`, so a Cyrillic byte is a syntax error and not a
  misread one. What §11 *does* decide is that `atab`'s `0241`–`0272` survive at all, `char` being
  unsigned here.
* **`%union`** — C14 retired it, and this grammar has no use for it: one pointer type covers every
  value.
