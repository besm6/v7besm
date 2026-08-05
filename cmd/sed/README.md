# Porting `sed(1)` — task C5e

`/bin/sed` is on the image, `/bin` holds forty-seven entries, and `kernel/test/filters`
runs **seventeen** filters in one boot. 81 cases under `b6sim`, in half a second.

Sizes, in words of the 28,672 §6 allows:

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `sed` | 123 | 7,912 | 389 | 5,696 | **14,120** |

Half the ceiling, and the largest of the seventeen filters — 5,696 words of bss is
`respace` (1,334), `linebuf`, `holdsp` and `genbuf` (667 each), `ptrspace` (1,131) and
`tlno` (256), which is most of the program before a line of code.

**Read [../grep/README.md](../grep/README.md) and [../ed/README.md](../ed/README.md)
first.** [../TODO.md](../TODO.md) said so and it was right: two of the five things below
are those two tasks' recipes applied, and the value of this file is the three that were in
neither.

---

## 1. A header that DEFINES the program's globals, seen a second time

`sed` is the first multi-file native port since `/bin/sh`, and v7's `sed.h` declared all
forty-odd of its objects as tentative definitions — `char genbuf[LBSIZE];` — in a header
both `sed0.c` and `sed1.c` include. The PDP-11 linker merged the two common blocks. **C11
has no tentative definition across translation units and `b6ld` has no common symbols**, so
each half would have got its own storage: `sed0.c` compiling a script into *its* `ptrspace`
and `sed1.c` executing an empty one, with nothing anywhere saying so.

[../sh/defs.h](../sh/defs.h) records the same fix from task C11, and `sh/glob.c` is the
same file as [sedglob.c](sedglob.c). What makes it worth writing down twice is that **the
failure is silent and total** — not a link error, not a wrong answer in one command, but a
program in which the compiler and the executor share nothing. The check that says it worked
is one line of `b6nm`:

```
$ b6nm build/rootfs/bin/sed | grep -E 'genbuf|respace|ptrspace'
26721 B genbuf
21645 B ptrspace
22776 B respace
```

One symbol each. **Any multi-file v7 program is a candidate**: `tar`, `make`, `m4`, `awk`
and `dc` are the five left, and `cmd/README.md` §1 now names the shape.

v7's copy also defined `reend`, `lbend` and `depth` **twice in that one file**, which the
common-symbol model swallowed and C11 does not.

## 2. The character class: `grep`'s recipe, and it did not paste

The `CCL` bitmap is 256 bits here as `grep`'s became in task C5c, and every word of that
task's account applies: the **compile** side masked nothing, `char` is unsigned, so a
pattern byte of `0300` stored eight bytes past its own sixteen-byte class and on top of
bytecode `compile()` had already written. The five widths and two masks are tabulated in
[../TODO.md](../TODO.md)'s brief and were all where it said.

**And one thing this port expected from `grep` and did not get.** `grep`'s proving assertion is
a *false positive*: `& 0177` folds `0320` onto `P`, so v7's `grep` finds `[п]` inside `ALPHA`.
The same case was written here and **v7's `sed` gets it right** — the stray compile-side bytes
land *forward* of the class, and in `sed` the bytecode compiled after the class overwrites them
before the match ever runs. The store is still out of bounds and still had to be fixed; what it
does not do here is produce a plausible wrong *match*. So the sharp negative is a different one:
`[а-я]` over Cyrillic, where the match-side mask means no byte of a Cyrillic letter can be in
any class and the answer is silently empty. `cmd_sed_utf8nomatch` stays as a correctness case
with a comment saying it is not sharp, because **an expected divergence that turns out not to
exist is worth writing down** — C3 said the same about a warning that named a bitmap `ed` did
not have.

**And `grep`'s hunk could not be copied**, for three more reasons worth listing because
the next regex program will meet the same question:

* **`sed` compiles into an ARENA.** `grep`'s `expbuf` is one bss array, zeroed once; `sed`
  carves every expression, replacement and text argument out of `char respace[RESIZE]` and
  reuses it across a whole script. So `memset(ep, 0, CCLSIZE)` is **mandatory** here where
  in `grep` it was belt and braces. A class compiled after another command would otherwise
  inherit that command's bytes as class members.
