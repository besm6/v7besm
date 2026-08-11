# `m4`, and what a builtin is when a pointer can be recycled

Task C13. `m4` is the third program on this image built from a yacc grammar, after
[`expr`](../expr/) (C11) and [`egrep`](../egrep/) (C26), and the first that is **a grammar plus
a hand-written translation unit** — [`m4y.y`](m4y.y) is `eval`'s expression parser, [`m4.c`](m4.c)
the macro processor, and they share three globals across the link. It does **not** retire the
risk [../yacc/README.md](../yacc/README.md) records: `m4y.y` writes `#define YYSTYPE int`, so
`%union` is still unexercised and C14's `make/gram.y` is still the grammar that will exercise it.
The grammar has **zero shift/reduce and zero reduce/reduce conflicts**, as `expr`'s did.

The C11 pass is described in the two sources' own headers and is not repeated here. Six things
the port *taught* are, because none of them is about this program alone.

## A builtin identified by the address of a one-byte block

v7 installed each of its twenty-one builtins with `install(name, eoa)`, kept the `char *` that
came back in a global — `lenloc`, `defloc`, `evaloc`, twenty-one of them — and dispatched by
comparing the macro's definition pointer against each in turn:

```c
dp = a1[-1];
if (dp==defloc)        dodef(a1, c);
else if (dp==evaloc)   doeval(a1, c);
```

[../README.md](../README.md) §2 is right that this survives the fat pointer: `==` and `!=`
between two `char *` are raw word compares and order nothing, so the comparison means what it
says. **The mechanism is not broken by the machine. It is broken by the machine's allocator.**

