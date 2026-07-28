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
`rmdir/`, `touch/`) today. That is the only marker; [../CMakeLists.txt](../CMakeLists.txt) names
its subdirectories one by one.

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

Ten things that are true of **every** port. They are collected here so that no task in
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
| `ed.c` | **ten** — `p >= &linebuf[LBSIZE-2]`, `sp >= &genbuf[LBSIZE]` ×3, `ep >= &expbuf[ESIZE]` ×3, … |
| `fgrep.c` | four — `p > &buf[512]` ×2, `p <= nlp`, `nlp < &buf[1024]` |
| `grep.c` | three — `ep >= &expbuf[ESIZE]`, `sp > cstart`, `lp >= curlp` |
| `sed/sed1.c` | three, all `sp >= &genbuf[LBSIZE]` |
| `pr.c` | two, both `>= &buffer[BUFS]` |
| `date.c` | one — `sp < ep` |

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

Sources carrying the most `long`: `ps.c` (17), `od.c` (10), `cmp.c` (7), `find.c` (7), `du.c` (5),
`strip.c` (5), `nm.c` (4), `grep.c` (4).

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
printed; `df`, `du`, `quot` and `dd` will do the same, and their manual pages must say so.

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
| `cat` | 83 | 2,989 | 165 | 1,544 | **4,781** |
| `chgrp` | 85 | 3,211 | 344 | 1,236 | **4,876** |
| `rmdir` | 81 | 2,868 | 208 | 1,033 | **4,190** |
| `mkdir` | 79 | 2,746 | 180 | 1,033 | **4,038** |
| `touch` | 77 | 2,560 | 160 | 1,032 | **3,829** |
| `init` | 27 | 820 | 37 | 323 | **1,207** |

Most of `cat` is libc's stdio, and `sh` is the largest v7 command bar the ones tasks C9 and C10
name — so nothing before task C6 is in danger of the first ceiling. `fsck`, `sort`, `awk` and
`make` are the four to measure early rather than late.

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
   which v7's had not — see [sh/README.md](sh/README.md).)
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
`getxfile()` takes the ISUID branch only `if (u.u_uid != 0)` — while every shell here is root's,
`init` execing `/bin/sh` with no `getty` and no `login` behind it. So a setuid program typed at
the console prompt exercises no setuid code at all. [../lib/test/suidt.c](../lib/test/suidt.c) is
the answer: the only thing on the image that makes a non-root process, dropping to uid 7 and
execing `/bin/mkdir`. **Anything else that wants the bit follows that pattern**, and
[mkdir/README.md](mkdir/README.md) is the account.

**Most programs do not want it.** `chmod(2)` is gated on `owner()`, which admits the file's owner,
and `chown(2)` on `suser()` — and that second gate is the rule that stops a user giving a file
away, so a setuid `chown` would defeat the thing it exists for. Ask what call actually needs
privilege before reaching for `04755`.

### 9. Which world a test runs in

Two harnesses, and choosing wrong wastes the effort:

* **`b6sim`** runs one BESM-6 `a.out` and services its syscalls on the *host*. Good for filters —
  stdin, stdout, files by relative path — and [sh/test/](sh/test/) is the pattern to copy
  (`run-sh-test.sh`, one `.expected` per case). It **cannot** read a directory (a host directory
  descriptor refuses `read`, which is why `ls` has no `b6sim` test) and it cannot exec `/bin/…`,
  because no such path exists on the build machine.
* **SIMH**, under the booted kernel. `kernel/test/console` is the model for a typed dialogue,
  `kernel/test/libtest` for running a program off `/usr/test` and diffing it against a
  `.expected`, `kernel/test/session` for anything that must *write* and then be fscked on the
  host afterwards, and `kernel/test/files` for anything that changes a tree or an inode.

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
