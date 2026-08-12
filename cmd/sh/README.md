# sh — the Unix v7 Bourne shell for the BESM-6

`/bin/sh`: S. R. Bourne's shell, compiled by the `b6*` toolchain and staged as
**`build/rootfs/bin/sh`** — the shell `/etc/init` execs to bring the system up single-user
([`../init/README.md`](../init/README.md), kernel task 24).

Like [`cmd/init/`](../init/), this is a `cmd/` subdirectory that is **not a host tool**. Nothing
here runs on the build machine.

## Building

Part of the ordinary top-level build; there is nothing to invoke separately.

```sh
make            # builds build/rootfs/bin/sh among everything else
make run        # runs its size check and its ten b6sim tests, with the rest
```

Two conditions gate it, both shared with `kernel/` and `lib/`: the external
[c-compiler](https://github.com/besm6/c-compiler/) must be installed, and libc must build, since
the link is `crt0.o *.o -lc -lruntime` in that order. [`CMakeLists.txt`](CMakeLists.txt) is one
`b6_prog()` call plus `-I.`, which matters: `defs.h` includes `"ctype.h"`, and that must resolve
to the shell's own character tables rather than the C11 `<ctype.h>` in the system tree.

It uses **9,008 words of the 28,672** available, with the highest relocatable symbol well inside
the 32,767 a 15-bit pointer reaches. It was 7,971 until task C24; 1,028 of the difference are the
subject of "Globbing" below and the last nine are task C29's, in the section after it. **The
number that decides what this program can do is not that one** — it is the frame of `execute()`,
and it has a section of its own.

## What the port changed

The sources are v7's, and the shell they build is v7's bar one deliberate addition — the comment
character, below. Two separate kinds of change were needed to get there.

### C11

`b6parse` is strict C11: no implicit `int`, no K&R parameter lists, no untyped `register i;`. So
every function has a prototype and an explicit return type, and the file-local ones are `static`.

**The ALGOL-68 macro dialect is gone.** `mac.h` defined `IF`/`THEN`/`ELSE`/`FI`,
`SWITCH`/`IN`/`ENDSW`, `REP`/`PER`/`DONE`, `LOOP`/`POOL`, `BEGIN`/`END`, `ANDF`/`ORF`/`NEQ` —
Bourne wrote the shell in it, and it preprocesses to perfectly legal C11. It was removed anyway,
because the pointer work below has to be read and reviewed and cannot be while the control flow is
spelled in macros clang-format will not format. Only the value macros survived, into `defs.h`.

Three things that the rewrite could have broken silently, and did not:

* **`VOID` was a typedef for `int`, not for `void`.** Exactly one function relies on it:
  `cmd.c`'s `skipnl()` is declared `LOCAL VOID` and its *returned value* decides whether
  `for x in a b c` parses as a list or as a bare `for x`. It is `INT` now.
* **Every `switch` has a deliberate fall-through**, and `SWITCH`/`IN`/`ENDSW` hid them all. The
  important one is `case TCOM` falling into `case TFORK` in [`xec.c`](xec.c) — that is how every
  external command gets run. Each one is marked in the source.
* **`BOOL` must stay an integer type.** The name says boolean, but `trapnote`, `trapflg[]` and
  `exfile()`'s `prof` parameter all carry multi-bit flags; `exfile(ttyflg)` ORs 040 straight into
  `flags`. Under `bool` that becomes 1 == `noexec`, and the shell would parse everything and
  execute nothing with no diagnostic.

`defs.h` no longer **defines** the shell's globals. v7 declared some sixty of them in that header
— `INT flags;`, with no initializer and no `extern` — and let the linker merge the twenty
copies into one common block. C11 has no common block, so they are `extern` there and defined once
in the new [`glob.c`](glob.c). Two were defined *twice* in v7 (`nosubst`, in `io.c` and
`service.c`) and the expression stack's four pointers were declared in `stak.h` and defined
nowhere at all.

Three declarations the header tree did not have, added the way `cmd/init` added `kill()` and
`<sys/wait.h>`: **`ioctl()`, `stty()` and `gtty()`** in [`<sgtty.h>`](../../include/sgtty.h) — the
header exists for those calls and declared none of them, so `lib/libc/gen/isatty.c` still writes
its own `extern` — and **`times()`** in [`<sys/times.h>`](../../include/sys/times.h), guarded
against the kernel's same-named handler exactly as `<sys/stat.h>`'s block is.

And `typedef struct sysnod SYSTAB[]` had to go: an array of an incomplete type is a C11
constraint violation, and `struct sysnod` is not defined until further down `mode.h`. The two
keyword tables are `SYSNOD[]`.

### The machine

A word is 48 bits and the machine is word-addressed. `sizeof(int) == 6` char-units — one word —
`char` is unsigned, `char *` and `void *` are **fat pointers** (a bit-48 marker, a byte offset, a
15-bit word address), and every other pointer is a bare word address in bits 15–1
([`doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md) §7–8). The v7 shell
is written for a machine on which an `int` and a `char *` are the same thing, so this reaches
further into it than into `init`.

**A flag packed into a pointer goes in bit 16, never in bit 0.** Bit 0 of a word address names the
neighbouring *word*. Two places did it: `blok.c`'s `BUSY` and `service.c`'s `ARGMK`.
[`lib/libc/gen/malloc.c`](../../lib/libc/gen/malloc.c) is v7's *same* allocator with the same fix
already made, and carries the full account; this port follows it rather than restating it. v7's
`Lcheat`/`Rcheat` puns existed only to do this and are gone from `mode.h`.

**Rounding to a word is not a bit mask.** `BYTESPERWORD` is 6, so `& ~(6-1)` is not a rounding
operation at all — it rounds 7 to 8. v7's `round()` is two macros in `defs.h`: `sizeup()` for byte
counts, and `wordup()` for pointers, which works *because* the `char *` → `int *` cast floors, so
adding 5 and flooring is exactly a ceiling. `stak.c`'s `brkincr` grew in steps of 256 in v7, into
768 and 1280 and 1792 — not powers of two, so that mask was already wrong on the PDP-11.

**Every cast from `char *` to a node pointer floors.** It drops the byte offset and keeps the
word; it does not round. The whole parse tree and argument list is built by casting stack
addresses to `ARGPTR`/`COMPTR`/`STRING *`, so the port depends on an invariant that `defs.h` now
states outright: **`stakbot`, `staktop` after `endstak()` or `getstak()`, and every `shalloc()`
result sit at byte #0 of a word.** `endstak()` is the line that keeps it.

**An offset is not a pointer.** `relstak()` yields a stack offset, and v7 stored it in a `STRING`
at three sites, because a byte offset and a `char *` are the same sixteen bits on a PDP-11. Here
an integer cast into a `char *` is not a pointer at all. `absstak()` takes an `INT`.

Smaller ones: `addblok()` added a byte count to the integer value of a word pointer, and built its
end sentinel out of `end+1` — one byte past `end`, which was below `bloktop` and had bit 0 set,
and neither property survives here. `sbrk()` reports failure as `NULL`, not `(char *)-1`, so
`setbrk()` returns a plain flag and nobody has to cast a break address to an `int`. `execve` is
spelled `exece`. `itos()` printed at most five
digits and turned a negative into a very large positive; an `INT` here holds a 41-bit signed
value. `prc()`'s parameter must stay a `char`: `&c` on a standalone `char` is a fat pointer at
byte #5, and widening it to `int` would make every `prc()` write a NUL. And `CPYSIZ`, the amount
`subst()` buffers, was 512 because that was one PDP-11 disk block; here a block is `BSIZE`, 3,072
bytes, and 512 is not even a whole number of words — the same conversion `<stdio.h>` makes for
`BUFSIZ`, and it is written as `BSIZE` now rather than as a number.

Names that collided with libc: the arena's `alloc`/`free` (v7 `#define`d them to `malloc`/`free`,
so the shell *defined* both) are `shalloc`/`shfree`; `getenv`/`setenv` are `readenv`/`shenv`;
`io.c`'s two-integer `rename` is `shrename`; `FILE`, `BUFSIZ` and `EOF` are `SHFILE`, `SHBUFSIZ`
and `SHEOF`, since `<stdio.h>` spells the last two 3072 and −1 and either would be silent.

### Eight bits — where the quoting mark went (task C11)

This is the largest single change in the port and the only one that touches every file that
handles a character. **v7 marked a quoted character by setting bit `0200` of it**, and
`trim()` cleared that bit from every word on its way to `argv`. That costs the eighth bit of
every byte the shell handles. Below the shell this machine is byte-transparent in both
directions and its text is UTF-8 ([`kernel/dev/sc.c`](../../kernel/dev/sc.c)) — so `cat` carried
Cyrillic and `echo привет` did not, because the argument reached `/bin/echo` with every second
byte bent into an ASCII letter. `0200` was overloaded a second time in
[`expand.c`](expand.c), as an internal "this position held a `/`", and the mark was written
**into the here-document temp file** as well as onto the stack.

**The mark is a byte of its own now, and there are two representations of it.** Keeping them
apart is the whole of the design; [`defs.h`](defs.h) states it and this is the summary.

*In flight* — a character in an `INT`: a register, a return value, `peekc` — the mark is still a
bit, `QUOTE`, moved from `0200` up to `01000000`. That value is not free choice: `wdval` and
`peekc` share one integer space with `SYMFLG`, `EOFSYM`, `SYMREP` and `MARK`
([`sym.h`](sym.h)), and it sits clear of all four, so no argument about which branches are
reachable has to be made. An `INT` is a 48-bit word; the headroom costs nothing.

*In storage* — the expression stack, the `argnod` words built on it, and the here-document temp
file — the mark is a **prefix byte `QESC`, `0377`**:

| | stored as |
|---|---|
| an ordinary byte `c` | `c` |
| a quoted character `c` | `QESC c` |
| a literal `0377`, quoted or not | `QESC QESC` |
| the empty quoted word (`""`) | a lone `QESC` just before the terminator |

A bare `QESC` never appears except in that last case, which every decoder reads as "contributes
nothing, end of string" — it is exactly what v7 spelled as a lone `0200`, and `macro()` still
pushes it for the same reason: `x=""; echo "$x"` must produce an empty *argument* and not no
argument. Quoted and unquoted `0377` deliberately share one encoding, because the quotedness of
`0377` cannot be observed (it is not a metacharacter, not a glob character, not in the default
IFS) and collapsing them is what makes `nosubst` derivable: *a `QESC` whose payload is not
`QESC`*.

**Three invariants**, each of which is a thing that will break if it is forgotten:

1. **The stack holds encoded text.** Anything entering it from outside — a variable's value, a
   command substitution's output, a directory entry's name, a here-document line — is encoded on
   the way in by `putq()` ([`stak.c`](stak.c)). `trim()` ([`service.c`](service.c)) is the one
   decoder on the way out to `argv`, and `nextq()` ([`string.c`](string.c)) is the one reader
   everything else uses.
2. **Script text is never encoded.** `readc()` decodes only when the input it is reading is
   flagged `fencd` ([`mode.h`](mode.h)) — which is `macro()`'s re-read of a word it pushed, and
   `subst()`'s read of a here-document temp file, and nothing else. A `0377` in a script is a
   byte of somebody's data. And a parse-tree `argval` is never mutated, which is what lets
   `trim()` compact in place: `macro()` runs over the same word again on every pass of a loop.
3. **The name tree holds raw values.** Two v7 paths stored a *marked* value and are normalised
   here; both are behaviour changes and are listed under Known limits below.

Two of the rewrites are worth naming, because in both cases the v7 code cannot simply be
adjusted:

* **Nothing may be scanned backwards any more.** In a variable-width encoding a byte is a
  payload or a mark according to what stands in front of it, so `expand()`'s search back from
  the first metacharacter for the directory slash is one forward pass now, and `comsubst()`'s
  walk back over trailing newlines is an offset remembered on the way forward. That second one
  is not a stylistic preference: inside `"` `` ` `` … `` ` `` `"` every output byte is laid down
  as `QESC d`, so the byte before a newline is a mark; and a `0377` in the output is laid down
  as `QESC QESC`, so the byte before a mark may be a payload.
* **The character tables are 256 entries, and this is the sharpest edge in the task.** v7 could
  stop at 128 because the `(c & QUOTE) == 0` test in front of every subscript in
  [`ctype.h`](ctype.h) *was* the bounds check — no marked character ever reached a table. The
  mark is above the byte now, so a Cyrillic byte arrives as an ordinary subscript in
  `0200..0377`. Nothing above `0177` is special to the shell, so the upper half is zero; it has
  to exist. 126 words, against a range test at seventeen call sites.

Two things the encoding costs, and one it does not. It costs a byte per quoted character on the
stack, which the arena absorbs; it costs **43 words of image**, taking the shell from 7,928 to
7,971 of the 28,672 available. It does not cost the pattern language anything, but it does make
one of its properties visible for the first time: **`?` and `[...]` match a BYTE**, so `приве?`
does not match `привет`, whose last letter is two bytes. That is the honest reading of an
eight-bit-clean v7 globber and `test/utf8.sh` asserts it rather than working around it.

### Three changes of substance

Everything above preserves what the shell does. These three do not. Two are about running out of
room in a 28-page address space; the first is a feature v7 had not got.

* **The comment character.** A `#` where a word would start now begins a comment that runs to end
  of line, wherever the shell reads. That arrived in System III, and it is taken
  here because what it replaces is worse than ugly: the v7 stand-in for a comment is a `:` line,
  and **its words are still parsed**, so a backquote in one starts a command substitution that
  runs to end of file and a parenthesis is a syntax error. Every script on this image was written
  around that.

  The whole of it is six lines in [`word.c`](word.c), between the whitespace skip and the
  `eofmeta()` test — the one place a `#` can be seen at the start of a word, and the only place it
  needs to be seen. A `#` anywhere else has already been taken by the word loop (`echo a#b` is
  literal), by the two quoting arms, or by `nextc()`'s backslash, which hands it back as `0243`
  and never as `'#'`; `$#` is `macro.c`'s and here-document bodies are `copy()`'s, and neither goes
  through `word()`. **Neither character table changes.** All eight bits of `_ctype1` are taken and
  the only reusable one, `T_MET`, would make `eofmeta('#')` true — which breaks `echo a#b` *and*
  sends `#` down the symbol path; a `c == COMCHAR` compare costs nothing, and measurably nothing:
  the linked image is the same size to the word.

  Three details in those six lines are contracts. It reads with `readc()` and not `nextc()`, which
  would eat a `\`-newline and pull the next line into the comment. It stops on `SHEOF` as well as
  `NL`, since `readc()` returns `SHEOF` for ever once the input is spent — without that a comment
  on an unterminated last line spins. And it *falls through* with the newline in hand rather than
  looping back, which is what makes a comment line indistinguishable from an empty one, pending
  here-document flush included. `test/comment.sh` drives all three, and ends without a final
  newline on purpose.

  **It used not to get you a comment you could type.** While `CERASE` was v7's `#`
  ([`include/sys/tty.h`](../../include/sys/tty.h)) the line discipline ate a typed one, and the
  character in front of it, before the shell was handed anything — `kernel/test/console` asserted
  exactly that from task 25b, typing `echo ab#c` and getting `ac` back. The erase character is
  `^?` now and the kill character `^U`, so a `#` typed at the prompt reaches the shell like any
  other character and comments work wherever the shell reads.

* **`gmatch()` recursed once per character** of an unbounded pattern. The user stack is four
  pages, and a pattern of a few hundred characters would have run off it with no diagnostic. Its
  three tail calls are loop iterations now; the fourth, in `*`, is not a tail call and stays,
  bounded by the number of stars.
* **`chkstak()`.** `locstak()` guarantees one increment of headroom when a stack item is
  *started*, and the loops that then fill it push as many characters as the user's data has, with
  no further check. v7 got away with that by arrangement: running off the break raises a memory
  fault, and `fault()` extends the arena and lets the faulting store re-execute. That arrangement
  is kept — but it needs the kernel to restart the faulting instruction, which nothing in this
  port has demonstrated yet, and under `b6sim` there is no signal delivery at all. So every
  unbounded push tests first. `test/nospace.sh` is what proves it: without the test that script
  walks out of the address space, and with it the shell says `no space`.

## Globbing, and the 1,028 words `opendir(3)` cost

Task C24 put [`expand.c`](expand.c) on the library. It fixed a live bug and it was the most
expensive of that task's seven conversions by a factor of four; both facts are worth the space.

**The bug.** v7 terminated the name once, before the loop:

```c
entry.d_name[DIRSIZ - 1] = 0;   /* to end the string */
```

That works only if the write lands **outside** what `read()` overwrites, and in v7 it did: the
file declared its own `#define DIRSIZ 15` against a fourteen-byte on-disk name and read sixteen
bytes, so index 14 was the pad byte. The port changed both numbers and kept the line. `DIRSIZ`
is 18 and `DIRENTSZ` is 24, so index 17 is the **last byte of `d_name`**, the write happened
before any read, and the first read wiped it. It was never written again. So a file name of
exactly eighteen characters reached `gmatch()` and `addg()` unterminated, and both scan to a NUL.

**What it did then depended on what followed `entry` in the frame**, which is why nobody had
noticed: `struct direct` was declared next to a `STATBUF`, and when the byte after the name
happened to be zero — which it was in the session C24 verified against, where an 18-character
name globbed correctly on the *unconverted* shell — the bug was invisible. That is the worst
shape a memory bug takes, and it is the argument for the library rather than for a one-line fix
in place. `readdir()` ends it: the name arrives terminated, with `d_namlen` beside it.

Three other things went with it. The `stat()`+`S_IFDIR` test before the `open()` is `opendir()`'s
own business, so the `STATBUF` left a frame that recurses. `open(plain, O_RDONLY) > 0` — which
would have rejected fd 0 — became a NULL check. And the `d_ino == 0` test is the library's.

**The interrupt seam survived.** The loop still reads

```c
while ((dp = readdir(dirp)) != NULL && (trapnote & SIGSET) == 0)
```

so a globbing shell is still interruptible between entries, which is the reason `cmd/README.md`
gave for possibly leaving this file alone. What changed is only that `readdir()` may block in
one buffer-sized `read(2)` where the old loop blocked in a 24-byte one — latency, not
correctness.

**The cost.** 7,971 words to 8,999. Only about 230 of that is the directory library; the rest is
`malloc`, `free` and `realloc`, which **this shell had never linked**. It allocates through
`brk(2)` and its own `locstak`/`endstak` arena — that is the whole subject of the section above —
so the allocator was 700-odd words of libc that no other call reached. `opendir()` calls
`malloc()` twice, and that one edge pulled the lot in. It is the sharpest illustration in the
tree of §6's rule that what a program *links* is decided by its smallest call, and it is why
`ttyname(3)` deliberately does not use this library either: `/bin/login` and `/etc/getty` should
not carry an allocator to scan `/dev`.

## The stack, and what a nested script costs

Task **C29**. Not the expression stack of the section above — the **machine** stack, the one the
C calls run on, four pages at `070000` and 4,096 words in all. It is the scarcest thing this
program has, and until C29 nothing in the tree said so.

**`execute()` recurses once per node of the parse tree**, so its frame is resident once per level
of nesting. v7 wrote it as one 404-line function, and this machine gives a frame **one slot per
compiler temporary and never reuses one**, so the 200-line built-in switch was reserved on every
recursion whether or not `case TCOM` was the arm being taken. Measured in the `.dis` the build
writes:

| | `execute` frame | words per level of nesting | levels that fit |
|---|---|---|---|
| v7's shape | 402 | 410 | **8** |
| split | 99 | 107 | **16** |

Sixteen and not thirty-eight, because **once `execute()` is down to 107 the PARSER is the deeper
of the two walks**: `cmd → list → term → item → cmd` is about 150 words a level, and past four
levels of nesting it is what the budget is being spent on. That is why `deepchk()` is called from
`cmd()` as well, and it is where the next few levels are if anybody ever needs them.

The split is four `static` functions in [`xec.c`](xec.c) — `docom()`, `dofork()`, `dofor()` and
`doswitch()`, one per big arm — and nothing else: the code inside them is v7's, moved. Only one
arm is live at a time, so a function each costs one frame instead of all of them at once. The
whole change is +9 words of image. The one delicate part is v7's fall-through from `TCOM` into
`TFORK`, which is how every external command is run: `docom()` returns whether the command was a
built-in and hands the argument vector back for the child to exec.

**The ninth level did not fault; it rewrote the program.** A user address is 15 bits and the
process owns all 32 pages, so a store past `077777` wraps mod 2^15 onto word 0 — which is the
shell's own const image. Hence the way it was reported: `** SIGNAL 8 **`, the message tables
printed to the console as text, and `cannot shift` from a builtin the script never called. That
is `syslook()` reading a table that has been overwritten.

**Two known limits were this, and both are closed.** v7's `calendar(1)` — a `case` arm holding a
`while` whose body holds an `if` and a pipeline — did not run on this machine at all (task C23,
[`../calendar/README.md`](../calendar/README.md)); and a pipeline of four or more stages *inside
a command substitution* killed the shell with `SIGNAL 4` (task C8, undiagnosed and written down
here as its own corner). They are one bug: four levels of nesting plus the frames a substitution
adds, against a budget of eight.

**Why no test had ever seen it, and how to measure it.** The b6sim harness runs `env -i`, and the
argv/envp block sits at the **base of that same stack** (`argc` is at absolute `070000`,
[`kernel/sys1.c`](../../kernel/sys1.c)) — so a login shell on the booted machine had several
hundred words fewer to spend than the test rig did. The calendar script measures 3,737 words of
4,096 with an empty environment: it *passed* here and died there. **Padding the environment is
what brings it back onto the host**, and that is the technique worth keeping for the next one of
these — `b6sim` passes a whitelist of the host's variables through, so any two of them will do:

```sh
P=$(python3 -c "print('x'*1200)")
env -i EDITOR="$P" PAGER="$P" b6sim ./sh ./repro.sh
```

The shell before the split answered that with `Division by zero` at 1,200 bytes of padding — the
machine's `** SIGNAL 8 **` — with a hang at 1,500, and at 2,500 with
`Illegal instruction 002 reg/mod @00616`, which is the program executing its own const segment.

The measurement itself is `b6sim -d`: the trace prints every write to `M17`, and the largest of
them less `070000` is the peak.

```sh
b6sim -d --trace=t.txt ./sh ./script.sh
grep -o 'M17 = [0-7]*' t.txt | sort -u | tail -1
```

**And the shell now checks the ceiling itself**, because nothing else on this system can:
`deepchk()` ([`error.c`](error.c)) at the top of `execute()` and of `cmd()` — the parser fails the
same silent way, and past four levels it is the one doing the spending —
compares the address of a local against the floor `main()` recorded and says `too deep` rather
than wrapping. The bound (`STAKROOM`, [`brkincr.h`](brkincr.h)) is **worst case rather than
exact**, and cannot be otherwise: the shell would have to name the address `070000` to know how
much of the stack the argument block below it took, and fabricating a pointer out of an integer
is the thing this port does not do. So it is 4,096 words less the 854 (`NCARGS`) that block can
be at its largest, less room for `error()` to report it — 2,800 words, which is sixteen levels of
nesting where a script that means anything uses four.

## Tests

Ten, under `b6sim`, run by `make run` (ctest labels `sh` and `rootfs`):

| test | what it covers |
|---|---|
| `sh_smoke` | sourcing with `.`, assignment, `${-}`/`${+}`/`${=}` substitution, quoting, positional parameters and `shift`, `if`/`for`/`case`/`while`/`until`, `export`/`readonly`/`umask`/`trap`/`set`, and the exit status |
| `sh_syntax` | the parser's error path — a truncated `if` |
| `sh_heredoc` | here-documents: `copy()` and `subst()`, a quoted terminator, and a document longer than `CPYSIZ` so the flush boundary is crossed — and with them `fork`, a subshell and file redirection |
| `sh_script` | running another shell script: arguments, PATH search, exit status, and a child that forks in turn |
| `sh_comment` | the `#` comment character: where it does *not* start one (inside a word, inside either quote, after a backslash, in `$#`, in a here-document body), the `\`-newline that must not continue it, and the unterminated last line that must not spin |
| `sh_utf8` | task C11: the four ways of quoting a Cyrillic word, substitution, splitting, `case` with `*`/`?`/`[...]` and a quoted wildcard, both kinds of here-document, command substitution, the empty quoted word, and the byte `0377` -- the one value the stored form has to write twice |
| `sh_nospace` | the arena and the break, to exhaustion |
| `sh_nest` | task C29: twelve levels of `case`/`if`/`for`, where the shell before the split of `execute()` died at nine — and died by rewriting its own const image, not by saying anything |
| `sh_toodeep` | the far end of the same budget: an `eval` that evals itself for ever must produce `too deep` and status 1 |
| `sh_subpipe` | a pipeline of four and of five stages inside a command substitution — task C8's `SIGNAL 4`, which was the same stack |

`b6sim` runs one BESM-6 `a.out` and services its syscalls on the host, which is enough for the
lexer, the parser, macro expansion, the name tree and — underneath all of them — the arena and
the expression stack, which is where this port's pointer work lives.

`fork`, pipes and file redirection **are** reachable, which is not obvious and was worth
finding: v7's shell refuses to redirect a built-in (`illegal io` in [`xec.c`](xec.c)), but a
`( )` **subshell** may be redirected and the built-ins inside it still print. `sh_heredoc` is
built on that, and it is what covers `copy()`, `subst()`, `initio()` and the fork path.

**So is running another shell script**, which is the whole of `sh_script`. It is worth being
precise about how, because it is not what a modern reader expects:

> **There is no `#!` on this system.** The shebang is a 4.xBSD invention; v7 had none, and
> neither this kernel's `getxfile()` ([`kernel/sys1.c`](../../kernel/sys1.c)) nor `b6sim`
> implements it. What runs a script is the **shell**: `exec` comes back `ENOEXEC` — the file
> exists and is readable but is not a binary — and `execs()` in [`service.c`](service.c) takes
> that as "this is a script", points its own input at the file and `longjmp`s back to the top of
> `main()`. The forked child *becomes* the shell that runs it.
>
> A `#!/bin/sh` line is therefore not magic, and it does nothing: since this shell has a comment
> character it is simply a comment. It used to be a command that was not found, printing one
> spurious error per run, which is why nothing on this image carries one.

Making that work needed a fix in `b6sim`, not in the shell: `sys_exec()` was reporting `ENOENT`
for *every* exec failure, including "not a BESM-6 a.out". A shell told `ENOENT` concludes the
file does not exist and reports "not found", so no script could ever run — while the kernel,
which gets it right, would have run it. `Machine::ExecError` now carries the errno.

**What is still out of reach**, and why:

* **Executing a compiled program other than the one the runner brings.** `b6sim` resolves `PATH`
  against the HOST filesystem, where every binary is an ELF file it cannot load, so only a BESM-6
  `a.out` copied in beside the fixture can be run. `run-sh-test.sh` copies `./echo`, and
  `sh_utf8` and `sh_subpipe` are what use it.
* **Globbing.** [`expand.c`](expand.c) reads directories, and `b6sim` maps that onto the host,
  where a directory descriptor refuses to be read. Since task C24 it goes through `opendir(3)`,
  which makes the failure **worse**: `open(2)` and `fstat(2)` on a host directory both succeed
  and only `read(2)` refuses, so `opendir()` returns a good `DIR`, the first `readdir()` returns
  `NULL`, and every directory reads as *empty* — indistinguishable from a real empty one. Before,
  the read failed and the pattern was left literal; now the pattern matches nothing. Neither is
  the machine's answer, and no expectation file can tell the second from a correct result.
* **Traps and interrupts.** `b6sim`'s `signal()` implements only `SIG_DFL` and `SIG_IGN`.

All three need the real kernel, which this shell runs on. `kernel/test/console` and
`kernel/test/session`, which typed at it and had it write files, are both deleted;
`kernel/test/multi` is what boots to a shell today. Since task C29 it types a `case` round a
`while` round an `if` round a pipeline at root's prompt — the one thing that has to be asserted
where a real environment sits under the stack — but it still neither globs, sends a signal nor
grows the arena into a fault, and those are the stages to add.

Two notes on the fixtures. They are commented with `#`, which they were not until this shell had
one — the `:` lines they used to carry are what the comment character replaced, and converting
them back was the first check that it works. And the runner clears the environment with `env -i`,
because `b6sim` hands the guest a whitelist of the host's variables, the shell reads all of them
into its name tree at startup, and `set` prints the tree.

A third: **`echo` is the only program a fixture may name**, and it has to be spelled `./echo` —
a bare `echo` finds the host's. So a fixture that wants to report something usually leaves it in
a variable and ends with `set` instead. `comment.sh` is written that way, and the one thing it
cannot show is a command substitution's *value* — `` `umask` `` prints where it stands rather
than being captured, which is `b6sim`'s pipe and not the shell.

## Known limits

* **Two v7 behaviours changed when the name tree was normalised to raw values** (task C11's third
  invariant, above). Both are corners, both are deliberate, and both are the same corner: v7
  stored a *marked* value and so remembered, after the fact, which of its characters had been
  quoted.
  * `read x` fed `a\ b` stored a marked space, and a later `$x` therefore expanded to **one**
    word. It stores a plain space here, and `$x` expands to two. `"$x"` is one word either way,
    which is what a script that cares should say.
  * `${x=a\ b}` assigned the marked text as well as expanding it, with the same consequence.
    [`macro.c`](macro.c) trims before the `assign()`.

  Leaving them as they were is not available: a half-encoded name tree makes the value push in
  `macro.c` wrong whichever way it is written — encode unconditionally and these two values are
  encoded twice, pass them through and a `0377` arriving from the environment is read as a mark.
* **A script may nest sixteen levels deep**, and the seventeenth is refused with `too deep`
  rather than run. That is the ceiling `deepchk()` enforces and the section above is the account
  of it; v7 had no limit and no way to survive passing one. Two entries that used to stand here
  — a pipeline of four stages inside a command substitution dying with `SIGNAL 4` (task C8), and
  a `case` arm holding a `while` holding a pipeline eating the shell (task C29) — were both this
  one thing, and both now have fixtures in [`test/`](test/) rather than paragraphs here.
* **`?` and `[...]` match one BYTE, not one character**, so `приве?` does not match `привет`.
  See the C11 section above; `test/utf8.sh` asserts it.
* **`wait()`'s status comes back in r12, a 15-bit index register**, so an exit code of 128 or more
  arrives truncated ([`lib/libc/sys/wait.S`](../../lib/libc/sys/wait.S)). That is the kernel ABI,
  not something `cmd/sh` can repair, and it bites twice: `await()` builds `$?` for a
  signal-killed child as `0200|sig`, which is itself in the truncated range.
* **Only the `TIOC*` ioctls `ttioccomm()` implements do anything.** `dev/sc.c`'s `scioctl()` hands
  everything to it and answers `ENOTTY` to the rest; a Consul typewriter has no line speed and no
  modem control for the others to reach.
* **It runs.** [`root.manifest`](../../root.manifest) carries this shell as `/bin/sh` and
  `cmd/init` as `/etc/init`, and since task 25b the boot reaches this shell's root prompt on the
  console. `kernel/test/console` holds a conversation with it — erase, kill, a line longer than a
  clist block, `>/dev/tty`, `pwd`, `ls /bin`, `^D` — and `kernel/test/session` has it write files
  and `sync`, after which the host fscks what reached the disk. Getting here needed the kernel
  stack, which was not big enough to run this program: the geometry is in
  [`../../kernel/README.md`](../../kernel/README.md) and the account at `UBASE` in
  [`../../include/sys/param.h`](../../include/sys/param.h).

`sh.1.umm` is the v7 manual page, corrected in place on the [`lib/libc/man/`](../../lib/libc/man/)
precedent: one addition, a `Comments.` paragraph marked `Note:` as the deviation from v7 that it
is. It goes on the image as `/usr/man/man1/sh.1`, where manview(1)
([`../README.md`](../README.md) C25a) formats it — and it is legible as it stands either way.