`install()` on a name that already exists does `free(np->def); np->def = copy(val);`.
[../../lib/libc/gen/malloc.c](../../lib/libc/gen/malloc.c) is circular first fit and leaves
`allocp` pointing at the block it just freed, and every builtin's definition is `copy("")` — a
one-byte request, two words after the header. So ``define(`len', X)`` frees `len`'s two-word block
and the `copy("X")` in the next statement, also two words, takes it straight back. `np->def`
comes out equal to `lenloc` again and **`len` is still the builtin**. Built as v7 wrote it, on
this machine:

```text
$ echo "define(\`len',X)len(abc)" | m4
3
```

The manual page's own rule — "They may be redefined, but once this is done the original meaning
is lost" — was the thing that silently did not happen. And the sharp half is worse, because the
block outlives the name that owned it:

```text
$ echo "undefine(\`len')define(\`zz',q)zz" | m4
2
```

`undefine` freed `len`'s block, `define` handed it to `zz`, and `zz` **became** `len` — the `2`
is `dolen` measuring a stale argument slot. An unrelated macro turned into a builtin.

The port replaces the pointer with an `int bltin` in `struct nlist`, carried into `struct call`
at the moment the call frame is opened, and `expand()` is a `switch`. The twenty-one globals,
the twenty-one one-byte `malloc`s and the twenty-one-deep `else if` chain all go; `install()`
sets the code to `B_NONE` on any name it redefines, which *is* the documented rule. It is
smaller, it is faster, and it costs one word per name that exists.

Three alternatives were weighed and each loses. *Never free a builtin's definition* fixes
`define` and leaves `undefine` dangling. *A sentinel byte prefixed to the definition string*
reintroduces exactly what §11 condemns, a byte value stolen out of a stream that carries all
256. *Compare `nlist` addresses instead* needs the same plumbing the integer code needs and buys
an aliasing risk in exchange for nothing.

The rule worth taking away: **identity that lives in the allocator is identity you do not own.**
`cmd/README.md` §2's three arena hazards are about a program that manages its own storage; this
is the fourth shape, and the program that has it does not manage any storage at all — it just
kept a pointer that `malloc` was still entitled to reissue. Three cases pin it:
`redefbuiltin`, `undefbuiltin` and `undefalias`.

## The relational that runs once per character, and why an index is not the fix

```c
#define	getchr()	(ip>cur_ip?*--ip: getc(infile[infptr]))
```

`ip` and `cur_ip` are both `char *`, so §2's `b$pdiff` call stands between the program and
every single input character of every job. `pbstr()` had one per pushed byte and `expand()`'s
default arm two per byte of every macro body it expanded.

The obvious reading of §2 — rewrite the buffers to `int` indices — **buys nothing here**, and
that is the part worth writing down. `b6nm` over any program in this tree lists `b$padd`
beside `b$pinc` and `b$pdec`: on a word-addressed machine `buf[i]` is a call exactly as `*p++`
is a call, and they cost the same. Only the *comparison* is removable. So the shape is a
**shadow counter beside the pointer**, not an index in place of it:

```text
invariant:  ip == ibuf + curx + nback
            curx  = the current input level's pushback floor
            nback = characters pushed back and not yet re-read
```

`getchr()` becomes `(nback ? (nback--, *--ip) : getc(...))` — one `int` test where there was a
`b$pdiff` — and `putbak` becomes a checked macro on `curx + nback`, which is a bound v7 tested
once per *string* rather than once per byte. `expand()`'s walk keeps its backward `char *` and
carries a count alongside. Over a 26,705-byte input with two macro calls per line:

| | instructions | text |
|---|---|---|
| v7's pointer relationals | 22,733,353 | 6,558 words |
| shadow counters | 17,889,988 | 6,500 words |

**21.3% fewer instructions and 58 words less text**, for a change that touches four macros and
one loop. `cur_ip` and `char *ip_stk[10]` disappeared with it, replaced by `int nback_stk[10]`.

Making the include push and pop explicit also exposed a v7 bug that had nowhere to hide once
the state was named. `doincl()` set the pushback floor **before** the `fopen`, and a failed
`sinclude` did only `infptr--` — it never moved the floor back. So

```text
define(x, sinclude(nosuchfile)tail)x
```

lost `tail` for ever. Opening before pushing makes the bug unwritable; `includepending` is the
case.

## A table indexed by `getc(3)`'s result is out of bounds at both ends

§11 has five shapes on the record — `grep`'s `CCL`, `sort`'s `+128` bias, `sed`'s `y///` table,
`file`'s `english()` and `expr`'s bitmap. This is the sixth, and its distinguishing feature is
that the index does not come from a `char` at all:

```c
if (type[t]==ALPH) {
	while ((t=type[*tp++=getchr()])==ALPH||t==DIG);
```

`type[]` has 128 entries. [../../include/stdio.h](../../include/stdio.h) defines `getc` as
`(--(p)->_cnt >= 0 ? *(p)->_ptr++ & 0377 : _filbuf(p))`, so a byte above `0177` arrives as
128..255 and reads up to 72 entries past the array — and `_filbuf` returns `EOF`, which is −1
and reads the word *below* it. Too short above, and negative below.

**The symptom was not a misclassification.** In that loop the assignment's value is the value
after conversion to `char`, so `EOF` becomes 255, the loop stops, and `putbak(*--tp)` pushes a
literal `0377` back into the input, which m4 then emits. Any file ending in a macro call with no
trailing newline grew a spurious `0377` byte — a fault in the output of a program that had
nothing wrong with its output logic. `eofname` is the case.

The table is 256 entries here and end of file is tested for by name. Bytes `0200`–`0377` are
**`ALPH`**: a byte above `0177` is part of a multi-byte letter and can never be punctuation, so
"letter" is the only classification that is right, and it makes `define(привет, здравствуй)`
work. `utf8name` is the positive case, and `utf8pass` and `utf8undef` are the sharp ones — with
a 128-entry table the failure is a *plausible wrong answer*, a word quietly eaten, not an error,
so the width needs a case that says nothing was eaten as well as one that says a name was found.

Two consequences follow from the same decision. `len`, `index`, `substr` and `translit` count
**bytes**, which the manual page now says and `utf8len`, `utf8index`, `utf8substr` and
`utf8translit` pin — `translit(абв,а,я)` yields `яѱѲ`, and that is what byte-wise
transliteration of a multi-byte alphabet looks like. And **a NUL is an ordinary byte**, which
took v7's `putbak(0)` input primer out with it: the primer meant EOF had to be tested as
`t <= 0`, so a NUL ended the file, and the same `> 0` test in the diversion reader truncated a
diversion at the first NUL. File advancement is a `nextfile()` of its own now and the test is
`t == EOF`. `nulbyte` and `divertnul` are the cases. A NUL still **terminates a macro argument**,
arguments being held as NUL-separated strings in one buffer; that is structural and is written
in the manual page rather than fixed.

## `mktemp(3)` writes its argument

```c
tempname = mktemp("/tmp/m4aXXXXX");
...
tempname[7] = 'a'+i;
```

Two writes into a string literal, at four sites, in a program whose data segment is writable
because it is not `PURE`. The template is an array here, and the index arithmetic is worth
stating because the two ends belong to different programs: **`tempname[7]` is m4's own `'a'`**,
the diversion letter, and **index 8 is libc's**, the first of the five `X`s that
[../../lib/libc/gen/mktemp.c](../../lib/libc/gen/mktemp.c) fills with process-id digits and then
uniquifies. The port also keeps the return value, because libc's `mktemp` answers with the
literal `"/"` when all 26 letters are spoken for, and `"/"[7]` is a store into the constant pool
— one `!=` is cheaper than the argument that it cannot happen. And `domake()` guards an empty
argument, since `mktemp` walks `--s` back from the NUL and an empty string takes it below the
array; that guard belongs at the caller, libc's behaviour being v7's bargain and not this task's
to change.

## Eight v7 bounds, and one that was unreachable

| site | v7 | here |
|---|---|---|
| call stack | `++cp > &callst[STACKS]` — one *past* the end | `>=`, and reachable at last |
| `argstk` | `[STACKS+10]`, tested on one of three paths | `[3*STACKS+10]`, tested on all |
| token | unbounded scan into `token[128]` | a longer word is emitted as text |
| `*op++` | tested once per token in `puttok` | a checked macro on every store |
| pushback | tested once per *string* in `pbstr` | a checked macro on every byte |
| `include` nesting | never tested against `infile[10]` | diagnosed at 9 |
| `len` with no argument | measured a stale `argstk` slot | `0` |
| `index` | kept comparing after a mismatch, past `p1`'s NUL | breaks |
| `substr` missing length | `TOKS` — 128, silently truncating | to the end of the string |

Two of these are worth more than a row. **`call stack overflow` was unreachable**: each frame
takes at least three `argstk` slots — definition, name, first argument — so with `argstk` at
`STACKS+10` the arg stack always ran out first and the message could not be printed at all.
Sizing `argstk` at `3*STACKS+10` costs 110 words of bss and makes both diagnostics reachable
and both testable; `argstack` and `callstack` are the cases.

And **the token bound is not a diagnostic**. A word longer than `token[]` cannot match any name,
so the port emits it as text and goes on reading — `m4` is a program whose job is passing text
through, and 128 bytes is only 64 Cyrillic letters, which is not an exotic word. v7 ran off the
end of the array; a fatal error would have been the other wrong answer. `longword` is the case.

`errprint` belongs here too, though it is not a bound: v7 wrote
`fprintf(stderr, ap[1], ap[2], ap[3], ap[4], ap[5], ap[6])` — five argument slots it had not
checked were there, and **the user's text as a `printf` format**, which is where §3's
echoed-verbatim `%D` lives. It prints `"%s\n"` here. `errprintpct` is the case.

## `eval` is one word, and it divides

`long` is `int` is one word, so `#define YYSTYPE long` became `int`, `ctol()` and its identity
wrapper `ctoi()` became one function, and `eval` is 41 bits rather than the page's 32 — wider,
so nothing that fitted before fails now, but the page said a number and the number was wrong.

Division is the part that is not cosmetic. `cmd/expr` established that this machine's divide
instruction **faults** on a zero divisor, so v7's unchecked `/` and `%` did not produce a wrong
answer, they killed the process. The guard sets `evalerr` and takes `YYABORT`, which
[../yacc/yaccpar.c](../yacc/yaccpar.c) defines as `return (1)` — exactly the value `doeval()`
already treated as failure, so the error path is v7's own and the syntax-error message keeps
v7's wording byte for byte.

## The measurements

```text
const   text   data   bss     dec
  119   6500    970  3152   10741 words        ceiling 28,672
```

Room to spare: `m4` keeps its state in globals rather than in one struct, so §6's 4,096-word
struct ceiling never comes near — `struct nlist` is 4 words and `struct call` 3. Of the 10,741,
`ibuf` and `obuf` are 1,408 and stdio is most of the rest; the program's own text is small.

Stack, from `m4.dis`, against the 4,096-word ceiling:

```text
main      509 words
yyparse   399
yylex      53
```

Nothing recurses: m4 rescans through the pushback buffer, not the C stack, so the deepest chain
is `main → expand → do*() → pbstr`, and the input cannot choose the depth. `argstk` (160 words)
and `callst` (150) are at file scope rather than in `main`'s frame, which is 310 words `main`
would otherwise have carried permanently.

On the disk `m4` costs **19 blocks**, and the image went from 364 free to **345**.

`b6nm` over the finished binary shows one definition each of `evalval`, `pe` and `evalerr` —
§1's check for a program in more than one translation unit, and the whole of what stands in for
a header here. There is no `m4.h`, so §1's build blind spot never arises.

## What this harness cannot say

`b6_progtest` runs the staged binary under `b6sim`, whose system calls are the host's, so the
105 cases stop where the host does.

* **`syscmd` has no case.** [../../lib/libc/stdio/system.c](../../lib/libc/stdio/system.c)
  `execl`s `/bin/sh`, and `b6sim`'s `sys_exec` loads a BESM-6 `a.out` — the build machine's
  shell is not one, so the child gets `ENOEXEC` and `_exit(127)`s in silence. A case would pass
  while asserting nothing, which is the worst kind.
* **`catchsig` and the SIGHUP/SIGINT cleanup**: nothing can signal the guest.
* **`m4: no space for alloc`** — §6's uncheckable heap ceiling — and **`m4: cannot create temp
  file`**, which wants 26 pre-existing `/tmp/m4a*`.
* **The image's own `/tmp`.** The diversions land in the *host's* `/tmp` under `b6sim`. That
  `/tmp` exists on the disk at mode 0777 is asserted by [../../root.manifest](../../root.manifest),
  not by these cases.
* **A file name containing a space, or an empty one**: `.args` is split on whitespace with no
  quoting. Only four cases here name a file at all, the rest being `.in` files fed verbatim, so
  this limit — which shaped `expr`'s and `egrep`'s suites — binds on almost nothing.
* **A `0377` byte typed at a terminal.** [../../kernel/dev/tty.c](../../kernel/dev/tty.c)
  refuses it, the raw queue's delimiter; `byte377` proves the file path only.
* **Two `m4`s at once** picking distinct diversion letters.

Checked by hand under the booted kernel — a single-user boot off `root3072.disk` with one
command line typed at the shell, which is the only place the first two facts can be established
at all:

```text
# echo 'syscmd(echo A-ok)' | m4; (echo 'divert(1)B-ok'; echo 'divert(0)main') | m4; ls /tmp; \
  echo 'len(привет) eval(6*7) eval(1/0)' | m4; m4 /nosuchfile; echo status=$?
A-ok

main
B-ok
m4: divide by zero in eval: 1/0
12 42
m4: file not found: /nosuchfile
status=1
```

Five things that says and `b6sim` cannot. **`syscmd` works** — `system(3)` found `/bin/sh` on
the image and its output reached the terminal. **The diversions used the image's own `/tmp`**,
and the empty `ls /tmp` between them says the temp files were unlinked on exit rather than left
behind. A Cyrillic argument survived the console path, the shell's quoting and the pipe. `eval`
answers on the real machine and not only under the simulator. And the exit status a shell sees
is the one the cases assert. (The stderr line precedes `12 42` because stderr is unbuffered and
a piped stdout is not; that ordering is what every `.expected` in `test/` records too.)

## What did not need saying

No `%D` or `%O` anywhere in either source — §3's trap costs a second to check and did not fire
here. The `#ifdef gcos` and `#ifdef M4` arms are deleted rather than carried: v7's makefile
passed neither, so both were dead, and the lower-case builtins and grave/acute quotes are what
the program actually was. `int lpar = '('` was a gcos artefact, written once and never again,
and is a constant now. `m4` reads no directory and calls no `read`, `write`, `lseek` or `stat`,
so §4's block size and §5's `DIRSIZ` do not enter into it at all.

One thing the port found and did not fix: **`$0` cannot be used.** A replacement text containing
`$0` expands to the macro's own name, which is rescanned as a call on that same macro and never
terminates — `m4` stops with `pushback overflow`. That is v7's behaviour and every later `m4`'s;
it is in the manual page under `define` now, because the page promised argument 0 without
mentioning what happens to it.