* **`ep[0] &= 0376` is outside `sed`'s `if(neg)`** and inside `grep`'s. Left where v7 had
  it; it is a no-op for a positive class, the `c == '\0'` arm having already exited.
* **The range loop has no `sp++` after it** and `sed` handles `\n` inside a class. Different
  control flow, same three lines of table arithmetic.

And the arena forced a bound `grep` does not need. **v7 limited an expression to `ESIZE`
from its own start and never asked whether `ESIZE` bytes were LEFT in the arena**, so an
expression compiled near the end of `respace` ran off it. `compile()` takes the smaller of
the two now.

## 3. The `y` table — a 128-entry table whose size is written as a loop condition

**This one is in no brief, no table and no warning**, and it is the finding of the task.

`y/abc/xyz/` compiles to a translation table indexed by the byte being translated. v7's was
128 entries. Grep `cmd/sed` for `128`, or for `[128]`, or for the `& 0177` that would go
with it, and you find **the mask and nothing else** — because the size is written twice and
neither time as a number:

```c
	for(c = 0; !(c & 0200); c++)	/* the identity fill: 128 iterations */
		if(ep[c] == 0)
			ep[c] = c;
	return(ep + 0200);		/* ... and 128 bytes is how much it took */
```

Meanwhile the **execution** side masks nothing at all:

```c
	case YCOM:
		p1 = linebuf;
		p2 = ipc->re1;
		while(*p1 = p2[*p1])	p1++;	/* *p1 is 0..255 */
```

So `y` on any byte above `0177` read **128 bytes past its own table**, into whatever
`compile()` had put next in the arena, and wrote what it found back into the line.

This is `sort`'s C5d finding in a third shape and the sharpest of the three. `sort`'s
tables were the right size and were reached with a `+128` bias — a grep for the *size*
would not have found it. `sed`'s table is the wrong size **and the size is not written
down**, so a grep for either finds nothing. What found it was reading `ycomp()` because it
was the one routine in the file nobody had named.

> **A ceiling written as a loop condition and a pointer bump is a ceiling no search will
> find.** Read the routines a brief does not mention.

## 4. Bit `0200` in the replacement text: `ed`'s `QESC`, transcribed

`compsub()` marked every backslash-escaped byte of an `s///` right-hand side by setting bit
`0200` of it, so that `dosub()` could tell `\1` from `1` and `\&` from `&`. And `dosub()`
stripped that bit from **every other byte**:

```c
	*sp++ = c&0177;		/* v7 */
```

On a machine whose text is UTF-8 that is `col`'s failure exactly, in the program whose
whole job is producing text: `s/x/привет/` wrote ten bytes of plausible ASCII, not an empty
line and not a diagnostic. [../ed/README.md](../ed/README.md) has the encoding and
`ed.c`'s `compsub()`/`dosub()` are the two sites; `QESC` is `0377`, a prefix byte, and an
escaped or unescaped `0377` share one encoding because `0377` is not a metacharacter in a
replacement and its escapedness cannot be observed.

Two things came with it, both `ed`'s:

* **The `esc` flag carries v7's control flow.** There, an escaped byte could not compare
  equal to the delimiter *because the `0200` bit had already made it unequal* — which is how
  `s/x/\//` inserts the delimiter. Testing `!esc` asks the same question, and `escamp` is
  the case that says so.
* **A prefix costs a byte where a set bit cost none**, so the replacement is bounded against
  the arena rather than assumed to fit.

`cmd_sed_utf8rhs` is the assertion, and it is sharp: v7's code gives a different answer.

## 5. Bounds that were announced and not enforced

Four writes had no bound at all and three more had one that did not act:

| | v7 | here |
|---|---|---|
| `dosub()`, `place()` — three sites | `if (sp >= &genbuf[LBSIZE]) fprintf(stderr, ...)` and **go on writing** | an `int` index, a diagnostic, `exit(2)` |
| `gline()` | drops every byte past `lbend` **in silence** | the same diagnostic, from the other side |
| `text()` into a `w` file name | no bound; a 40-byte row of `fname[][]`, with `fcode[]` behind it | an end pointer and `File name too long` |
| `text()` into the arena | `p > reend` tested **after** the write | before |
| `compsub()` into the arena | not tested at all | before each byte |
| `rline()` into `linebuf` | not tested at all | `sed: script line too long` |
| `abuf[]`, `a` and `r` | stored, then warned, and the slot warned about is the one the NUL terminator needs | tested first, and fatal |

The `genbuf` pair is the one worth naming, because it is `sort`'s C5d finding from a worse
angle. There, a line limit existed on one path and not the other; **here it is stated on
every path and enforced on none** — `fprintf` with no `exit`, no `break` and no `return`
behind any of the three. A bound that is announced and not acted on reads exactly like a
bound, which is `fgrep`'s C5c rule about a test that asserts a diagnostic, applied to the
code instead of the test. `cmd_sed_toolongin` and `cmd_sed_toolongout` are the pair.

**And a fourth ceiling nobody states.** `advance()` recurses once per star that consumes
something, as `grep`'s does, and §6's stack is unchecked. `MAXDEPTH` is **twelve** rather
than `grep`'s sixteen, and the number is measured:

* one `advance1()` frame is 169 words (`b6disasm`: `15 utm 0251`), its counted wrapper 7;
* **the two paths that reach it do not start from the same place** — an address match enters
  from `execute()` about 840 words in, an `s` command enters through `command()`, whose own
  frame is 540, and starts at about 1,010. So `grep`'s sixteen would have been under the
  edge on one path and over it on the other;
* **and printing the diagnostic needs the stack too**: `_doprnt`'s frame is 281 words. A
  limit set where the recursion just fits is a limit that faults while saying it was
  reached, which is exactly what the first draft did.

> **Ask what a program still has to do after it has decided to stop.**

## 6. The `l` command, which is the fifth divergence in `cmd/`

After `touch`, `rev`, `col` and `grep -b` — and `sort`'s tables. `sed.1.umm` promised `l` would
list "in an unambiguous form" with "non-printing characters spelled in two digit ascii", and
v7's table could not do both:

| | v7 | here |
|---|---|---|
| `001`–`037` | `\1` … `\37`, two digits | `\001` … `\037`, three |
| `010`, `011` | `<-`, `>-` | unchanged — `ed`'s `l` prints the same two characters |
| `012`, an embedded newline | **a raw newline** | `\012` |
| `0177` | **a raw DEL** | `\177` |
| a byte above `0177` | `\3` and friends, the octal of a negative number | passed through |

Two digits cannot spell `0177`, so the page's two promises were incompatible the moment
anything above `037` reached the routine; `ed` met the same thing and the page is corrected
the same way. The newline is the one that matters for `sed` and not for `ed`: a `sed`
pattern space **can** hold one (that is what `N` is for), and printing it raw makes the one
command whose job is an unambiguous listing produce an ambiguous one — indistinguishable
from the 72-column fold. The passthrough is `ed`'s answer and is what a console speaking
UTF-8 wants.

## 7. Six other upstream defects, fixed rather than carried

C1's rule, and the fix says which it is:

* **`compile()` treated a NUL as a no-op** (`case '\0': continue;`) and went round again. A
  NUL is the end of the script line, so `s/a` with no closing delimiter walked `sp` off the
  end of `linebuf` and compiled whatever it found there.
* **`rline()`'s two arms were not identical.** The continuation copy compared a
  backslash-escaped byte with `'0'` where the first compared it with `'\0'`, so a `\0`
  anywhere but on the first line of a `-e` script silently ended the script. They are one
  loop now, which is a fix that cannot come undone.
* **The unknown-flag diagnostic went to STDOUT** — into the middle of the edited stream —
  and then carried on with the flag consumed and ignored.
* **`advance()`'s `default:` did not return.** It printed `RE botch` and fell back into the
  `for(;;) switch`, so a corrupted opcode gave an unbounded stream of diagnostics rather
  than a stop — and a corrupted opcode is precisely the state §2's wild store left the arena
  in.
