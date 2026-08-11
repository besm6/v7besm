# `make` on the BESM-6

Task C14, and the largest thing on this image after the shell: `main.c`, `doname.c`, `files.c`,
`misc.c`, `dosys.c` and `ident.c` over `gram.y` and the `defs.h` they share — v7's 2,047 lines.
It is also the **only program here whose grammar declares a `%union`**, the first that resolves a
dependency on an *archive member*, and the first that both walks a directory and forks.

[../README.md](../README.md) is the recipe and a bare `§N` below is a section of it.

## `%union`, and why it was retired first

`gram.y` values are three different struct pointers, so `YYSTYPE` is a union — the only one in
the tree. That changes what the generated parser *does*: the three value copies in
[`../yacc/yaccpar.c`](../yacc/yaccpar.c) stop being one-word moves and become aggregate copies.
Nothing here had ever compiled and run one, and the failure would have been indistinguishable
from a grammar bug.

So it was proved on a grammar of its own before this port started —
[`../yacc/rootfs/calcu.y`](../yacc/rootfs/calcu.y), `calct` again over
`%union { int i; char *s; }`, three `b6_progtest` cases and a `yacc_agree` line. It passed
first time; `b6lower`'s `gen_aggregate_assign()` was already right. `../yacc/README.md` under
"The contract" is the note, and this is the last of C10's risks to be spent.

The grammar itself needed **no change at all**. `b6yacc` takes `%term`, a comma-separated
`%type` list, the old `= {` action syntax and a `%{ … %}` block after `%%`. `y.output` reports
**zero conflicts**, and a number other than zero would be a change to the grammar.

What did change is the C either side of it: `y2.c` emits `int yylex(void);` and
`void yyerror(char *);`, so both had to be spelled that way.

## `defs` was not quite §1's trap

[TODO.md](../TODO.md) expected the classic one — a v7 program defining its globals in a header
the PDP-11 linker merged. `defs` does not: every shared object is already `extern` there and
`main.c` defines it. What it *did* have was a dozen `extern` declarations buried inside function
bodies and three globals in `files.c` that were never `static`. Those moved to `defs.h`, and
`b6nm -n` over the finished binary shows one definition each of `funny`, `mainname`, `firstname`,
`sufflist`, `firstvar`, `firstpat`, `firstod`, `linesptr`, `fin`, `zznextc`, `yylineno` and
`builtin` — §1's check for a program in more than one translation unit.

The header is `defs.h` and not `defs` because the generated parser is compiled out of
`gram.yacc.dir`, not out of this directory, so the include is found by `-I` rather than by
proximity; a name CMake and `b6cpp` both recognise is worth more than the original spelling.

**Bit-fields went.** `struct nameblock` packed `done` and `septype` into three bits each and
`struct varblock` `noreset` and `used` into one. `b6parse` accepts the syntax and then drops the
width before layout — `new_member()` takes a name, a type and an offset and no width — so each
already occupied a whole 48-bit word. Saying so costs nothing and stops the program depending on
undocumented behaviour. There is no space pressure here: a `struct nameblock` is 6 words.

## Directories

`srchdir()` is what expands a `*`, `?` or `[…]` in a dependency list, and what lets an implicit
rule find a source nobody named. v7 `fread`s 32 raw `struct direct` at a time and copies each
name through a 15-byte buffer. `DIRSIZ` is **18** here and a name off the disk carries no
terminator, so that could not stand: it goes through `opendir(3)` (§5). `struct opendir` holds a
`DIR *`, the `fseek(dirf,0L,0)` that restarted a scan is `rewinddir()`, `fileno()` under `-p` is
`dirfd()`, and `doclose()` — which closes the open directories in the child before it execs — is
`closedir()`. `readdir()` already skips the free slots and plants the terminator, so the copy
loop simply disappears.

`amatch()`, the glob matcher stolen from `glob` through `find`, **recursed once per matched
character**. It iterates now. §6's stack ceiling is unchecked by the harness and a name is 18
characters, so 18 frames per pattern was 18 too many to leave to chance; the recursion that
remains is one level per `*`.

## Archives

`lib.a(member)` and `lib.a((entry))` are dependencies on a file inside an archive and on the
object that defines a symbol. v7 reads them by `fread`ing `struct ar_hdr`, `struct exec` and
`struct nlist` straight off the disk. None of those is this machine's layout — every field is a
six-byte word — and the target has no `<ar.h>` or `<a.out.h>` at all.

