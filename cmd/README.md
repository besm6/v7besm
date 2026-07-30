# Porting the v7 userland to the BESM-6

`cmd/` holds two kinds of subdirectory, and telling them apart is the first thing to know:

* **host tools** — `cc`, `as`, `ld`, `cpp`, `disasm`, the binutils, `sim` and `fsutil` — compiled
  by the build machine's own compiler and run there. They have their own `README.md`s and their
  own chapters in [../doc/](../doc/); nothing below is about them.
* **native BESM-6 programs** — the v7 commands that go on the disk image, compiled by the `b6*`
  toolchain and staged into `build/rootfs/`.

**This file is the manual for the second kind**: what is in the directory, what a v7 source
assumes that is not true on this machine, and what it takes to get a program onto the image and
under test. [TODO.md](TODO.md) beside it is the work plan — which programs are worth porting, in
what order, and what each will cost — and it does not repeat any of this.

**Read these two before anything here:** [sh/README.md](sh/README.md) is the porting manual
written from the largest port there has been — what a v7 source assumes that is not true here —
and [ls/README.md](ls/README.md) is its shorter second half. Everything below is written on top
of both.

## The sources are already here

**Every program named by [TODO.md](TODO.md)'s tasks C2–C8 and C10 is in this directory**, one
directory per program, in the shape the port will build it from — the source, whatever auxiliary
files come with it, and the manual page. Every one still listed there is a **verbatim upstream
copy**: unbuilt, unmodified, not a line of C11 work done, so the first diff on any of them is the
porting diff. A task starts by writing a `CMakeLists.txt`, not by fetching anything.

They came from v7's own tree, [tmp/v7x86-0.8a/usr/src/cmd/](tmp/v7x86-0.8a/usr/src/cmd/) — 119
single-file programs and 29 directories of larger ones. That tree is **not in the repository**:
`tmp/` is git-ignored, and it is an unpacked reference copy.

**A directory is part of the build when it holds a `CMakeLists.txt`**, and only the ported ones
do — the ten of task C1 (`chgrp/`, `chmod/`, `chown/`, `cp/`, `ln/`, `mkdir/`, `mv/`, `rm/`,
`rmdir/`, `touch/`), the three of C2a (`date/`, `kill/`, `sleep/`), the five of C2b
(`basename/`, `test/`, `time/`, `tty/`, `yes/`), the one of C3 (`ed/`), the three of C4a
(`df/`, `du/`, `quot/`) and the two of kernel task 29b (`getty/`, `login/`) today. That is the only marker;
[../CMakeLists.txt](../CMakeLists.txt) names its subdirectories one by one.

Four things about the copies:

* **The v7 `makefile` came along** for each multi-file program (`sed`, `tar`, `make`, `m4`, `awk`,
  `dc`) as the record of its source list and flags. It is a PDP-11 recipe, kept for reading and
  replaced by a `CMakeLists.txt`; nothing runs it.
* **Ten programs have no manual page of their own.** Eight are documented inside another
  program's page, which was *not* duplicated — [rm/rm.1](rm/rm.1) covers `rmdir`,
  [chown/chown.1](chown/chown.1) covers `chgrp`, [mount/mount.1m](mount/mount.1m) covers `umount`,
  [grep/grep.1](grep/grep.1) covers `fgrep` and `egrep`, [diff/diff.1](diff/diff.1) covers
  `diffh`, [at/at.1](at/at.1) covers `atrun`, and [sa/sa.1m](sa/sa.1m) covers `accton`. **`yes`
  and `dmesg` have no page anywhere in v7** and need one written from scratch.