* **Three bounds were one place tighter than their own arrays**: a label may be eight
  characters (`asc[9]` holds them), there may be 49 of them, and the `NLINES` check refused
  the last slot it had just written. `maxlabellen`/`labeltoolong` and
  `maxlabels`/`toomanylabels` bracket the first two.
* **`ycomp()` walked off the end of `linebuf`** for an unterminated `y`, looking for a
  delimiter and a newline where `rline()` leaves neither.
* **Two pointers were formed before their arrays** (`lbuf - 1`, `abuf - 1`) and one string
  compare was hand-written to do it twice more; they are indices and `strcmp` now.

And **`sed` exits 2 when an input file cannot be opened**, where v7 printed `Can't open` and
exited 0 — so a script could not tell a missing input from an empty one.

## 8. What the harness could and could not say

**81 cases under `b6sim`**, of which four are sharp — restore v7's masks and widths, rebuild,
and the answer differs. They were checked that way rather than asserted to be: `utf8class`,
`utf8range`, `utf8rhs` and `utf8y` all change, and `utf8nomatch`, which was *written* as the
sharp negative on `grep`'s precedent, **does not** — §2 is the account. Checking cost one
rebuild and it corrected a claim that had already been written into three files.

**Every one of the eighty-one expectations was designed from `sed.1.umm` and the sources**, which is C5d's correction
to C5b rather than `od`'s rule — `sed`'s output is ordinary readable text, so a small
fixture with a known answer says *which* rule broke. They were then **cross-checked against
the host's `sed`** as an independent second opinion, and it disagrees about exactly six: the
two v7-only flags (`-g`, `s///P`) and the four `l` cases, which are the divergence. The
other seventy-odd agree byte for byte with an implementation sharing no line with this one.
That is [../grep/README.md](../grep/README.md)'s closing finding pointed at `sed` as
[../TODO.md](../TODO.md) asked, and it cost about a minute.

**The file oracle** ([test/run-sed-test.sh](test/run-sed-test.sh)) says four things no
stdout diff can, and the first is the one a careless test misses: **a `w` file is created
before processing begins**, which `sed.1.umm` states and which is a property of the *compiler* —
`fcomp()` does the `fopen(, "w")` while reading the script, so the file is truncated even by
a run that reads no input at all.

**And the booted test says what `b6sim` cannot**, which for `sed` is larger than for any
filter before it: **a script containing a space cannot be a `b6sim` case at all**, the
`.args` line being split with no quoting, and a substitution whose pattern or replacement
holds a space is most of what `sed` exists for. `kernel/test/filters.sh` §11 is where
`sed 's/привет мир/мир привет/'` lives, beside a `-f` script from a here-document and a `w`
file written into the image's own `/tmp`.

## 9. Two things left alone, deliberately

* **`N` at end of input discards the pattern space.** v7 sets `pending` and `delflag`, so
  the last line of `sed -n 'N;P;D'` never appears; every later `sed` prints it. It is v7's
  documented-by-behaviour choice, it is what `sed.1.umm` describes, and `cmd_sed_dcmd` pins it
  so that a later change has to be deliberate.
* **`NLINES` is unreachable.** 256 line-number addresses, against 99 commands carrying at
  most two each: 198 is the most any script can ask for, so the `Too many line numbers`
  diagnostic is dead code. It is left rather than tuned — 256 words is nothing, and the
  constant documents the field width the compiled `CLNUM` byte really has.

## 10. And one finding that is about the build rather than about `sed`

**`b6_obj`'s header dependency is the SYSTEM header tree only.** `b6cc` has no `-M`, so
every `cmd/` directory sets `KHDRS` to `include/*.h` and `include/sys/*.h` and the coarse
dependency is "all of them at once". A program whose own constants live in a header of its
own — which is every multi-file port — therefore has **no dependency on that header at
all**, and editing it rebuilds nothing.

It cost an afternoon here: every measurement of the recursion ceiling in §5 was taken
against a binary that still had the previous `MAXDEPTH` compiled into it, and the numbers
made no sense until that was noticed. `cmd/sed/CMakeLists.txt` appends `sed.h` to `KHDRS`;
**`cmd/sh` had the same blind spot over all six of its headers** and has the same line now.