So `lookarch()` reads them through **`cmd/libaout`**, and the walk is
[`../nm/nm.c`](../nm/nm.c)'s: `fgetw()` for the `ARMAG` word, `off = 6L` for the first member
header, `fgetarhdr()`, `off = ar_size + ftell()`, then `fgethdr()` and `fgetsym()` for an entry
point. `b6_prog()` is handed `B6_LIBAOUT_SOURCES_NATIVE` — the `_NATIVE` subset, the four
file-descriptor routines being `ar`'s and `ranlib`'s alone.

One thing got *better* rather than merely different. Our `ar_name` and `n_name` are malloc'd
NUL-terminated strings, so v7's `eqstr(a, b, 14)` and `eqstr(a, b, 8)` become `strcmp`: a member
name and an entry point are compared **entire**, where v7 compared the first 14 and 8 bytes and
silently conflated anything longer.

## The default rules

`builtin[]` is trimmed to the toolchain that is actually on this disk — `cc`, `as`, `yacc`,
`lex` — and v7's `.f`, `.r`, `.e`, `.yr` and `.ye` rules are gone with the `f77` that is not
here, as are the `-ly` and `-ll` flags with no archive behind them. A rule only fires when a
matching source exists, so carrying them would have been harmless and useless both; C13's
precedent is to delete a dead arm rather than carry it. The `#ifdef vax` arm went with them, and
it is how one knows nobody ever compiled it: it reads `"AS=as".` — a full stop where the comma
should be.

`/usr/bin/cc` on this image **cannot compile C** — `b6parse`, `b6lower` and `b6codegen` are host
programs — so the `.c.o` rule fires and `cc` says so. That is a property of the image and not of
`make`, and it is why the transcript below runs the implicit rule under `-n`.

## The stack

`doname()` recurses once per level of the dependency graph, and this is §6's unchecked ceiling
rather than the two `rootfs_make_size` asserts. From `make.dis`, against 4,096 words:

```text
doname     214 words   × once per level of the graph
implicit    60
makeit      48
srchdir    134
subst      116         × once per nested macro reference
yylex       66
lookarch   160
docom       36
main       139
```

v7's `doname()` measured **310**, and two blocks of it never recurse: the suffix-rule search and
the command run. Both are functions now — `implicit()` and `makeit()` — so their words are on
the stack while they run and not once per level of the graph. That is the whole of the
difference, and it is worth having: the frame is line-count-driven here, at §6's 1.5–2 words per
source line, so shortening the recursive function is the only lever there is.

At 214 words a level the graph may be **11 deep**, and `doname()` says so rather than letting
the machine find out: 12 levels of frames, `main`, and the deepest tail below them (`implicit` +
`srchdir` + `readdir`, or `docom` + `docom1` + `printf` + `doprnt`, either about 400 words) come
to some 3,100 of 4,096. Without the bound the failure is not a diagnostic — b6sim reports a
stack protection violation, and the kernel would fault — which is exactly §6's point. Raising
the bound means shrinking the frame first; the arithmetic is here so that it can be redone.

`subst()`'s own recursion, one level per nested macro reference, v7 bounded at 100. At 116 words
that is 11,600, so it is 20 here.

## Eight bits, and the bounds v7 did not check

`funny[]` classifies a character as a metacharacter or a terminator and is indexed by a `char`.
A `char` is **unsigned** here, so the table is 256 entries and not v7's 128 (§11) — a target, a
dependency and a command may be Russian, and the transcript's aren't only because the `.mk`
fixtures say them in English. `cmd_make_eightbit` is the case.

Six bounds are checked that were not:

* **`subst()` had no bound at all** on the buffer it writes, and neither did the caller. It
  takes the end of the buffer now, and so does the macro *name* it accumulates. The
  description-line path in `nextlin()` is bounded to match.
* **A word in `yylex()`** could run off `word[INMAX]`.
* **`mkqlist()` returned nothing whatever** on the empty chain — a `char *` function whose
  `p == NULL` arm is a bare `return;` — and on a full one it wrote `*--qbufp` when `qbufp` was
  still `qbuf`, forming a pointer below the array (§2).
* **`namelist`** counted a left-hand name and *then* checked `NLEFTS`, so the fortieth wrote past
  `lefts[]`.
* **`doexec()`** filled `argv[200]` with no limit.

And three that are not bounds:

* **`sprintf(3) returns an `int`.** v7's returned its buffer, and `fatal1()` and `yyerror()` both
  passed the result straight to `fatal()` as a string.
* **A built-in rule is copied before it is parsed.** `nextlin()` handed `eqsign()` a pointer into
  `builtin[]`, which writes a NUL into it — into a string literal, which on this machine is in
  the const segment. v7 wrote to literals freely. The copy also means `retsh()` can stop
  special-casing the built-in case and always `copys()`.
* **A failed `exec` in the child called `fatal()`**, which is `exit(1)`: a fork's child flushing
  the parent's stdio buffers, printing whatever the parent had pending a second time. It is
  `_exit()` now, as [`../cc/cc.c`](../cc/cc.c)'s `run()` already had it.

## Signals, and waiting without `waitpid()`

`(int) signal(SIGINT, SIG_IGN) & 01` is a PDP-11 pun on a function pointer for "was it already
ignored"; it is a comparison against `SIG_IGN` now, and `enbint()` takes the `void (*)(int)`
this `<signal.h>` uses.

`await()` needed no rethinking, which is the interesting part: `while ((pid = wait(&status)) !=
childpid)` is already the shape a system with **no `waitpid()`** wants, and it is what
`../cc/cc.c`'s `run()` arrived at independently. The global it tests was v7's `waitpid`, renamed
`childpid` so that nothing reads as a call to a function that is not here. `EINTR` is retried,
which v7 did not do.

## The measurements

```text
const   text   data   bss     dec
  139   8635   1464  2577   12815 words        ceiling 28,672
```

Comfortable, and the second ceiling — no relocatable symbol above word 32,767 — is not close
either. No struct is anywhere near §6's 4,096-word limit: the largest is `struct nameblock` at 6
words. The three `INMAX` scratch buffers in `gram.y` (750 words) and `docom()`'s `OUTMAX` line
(417) are at file scope rather than in a frame, which is where the stack table above got its
room.

On the disk `make` costs **24 blocks**, and the image went from 345 free to **321**.

## What this harness cannot say

`cmd/make/test/` is 26 `b6_progtest` cases, and **every one passes `-n` or reads a description
file and stops**. Two limits put them there, and both are the simulator's rather than the
program's:

* **Under b6sim every directory looks empty.** The host's `open()` and `fstat()` succeed on a
  directory and only `read(2)` refuses, so `opendir()` returns a good `DIR` and the first
  `readdir()` returns NULL — indistinguishable from an empty directory.
  `lib/test/progs.cmake` says it at length for `dirt`. So neither the `*.c` expansion nor the
  implicit-rule search that finds a source nobody named can be a case here, and `suffix.mk`
  names its source so as not to need one.
* **The shell b6sim would reach is the build machine's**, which is not a BESM-6 a.out. b6sim
  does implement `fork(2)` and `exec(2)`, so this is about `/bin/sh` and not about the harness.

Which leaves the whole of what a command *does* to a booted image. Single-user, in `/tmp`:

```text
# echo one >alpha.c; echo two >beta.c
# cat >mk1
all: *.c
	echo GOT $?
# make -r -f mk1 all
No suffix list.
echo GOT beta.c alpha.c
GOT beta.c alpha.c
```

That is `srchdir()` reading a real directory, and `doexec()` — the command has no
metacharacter, so there is no shell in it at all, just `execvp`. The other half:

```text
# cat >mk2
sh:
	echo through the shell >shout; cat shout
# make -r -f mk2 sh
No suffix list.
echo through the shell >shout; cat shout
through the shell
```

`;` and `>` are metacharacters, so `metas()` sends that one to `/bin/sh -ce`. Then an implicit
rule whose source is named nowhere — only the directory scan can find `alpha.c` — with the
built-in rule table supplying the command:

```text
# cat >mk3
prog: alpha.o
	echo LINK $?
# make -n -f mk3 prog
cc  -c alpha.c
echo LINK alpha.o
```

And the exit status of a command that fails, which is `docom1()` reading `status >> 8` out of
what `await()` returned:

```text
# make -r -f mke bad
No suffix list.
test 1 -eq 2
*** Error code 1

Stop.
# make -r -f mke ign
No suffix list.
test 1 -eq 2
*** Error code 1 (ignored)
echo carried on
carried on
```

Still unasserted anywhere, and worth knowing: **the interrupt path**. `intrupt()` unlinks the
target it was building unless `.PRECIOUS` names it, and neither a `^C` mid-command nor
`.PRECIOUS` protecting a target from one has been exercised by anything but reading. `-t`'s
`touch()` is exercised only as far as the message it prints, `-t -n` being the only form of it
that is idempotent enough to have an expectation.

## What did not need saying

No `%D` or `%O` (§3) anywhere in these 2,047 lines; the only conversion of a `long` is `%ld` for
a file's time, and a `long` is one word. Nothing needed `-I` for `y.tab.h`, no unit outside the
grammar naming a token. Nothing here is setuid (§8), and nothing is `PURE`: `make` runs once and
exits.