* **The file-format pages are in [../include/man/](../include/man/)**, not here: `acct.5`,
  `dir.5`, `environ.5`, `filsys.5`, `group.5`, `mtab.5`, `passwd.5`, `ttys.5`, `types.5`,
  `utmp.5` document what the headers in `../include/` declare rather than what a program does.
  **Five of them are nothing but `.so /usr/include/…` of a header** and there is no
  `/usr/include` here — the same problem [../lib/libc/man/](../lib/libc/man/) solved by writing
  the structure out, and the structures differ on this machine (`DIRSIZ` 18, one-word `off_t`
  and `time_t`). v7's other eight `man5` pages were left behind: `a.out.5` and `ar.5` describe a
  PDP-11 format ([../doc/Linker_Manual.md](../doc/Linker_Manual.md) and
  [../doc/Archiver_Manual.md](../doc/Archiver_Manual.md) are this machine's), and the rest belong
  to programs [TODO.md](TODO.md)'s exclusion table drops.
* **Two data files** came with their programs, because neither program does anything without one:
  [units/units](units/units) (the 484-line conversion table) and [cron/crontab](cron/crontab).
  `calendar` has none — what this reference tree holds under that name is an x86 binary, not the
  database, so `/usr/lib/calendar` must be written or found elsewhere.

The `.y`, `.l` and header-ish files (`awk/awk.def`, `make/defs`, and the four `.y` grammars) carry
**no v7 copyright banner**, unlike the 117 `.c` sources that do. The top-level `COPYRIGHT` covers
them; do not add one.

The programs of task C9 are **not** here and never will be: they are this repo's own C sources
built a second time, not ports. See [TODO.md](TODO.md).

---

## The porting recipe

Eleven things that are true of **every** port. They are collected here so that no task in
[TODO.md](TODO.md) has to say them again; a task names only what is unusual about *it*.

### 1. The C11 pass, which is mechanical

`b6parse` is strict C11. No implicit `int`, no K&R parameter lists, no untyped `register i;`, no
`char *malloc();` re-declarations of a library function. Prototypes and explicit return types
everywhere, `static` on file-scope objects and functions. Every v7 source needs this before it
compiles, and it is the least interesting part of any of these ports —
[init/README.md](init/README.md) is the small worked example, [sh/README.md](sh/README.md) the
large one.

Watch for **names C11 has reserved that v7 used freely**: `chmod.c` defined `abs()`, which this
libc really provides ([../lib/libc/gen/abs.c](../lib/libc/gen/abs.c)), and `chown.c` defined
`isnumber()`, which is in `<ctype.h>`'s reserved `is`-plus-lower-case namespace. Neither fails to
link — the program's own definition satisfies its own call — so the collision waits silently for
whatever wants the real one. Rename on sight; `mkdir.c`'s `readdir`→`listdir` and
`mkdir`→`makedir` are the precedent.

### 2. An `int` is not a `char *`, and `<` does not order two of them

This is the part that is *not* mechanical, and it is where the time goes. A `char *` here is a
**fat pointer** — byte offset in bits 47–45, word address in bits 15–1 — and the offset
**decrements** as the pointer advances. So:

> **A relational operator between two `char *` values gives the wrong answer.** There is no
> relational helper; `<` compiles to an integer comparison of the whole word, the offset field
> dominates the address field, and the ordering comes out scrambled and inverted within a word.
> `p < end` on a buffer cursor is silently, unpredictably wrong. **Subtraction is fine**
> (`b$pdiff` decodes both operands); it is ordering that has no helper.

[ls/README.md](ls/README.md) states it, [../lib/libtermcap/README.md](../lib/libtermcap/README.md)
found four instances, [../lib/libcurses/README.md](../lib/libcurses/README.md) eleven — and one of
those made `getpass()` return the empty string every time, for months, in a library everything
links. **Grep for it first, in every source, before reading anything else.** A count of the
candidates, as they stand today:

| source | `char *` comparisons |
|---|---|
| `sort.c` | **fifteen** — all in `cmp()` and `newfile()`: `pa<la`, `ipa>pa`, `cp>=ce`, `cp < tspace+ntext`, … |
| ~~`ed.c`~~ | ~~**ten**~~ — **twenty**, and this table undercounted by half; the two `-x` took with it left nineteen to rewrite. **Found and fixed, task C3**: they are index counts and `int` differences now, and [ed/README.md](ed/README.md) lists them. Every one bounds a buffer the regex engine or the substitute path writes into |
| `fgrep.c` | four — `p > &buf[512]` ×2, `p <= nlp`, `nlp < &buf[1024]` |
| `grep.c` | three — `ep >= &expbuf[ESIZE]`, `sp > cstart`, `lp >= curlp` |
| `sed/sed1.c` | three, all `sp >= &genbuf[LBSIZE]` |
| `pr.c` | two, both `>= &buffer[BUFS]` |
| ~~`date.c`~~ | ~~one — `sp < ep`~~ — **found and fixed**, task C2a: it bounded the in-place reversal of `argv[1]`, and is an index pair now |
| ~~`basename.c`~~ | ~~**two**, not the one this table used to claim — `p1>p2 && p3>argv[2]`~~ — **found and fixed**, task C2b: both are in the *same expression*, the backwards suffix compare, and both are index counts now |

And the counterexample, because it is what makes the hazard hard to see: **`sort.c`'s record
arena is clean.** `lp`, `hp`, `i`, `j`, `k` in `qsort()` are `char **` — thin word pointers that
compare correctly — while `pa`, `pb`, `la`, `lb` one function away are `char *` and do not. Two
kinds of pointer in one program, identical syntax, opposite behaviour. The same is true of
`ar.c`'s `int **mp`, `fgrep.c`'s `struct words *smax` and every `p < &tab[N]` in `ls`, all of
which were left alone. **It is the pointed-to type that decides, not the shape of the loop.**

The other three hazards `sh/README.md` names — a flag packed into bit 0 of a pointer, a bit mask
used to round to a word when `BYTESPERWORD` is 6, and a cast to a pointer that *floors* rather
than rounds — come round again in anything that manages its own arena. `sort`, `dd` and `find`
all call `sbrk` and are the places to expect them.

### 3. A `long` is one word, and `%D` is not a conversion

`long` is `int` is one 41-bit word. Two consequences, and the second is nastier than it looks:

* `%ld` / `%7ld` is harmless — `l`, `h`, `L`, `j`, `z` and `t` are parsed and ignored — but it
  means nothing, and should be written `%d`.
* **`%D` prints the two characters `%D`.** [../lib/libc/stdio/doprnt.c](../lib/libc/stdio/doprnt.c)
  does not know that PDP-11 conversion, and an unknown conversion is echoed verbatim **and
  consumes no argument** — so it desynchronises every later conversion in the same format string.
  v7 wrote `%D` freely. `ls` had two.

Sources carrying the most `long`: `ps.c` (17), `od.c` (10), `cmp.c` (7), `find.c` (7),
~~`du.c` (5)~~ (**done, task C4a** — all five were `int`), `strip.c` (5), `nm.c` (4),
`grep.c` (4).

The other direction is worth a glance too: **plain `char` is unsigned here**
([../doc/Besm6_Data_Representation.md](../doc/Besm6_Data_Representation.md)), so the
`(unsigned char)` a `<ctype.h>` call wants is habit rather than necessity — and a
`signed`→`unsigned` conversion is a bare reinterpretation of the word, not C11's modulo
adjustment, because an `int` occupies bits 41–1 and an `unsigned` all 48. Prefer `int` wherever
v7 wrote `unsigned` for no reason.

### 4. A filesystem block is 3072 bytes, and there is no `BSHIFT`

`BSIZE` is 3072 — not a power of two, and [../include/sys/param.h](../include/sys/param.h) says
outright that there cannot be a `BSHIFT`/`BMASK` to go with it. So every `>>9`, `<<9` and `&0777`
that means *a filesystem block* is wrong and becomes a divide or a remainder. (`BWSHIFT` 9 and
`BWMASK` 0777 exist, and are **word** offsets within a block, which is a different quantity.)
`ls -s` and its `total` line already count 3072-byte blocks and print a sixth of what a PDP-11
printed; **`df`, `du` and `quot` do too since task C4a**, each with a `BLOCKS ARE 3072 BYTES`
section in its manual page, and `dd` will when it lands. Two of the three met `BSHIFT` as a
*compile error*, which is the best way to meet it.

A program's *own* file blocking is its own business: `ed`'s temp file is `512`-byte blocks by its
own choice and stays that way.

### 5. `DIRSIZ` is 18

`struct direct` is four words — a full-word `d_ino` and three words of name.  So `%.14s` is `%s`,
`char name[15]` is `char name[DIRSIZ + 1]`, and a name read out of a directory **is not
NUL-terminated** unless the port terminates it (a v7 bug `ls` had to fix rather than carry).
Anything that walks directories — `rm -r`, `du`, `find`, `ncheck`, `dcheck`, `mv` — inherits this.

### 6. Three ceilings, of which only two are checked

* **28,672 words** of `const + text + data + bss` — 32 pages less the four the stack takes.
* **Word 32,767** — no relocatable symbol above the reach of a 15-bit pointer.
* **4,096 words of stack**, at `070000`. **Nothing checks this one.** A single big automatic
  array blows it in silence; [cpp/TODO.md](cpp/TODO.md)'s blocker L2 is the worked example
  (`acttxt[BUFSIZ] + exptxt[4*BUFSIZ]` ≈ 6,827 words in one frame).

`b6_prog()` registers `check-size.sh` for the first two automatically, as ctest
`rootfs_<name>_size`. For scale, what is on the image today, in words of the 28,672:

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `sh` | 121 | 7,039 | 637 | 131 | **7,928** |
| `ls` | 110 | 4,632 | 298 | 2,638 | **7,678** |
| `login` | 106 | 5,063 | 408 | 1,321 | **6,898** |
| `ed` | 95 | 4,027 | 50 | 633 | **4,805** |
| `quot` | 102 | 5,208 | 227 | 4,375 | **9,912** |
| `du` | 83 | 3,041 | 198 | 3,132 | **6,454** |
| `df` | 80 | 2,633 | 177 | 2,570 | **5,460** |
| `cat` | 83 | 2,989 | 165 | 1,544 | **4,781** |
| `chgrp` | 85 | 3,211 | 344 | 1,236 | **4,876** |
| `time` | 85 | 3,088 | 340 | 1,041 | **4,554** |
| `date` | 99 | 3,348 | 265 | 1,041 | **4,753** |
| `kill` | 78 | 2,619 | 311 | 1,032 | **4,040** |
| `rmdir` | 81 | 2,868 | 208 | 1,033 | **4,190** |
| `mkdir` | 79 | 2,746 | 180 | 1,033 | **4,038** |
| `tty` | 80 | 2,644 | 159 | 1,032 | **3,915** |
| `sleep` | 78 | 2,531 | 155 | 1,040 | **3,804** |
| `touch` | 77 | 2,560 | 160 | 1,032 | **3,829** |
| `yes` | 76 | 2,452 | 152 | 1,032 | **3,712** |
| `basename` | 35 | 1,162 | 147 | 1,030 | **2,374** |
| `init` | 27 | 820 | 37 | 323 | **1,207** |
| `test` | 22 | 886 | 49 | 7 | **964** |
| `getty` | 34 | 361 | 28 | 11 | **434** |

Most of `cat` is libc's stdio, and `sh` is the largest v7 command bar the ones tasks C9 and C10
name — so nothing before task C6 is in danger of the first ceiling. `fsck`, `sort`, `awk` and
`make` are the four to measure early rather than late.

**The bottom three rows say what stdio costs.** Every program above `basename` links `printf`
and a `FILE` buffer, which is the ~1,030 words of bss and most of the text they have in common;
`test` and `getty` link neither — `test`'s whole output is four `write(2)` calls and `getty`'s is
one `write(2)` per character — so `getty` is the smallest program on the image, an eleventh the
size of `cat`. That is the difference `lib/libc/README.md` measures for `hello`, seen from the
other end.

**And `ed` is the third angle on it**, being the one *large* program that pays nothing either:
it has no format string at all (`putd()` over `write(2)`), so its 4,027 words of text — 1,038
more than `cat`'s — come with 911 words *less* bss, and the two totals land twenty-four words
apart despite `ed` being five times the source. When measuring a candidate against the ceiling,
ask what it prints with before extrapolating from a line count.

**The stack is where a fixed buffer goes wrong, and every port so far has had to bound one.**
`mkdir`, `rmdir`, `ln`, `cp` and `mv` each build a path in a fixed automatic that v7 filled with
an unbounded `sprintf`/`strcpy`/`strcat` from `argv`. Add the length test. `chmod` is the only
exception to date, and only because it has no buffer at all — it walks `argv[1]` in place.

### 7. How a program gets onto the image — five steps

1. `cmd/<x>/CMakeLists.txt`: one `b6_prog(<x> DEST bin/<x> SOURCES <x>.c)` call
   ([../scripts/BesmCross.cmake](../scripts/BesmCross.cmake)). Add `PURE` only for something that
   will run in several processes at once; it costs a page-aligned data segment.
2. `add_subdirectory(cmd/<x>)` in the top-level [../CMakeLists.txt](../CMakeLists.txt), **inside
   the `libruntime.a` guard and after `add_subdirectory(lib)`** — the program links against the
   libc built there.
3. A stanza in [../root.manifest](../root.manifest): `mode`, `file /bin/<x>`, `source
   ../../rootfs/bin/<x>`. Paths there resolve against `b6fsutil`'s working directory
   (`build/kernel/test`), not against the manifest.
4. A line in [../etc/rc](../etc/rc) if the boot script wants it — remembering that `/etc/rc` runs
   with **no terminal**, so anything meant to be seen redirects to `/dev/console` for itself.
   (The other rule that used to stand here is gone: this shell takes `#` as a comment character,
   which v7's had not — see [sh/README.md](sh/README.md).) The line's *assertion* has exactly one
   home, `kernel/test/console`: `/etc/rc` does not run until the first shell has exited
   (`init/init.c`: `shutdown, single, runcom, multiple`), and console is the only test that types
   the `^D` that gets there. `kernel/test/boot` quits on the first prompt, with the disk still
   attached read-only.
5. The test, per §9.

Two lists have to grow with the program, and nothing catches them but a failing test:
`ROOTFS_FILES` in [../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt), so the image
rebuilds when the program does, and the hard-coded `ls /bin` expectations in
`kernel/test/console.ini` and `kernel/test/session.expected`.

The disk is one EC-5052: **2000 blocks, 6,144,000 bytes**. Nothing planned comes close to
filling it.

### 8. Setuid works, and it is asserted

`mkdir` calls `mknod(d, 040777, 0)` and `rmdir` calls `unlink` on `.` and `..`; both are
super-user operations, and both programs are **setuid root** in v7, as is `mv` for the half of
its job that re-parents a directory. The kernel honours `ISUID` in `getxfile()`
([../kernel/sys1.c](../kernel/sys1.c), the SUID/SGID block), and `b6fsutil` carries the bit
through: a manifest `mode 04755` reaches the inode as `IFREG | (mode & 07777)`
(`cmd/fsutil/command.cpp`), and 07777 includes 04000. So the manifest stanza is the whole of the
work, and no source change was needed anywhere to make it work.

**Which is exactly why it had to be asserted rather than assumed**, and the trap is that
`getxfile()` takes the ISUID branch only `if (u.u_uid != 0)`. That used to be unreachable from a
prompt: **every shell on this machine was root's**, `init` execing `/bin/sh` with no `getty` and
no `login` behind it, so a setuid program typed at the console exercised no setuid code at all.
[../lib/test/suidt.c](../lib/test/suidt.c) is the answer that was written for it, and is still
the assertion: the program drops to uid 7 itself and execs `/bin/mkdir`, so the transition is
made deliberately rather than depended on.

**Kernel task 29b changed the premise but not the practice.** [login/](login/) is on the image
now, so a shell can belong to somebody other than root — `kernel/test/login` logs `guest` in and
the prompt comes back `$ ` rather than `# ` — and a setuid program run from *that* shell really
does take the ISUID branch. It is still not the way to test one: a bit asserted through a login
dialogue is asserted through `getty`, `login`, `crypt` and `/etc/passwd` as well, and any of them
failing looks the same. **Anything that wants the bit follows `suidt`'s pattern**, and
[mkdir/README.md](mkdir/README.md) is the account.

**Most programs do not want it.** `chmod(2)` is gated on `owner()`, which admits the file's owner,
and `chown(2)` on `suser()` — and that second gate is the rule that stops a user giving a file
away, so a setuid `chown` would defeat the thing it exists for. Ask what call actually needs
privilege before reaching for `04755`.

### 9. Which world a test runs in

Two harnesses, and choosing wrong wastes the effort:

* **`b6sim`** runs one BESM-6 `a.out` and services its syscalls on the *host*. Good for filters —
  stdin, stdout, files by relative path. **`b6_progtest(<prog> <case>)` is the harness** (task
  C2a, [../scripts/BesmCross.cmake](../scripts/BesmCross.cmake) and
  [../scripts/run-prog-test.sh](../scripts/run-prog-test.sh)): one call per case, files
  `<case>.args` / `<case>.expected` / optional `<case>.status` in `cmd/<x>/test/`, ctest name
  `cmd_<x>_<case>` under label **`cmd`**. It runs the program **as staged for the image**, so the
  bytes under test are the image's. [sh/test/](sh/test/) is the other shape — a whole shell
  *script* per case — and is what to copy when the thing under test is the shell.
  b6sim **cannot** read a directory (a host directory descriptor refuses `read`, which is why
  `ls` and, since C4a, `du` have no `b6sim` test) and it cannot exec `/bin/…`, because no such
  path exists on the build machine.
  **But it can be handed a whole filesystem**, which task C4a found and which nothing had
  needed: a program that reads a *device* reads a file, and a flat `b6fsutil` image is one, so
  [df/test/](df/test/) builds a small one at build time and `df` and `quot` walk its real free
  list and real i-list with no boot. C4c and C4d inherit it.
  A fourth limit came with that, and it is about the *host* rather than the simulator:
  **`getpwent(3)` opens the literal `/etc/passwd`**, so under `b6sim` a program that maps a uid
  to a name reads the build machine's password file and no case may assert what comes back.
  There is no uid to steer around it — see [df/README.md](df/README.md). Two more limits C2a ran into: **`stime(2)` is a no-op that reports success**, so a
  program that sets global state cannot be asserted there at all; and **`kill(2)` is the build
  machine's own**, so no case may name a pid. And two C2b ran into: a program that **does not
  terminate** cannot be a case at all, however it is invoked, which is why `yes` has no `test/`
  directory; and **`argv[0]` is the staged path**, so a program that dispatches on the name it
  was called by — `test` and `[` — can only be tested through one of its names here.
  **A third is gone: there is stdin now.** C2b recorded that a case could not feed a filter and
  named `<case>.in` as the obvious fix; task C3 made it, because `ed` reads its whole command
  language on standard input and nothing else about it could have been tested cheaply. The file
  is redirected in when it exists and `/dev/null` when it does not, and C5's filters inherit it.
  Two rules come with it, both consequences of `ed`'s `error()`, and
  [ed/README.md](ed/README.md) is the account: the redirection is from a **file** and not a
  pipe, because `error()` does an `lseek(0, 0, SEEK_END)` to discard the rest of a script and
  only a seekable descriptor can answer that — and therefore **one error per case**, since
  everything after the first diagnostic *is* that discarded remainder. `cmd_ed_badcmd` is the
  case that asserts the discard rather than merely living with it.
* **SIMH**, under the booted kernel. `kernel/test/console` is the model for a typed dialogue,
  `kernel/test/libtest` for running a program off `/usr/test` and diffing it against a
  `.expected`, `kernel/test/session` for anything that must *write* and then be fscked on the
  host afterwards, `kernel/test/files` for anything that changes a tree or an inode, and
  `kernel/test/utils` (tasks C2a and C2b) for anything that touches the clock, a signal,
  another process or a pipe, and `kernel/test/login` (kernel task 29b) for anything that needs a
  terminal's *modes* or a process that is not root's, and `kernel/test/edit` (task C3) for
  anything that must *author* a file rather than merely write to one — it is also the only test
  that runs a **here-document** under the booted kernel — and `kernel/test/fsinfo` (task C4a)
  for anything that reads a **device**, or that reports about the filesystem as a whole; it is
  the only one that captures the **console transcript** as part of its oracle, and the only one
  whose oracles are *recomputed* rather than checked in. Each has its own copy of the image at
  its own volume number; **3087 is the highest used** (`edit` took 3086 and `fsinfo` 3087). All but `login` graft a script onto that copy
  with `b6fsutil -a`, and `login` is the exception because what it tests is the thing that reads
  what is typed: it types every character it needs.

Most of task C5 lands in the first; C1, C3, C4 and C6 land in the second. Where a program can run
in both, **do both** — the first time the libc suite was run in both worlds it found two bugs
nothing else had exercised.

### 10. The manual page comes with the source

Each v7 command ships its `.1`, and it is **already in the program's directory** — see "The
sources are already here" above, including the ten programs that have no page of their own.
Follow the [../lib/libc/man/](../lib/libc/man/) precedent: **correct it in place** — ANSI SYNOPSIS,
every wrong claim fixed where it stands and marked `Note:`, block counts in 3072-byte blocks,
`DIRSIZ` 18 where it shows. Nothing installs any of them; they are read with `nroff -man`. Rewrite
a page rather than correct it only when the DESCRIPTION itself stopped being true, which has
happened once: `touch` — see "What task C1 taught" below. A `README.md` is worth writing only when the port *taught* something
structural — a new privilege transition, a new hazard — which is the standard `sh`, `ls`, `mkdir`
and `mv` met and `cp`, `ln` and `rm` did not.

### 11. Everything carries eight bits, and a `char` is unsigned

The console path is byte-transparent in both directions and this machine's text is UTF-8
([../kernel/dev/sc.c](../kernel/dev/sc.c)); so are the clists, the filesystem and — since task
C11 — `/bin/sh`, which used to mark a quoted character with bit `0200` and clear that bit from
every word on its way to `argv`. A Cyrillic string now survives being typed, being written, being
stored, being globbed and being passed as an **argument**. Nothing has to be routed around the
shell any more.

What that leaves is a rule about **your** program, and it is the one v7 sources break: `char` is
unsigned here, so a byte above `0177` is a value in `128..255` and not a negative number — but
v7 code masks with `0177`, tests `c > 0` to mean "not EOF", or indexes a 128-entry table with a
character. All three are silent on ASCII and wrong on the first Cyrillic byte. The shell's own
account of what that cost is [sh/README.md](sh/README.md)'s C11 section; the short form is that a
table indexed by a character must have **256** entries, and a mask that throws away the eighth
bit is almost always a bug.

Two things that follow, and are worth knowing before writing a test: the shell's pattern
language matches **bytes**, so `?` is one byte and not one letter; and the terminal driver
refuses a typed `0377` ([../kernel/dev/tty.c](../kernel/dev/tty.c)), which is the raw queue's
delimiter — a script read from disk may contain one, a console user cannot type one.

---

## What task C1 taught

The file-management set — `mkdir`, `rmdir`, `cp`, `ln`, `mv`, `rm`, `chmod`, `chown`, `chgrp`,
`touch` — is on the image, and [../kernel/test/files.sh](../kernel/test/files.sh) holds it there.
Four findings from it outlive the task, and every later port that touches a file's metadata will
meet them.

**A mode and an owner cannot be asserted in the guest.** `ls -l` prints them and prints a date
beside them, and `files.sh`'s rule is that only reproducible things reach the log. So
`run-files.sh` asks the *image* instead — `b6fsutil -v -v` piped through an `awk` that keeps the
type, the mode, the uid/gid and the path, sorted, diffed against `files.modes`. That is a
**closed** assertion over the whole of `/tmp`, so a mode nothing asked to change fails the run
too. It is the reusable half of the task.

**The guest clock advances about two seconds over a whole `files` run** — compare `root.img`'s
superblock `Last update` with `filesafter.img`'s. Seventy-odd programs therefore execute inside
two ticks of a one-second `mtime`, no two commands in a script are a tick apart, and `ls -t`
between two files the script made is a coin toss (`ls.c`'s `compar()` returns 0 on equal times,
into a `qsort` that is not stable). **The only way to see a time change is a gap you put there
yourself**: `b6fsutil -T <far-off> -a` grafts a fixture dated a generation away, and touching it
moves it by decades rather than by ticks.

**`touch` is the one deliberate divergence so far.** v7's calls no `utime(2)` — it opens the file
`O_RDWR`, reads its first byte and writes it back. This port calls `utime(2)`;
[touch/touch.c](touch/touch.c)'s header and a rewritten [touch/touch.1](touch/touch.1) are the
account, and §10 is the rule it followed. A divergence is allowed, but it is written down twice:
in the source and in the page.

**Upstream bugs are fixed, not carried, and the fix says which it is.** `mv` had a `strcat` into
an uninitialized buffer in the middle of the four-call directory re-parent, with every signal
ignored; `rm` had a `%.14s`; `touch` had a `main()` that fell off the end. But `chown`'s unchecked
`stat()` is *defensive* rather than a visible bug — `stat` and `chown` resolve through the same
`namei()`, so the stale field could never reach a successful call — and its source header says so
in those words. Claiming more than the fix does is worse than carrying the bug.

---

## What task C2 taught

C2a put `date`, `kill` and `sleep` on the image and C2b `basename`, `test`, `time`, `tty` and
`yes`; [kernel/test/utils.sh](../kernel/test/utils.sh) holds all eight. C2c then spent one of
them: [../etc/rc](../etc/rc) prints the date now as well as the motd, which is the whole of what
the boot script was waiting for from this task. Five findings outlive it, and three of them are
about this machine rather than about any program.

**An exit status above 127 does not survive `wait(2)`.** A status is `(code << 8)` and it comes
back through r12, a fifteen-bit index register
([../lib/libc/sys/wait.S](../lib/libc/sys/wait.S) states it), so `test`'s `exit(255)` reaches
the shell's `$?` as **127**. It was left at 255 rather than lowered: the truncation reaches
every program that exits above 127, and hiding a general limitation behind one local divergence
is worse than recording it. [test/README.md](test/README.md) is the account, and the same
finding is what makes the two harnesses disagree — under `b6sim` the *host* does the waiting
and really does report 255.

**A magic number can be spread across two expressions that share no variable.** `time` builds
the PDP-11's 60 Hz clock into a multiplier and into the leading entry of a radix table, and
nothing in `printt` suggests that entry is a clock rate. Grepping for `HZ` finds neither.
[time/README.md](time/README.md) is the account; `od`, `sort` and `dd` are flagged in
[TODO.md](TODO.md) for the same kind of trouble.

**A hard link is invisible to the whole build.** `/bin/[` is `/bin/test` — the first `link`
stanza [../root.manifest](../root.manifest) has ever carried, and without it the `argv[0]`
branch of `test.c` is unreachable code. Nothing stages it, nothing sizes it, nothing depends on
it; only the finished image can say it is there, which is why `rootimg_link` exists beside
`rootimg_setuid`. Same argument, same shape — see [test/README.md](test/README.md).

**Some programs cannot be a `b6sim` case at all**, and the reasons are in §9: no `argv[0]`
control, and no way to bound a program that does not terminate. (There were three; **task C3
lifted the first**, `<case>.in`, and §9 records what came with it.) `yes` is the last of them,
and what tests it is a pipeline — **the first this image has ever run**, since
nothing in `kernel/test/` or [sh/test/](sh/test/) had used `|`. It worked first time, which is
worth writing down precisely because it might not have.

**Anything `/etc/rc` prints from the clock is a literal to the minute.** There is no
clock-calendar here, so `main.c`'s `iinit()` seeds `time` from the root superblock's `s_time` and
`b6fsutil` stamps that with `-T ${ROOTTIME}` — the same number `kernel/test/fstest.c` asserts. So
the boot date is a build constant, `Sat Jul 25 08:23:0X GMT 2026`, and only the `X` moves: a
whole run advances the guest clock about two seconds. `console.ini` therefore asserts it by
*truncating the match* short of the seconds, where `run-utils.sh` masks them with `sed` — the
same projection, chosen differently because a SIMH `expect` has no host-side stream to filter.

---

## What task C3 taught

`ed` is on the image, and [../kernel/test/edit.sh](../kernel/test/edit.sh) holds it there. It is
the largest single-file port so far and the one the plan called the pivot, so most of what it
taught is in [ed/README.md](ed/README.md) rather than here. Four findings are general.

**A source can be wrong about itself, and this file was wrong about `ed` twice.** §2's table
said ten `char *` comparisons; there are twenty. [TODO.md](TODO.md)'s brief for the task said
the character classes were a `CCL` bitmap and warned that widening it would be the third thing
to bite; `ed` has no bitmap at all — that is `grep`'s and `sed`'s, and the warning has been
moved to task C5, where it is true. Both were written from a survey rather than from the code.
**The count is worth re-grepping at the start of a task, and a warning worth confirming before
budgeting for it**, because the second kind of error is the expensive one: it sends the work at
a problem that is not there and leaves the real one (`rhsbuf`'s escape bit) unnamed.

**A harness limitation can be a one-line fix that nobody had needed yet.** §9 had carried "there
is no stdin" as a fact about `b6sim` since task C2b. It was a fact about
[../scripts/run-prog-test.sh](../scripts/run-prog-test.sh), which never redirected one, and the
fix is four lines. `ed` forced it because a program whose entire command language arrives on
stdin has *no* cheap test otherwise — nineteen cases that now run in a tenth of a second each
would have been nineteen two-minute boots. **When a limit is stated of a harness rather than of
the machine, check which it actually is** before designing around it.

**`error()`-style recovery interacts with the harness, and the interaction is the assertion.**
`ed` discards the rest of its input after any diagnostic — `lseek(0, 0, SEEK_END)` — which makes
the input **have to be a file rather than a pipe**, allows **one error per case**, and means a
program cannot be driven by a here-document naming a file that does not exist, because the seek
takes the document with it. All three are properties of a program's error handling reaching out
into how it can be tested, and none was visible before there was something to test.

**A here-document runs under the booted kernel.** Nothing in `kernel/test/` or
[sh/test/](sh/test/) had used one there — the shell writes it to `/tmp/sh-<pid><serial>` and
unlinks it at once (`cmd/sh/io.c`) — and `kernel/test/edit` is built out of seven of them. It
worked first time, which is the same thing task C2b said about the first pipeline and worth
writing down for the same reason.

---

## What task C4a taught

`df`, `du` and `quot` are on the image and [../kernel/test/fsinfo.sh](../kernel/test/fsinfo.sh)
holds them there. They are the first programs here that *measure* the filesystem rather than
change it, and most of what the port taught is in [df/README.md](df/README.md), which is the
account of the raw-device path the whole of the rest of C4 will take. Four findings are general.

**Reading a device is not reading a file, and three of the four ways to get it wrong are
`EFAULT`.** `physio()` and `mdstrategy()` require the buffer to start at byte #0 of a word, the
count to be a whole number of `BSIZE`s and the buffer's *word address* to be a multiple of 512;
the fourth condition, a block-aligned seek, is **silent** — `physio()` truncates the offset and
reads the wrong block. C has no way to ask for the alignment, so the idiom is an over-sized
`bss` array stepped forward with `(int)ptr`, which is a word address here. **Obeying the rules
made both programs smaller**, which is worth expecting rather than dreading: `quot`'s 4,096-word
`itab[256]` disappeared, one block already being `INOPB` inodes.

**An expectation that is a property of the whole image should be recomputed, not checked in.**
`run-files.sh` diffs `b6fsutil -v -v` against a checked-in `files.modes`, which is right there —
those modes are what the test set. `df`'s and `quot`'s numbers are not: they move whenever a
program joins `/bin`, so a checked-in table would be a file somebody has to update for a reason
unconnected with the test. `run-fsinfo.sh` derives both from the finished image instead. **Ask
which kind of number an oracle is holding** before writing it down.

**A program that measures a filesystem cannot write its answer into one.** `quot >/tmp/x` counts
`/tmp/x` at the size it had before `quot`'s output reached it; `df >/tmp/x` samples the free list
before the block that output needs is allocated. Either report, on disk, disagrees with the disk
by its own size. Both write to `/dev/console` instead, and `run-fsinfo.sh` captures the
transcript — which no test here had done. Anything self-referential inherits this.

**Two silent parsing traps on the host side, and the first run hit both.** The console runs with
`XTABS`, so a tab a program emits reaches the transcript as spaces and a report cannot be split
on one. And `b6fsutil -v -v` is *two* reports — the superblock summary, then the tree — so an
`awk` that does not skip the preamble reads `Magic: 0123456701234` as an object with no third
field, invents a uid-0 entry, and comes out one file ahead of the guest. **The guest was right
and the oracle was wrong**, which is the failure mode a recomputing oracle has and a checked-in
one does not; budget for debugging the oracle, not just the program.
