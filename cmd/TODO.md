# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. It is the companion of
[../kernel/TODO.md](../kernel/TODO.md), which carries the kernel's own list, and it starts where
that one's task 24 left off: the machine boots, mounts a root filesystem, execs `/etc/init` and
gives a shell prompt, with exactly **six programs** on the image beside it — `sh`, `cat`, `echo`,
`ls`, `pwd` and `sync`. Those six were chosen because they *prove the prompt*, not because they
make the machine usable. [../etc/rc](../etc/rc) says so in its own words:

> What the v7 rc did next all wants a program this system has not got yet: fsck, mount, rm,
> date, cron, update, accton.

The sources are v7's own, under
[tmp/v7x86-0.8a/usr/src/cmd/](tmp/v7x86-0.8a/usr/src/cmd/) — 119 single-file programs and 29
directories of larger ones. That tree is **not in the repository**: `tmp/` is git-ignored, and it
is an unpacked reference copy. This file says which of its programs are worth porting, in what
order, and what each will cost.

**Read these two first, and do not expect the tasks below to repeat them:**
[sh/README.md](sh/README.md) is the porting manual — what a v7 source assumes that is not true
here — and [ls/README.md](ls/README.md) is the shorter second half of it. Everything below is
written on top of both.

**Task numbers carry a `C`** — `C1`, `C2a`, … — because `kernel/TODO.md`'s 1–34 are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it
compiles.

## The sources are already here

**Every program named by C1–C8 and C10 is in this directory**, one directory per program, in the
shape the port will build it from — the source, whatever auxiliary files come with it, and the
manual page. They are **verbatim upstream copies**: unbuilt, unmodified, not a line of C11 work
done, so the first diff on any of them is the porting diff. A task starts by writing a
`CMakeLists.txt`, not by fetching anything.

**A directory is part of the build when it holds a `CMakeLists.txt`**, and none of these does.
That is the only marker; `../CMakeLists.txt` names its subdirectories one by one and none of these
is among them.

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
  to programs the exclusion table drops.
* **Two data files** came with their programs, because neither program does anything without one:
  [units/units](units/units) (the 484-line conversion table) and [cron/crontab](cron/crontab).
  `calendar` has none — what this reference tree holds under that name is an x86 binary, not the
  database, so `/usr/lib/calendar` must be written or found elsewhere.

The `.y`, `.l` and header-ish files (`awk/awk.def`, `make/defs`, and the four `.y` grammars) carry
**no v7 copyright banner**, unlike the 117 `.c` sources that do. The top-level `COPYRIGHT` covers
them; do not add one.

C9's programs are **not** here and never will be: they are this repo's own C sources built a
second time, not ports. See C9.

| | task | what it buys | size |
|---|---|---|---|
| C1 | the file-management set — `mkdir` `rmdir` `rm` `ln` `mv` `cp` `chmod` `chown` `chgrp` `touch` | a filesystem you can change | small ×10 |
| C2 | the small utilities `sh` and `/etc/rc` want — `date` `sleep` `kill` `test` `basename` `tty` `time` `yes` | shell scripts that do something | small ×8 |
| C3 | **`ed`** | authoring text *on* the machine | large, and the pivot |
| C4 | filesystem maintenance — `df` `du` `dd` `mkfs` `fsck` `icheck` `dcheck` `ncheck` `clri` `quot` `mount` `umount` | a system that maintains itself | large |
| C5 | the text filters — `wc` `cmp` `sum` `tee` `split` `rev` `tr` `uniq` `comm` `tail` `od` `look` `col` `grep` `fgrep` `sort` `sed` `pr` `diff` `cal` `tsort` `join` `find` `file` | the corpus everything else is tested against | medium ×24 |
| C6 | multiuser userland — `getty` `login` `passwd` `su` `newgrp` `stty` `who` `write` `wall` `mesg` `mail` | more than one person | medium; gated on kernel 29a |
| C7 | `tar` | getting data on and off without `b6fsutil` | medium |
| C8 | inspection — `ps` `dmesg` `pstat` `iostat` `nice`, `ac` `sa` `accton` | seeing what the machine is doing | medium; needs `nlist(3)` |
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` | a system worth using | open-ended |

C1 through C4 are the ones that matter: they take the machine from *demonstrating a prompt* to
*being a computer you can keep files on and repair*. C5 is cheap and pays for itself in test
coverage. C7 is one program and can be taken at any time; C6, C8 and C9 are each gated on
something the task names.

---

## The porting recipe

Ten things that are true of **every** task below. They are collected here so that no task has to
say them again; a task names only what is unusual about *it*.

### 1. The C11 pass, which is mechanical

`b6parse` is strict C11. No implicit `int`, no K&R parameter lists, no untyped `register i;`, no
`char *malloc();` re-declarations of a library function. Prototypes and explicit return types
everywhere, `static` on file-scope objects and functions. Every v7 source needs this before it
compiles, and it is the least interesting part of any of these ports —
[init/README.md](init/README.md) is the small worked example, [sh/README.md](sh/README.md) the
large one.

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
| `init` | 27 | 820 | 37 | 323 | **1,207** |

Most of `cat` is libc's stdio, and `sh` is the largest v7 command bar the ones C9 and C10 name —
so nothing in C1 through C6 is in danger of the first ceiling. `fsck`, `sort`, `awk` and `make`
are the four to measure early rather than late.

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
4. A line in [../etc/rc](../etc/rc) if the boot script wants it — remembering that the v7 shell
   **has no comment character** and that `/etc/rc` runs with no terminal.
5. The test, per §9.

The disk is one EC-5052: **2000 blocks, 6,144,000 bytes**. Nothing planned here comes close to
filling it.

### 8. Setuid works, and two of these programs need it

`mkdir` calls `mknod(d, 040777, 0)` and `rmdir` calls `unlink` on `.` and `..`; both are
super-user operations, and both programs are **setuid root** in v7. The kernel honours `ISUID` in
`getxfile()` ([../kernel/sys1.c](../kernel/sys1.c), the SUID/SGID block), and `b6fsutil` carries
the bit through: a manifest `mode 04755` reaches the inode as `IFREG | (mode & 07777)`
(`cmd/fsutil/command.cpp`), and 07777 includes 04000. So the manifest stanza is the whole of the
work — but it is **the first setuid program on this image**, so C1a should assert the transition
happened rather than assume it.

### 9. Which world a test runs in

Two harnesses, and choosing wrong wastes the effort:

* **`b6sim`** runs one BESM-6 `a.out` and services its syscalls on the *host*. Good for filters —
  stdin, stdout, files by relative path — and [sh/test/](sh/test/) is the pattern to copy
  (`run-sh-test.sh`, one `.expected` per case). It **cannot** read a directory (a host directory
  descriptor refuses `read`, which is why `ls` has no `b6sim` test) and it cannot exec `/bin/…`,
  because no such path exists on the build machine.
* **SIMH**, under the booted kernel. `kernel/test/console` is the model for a typed dialogue,
  `kernel/test/libtest` for running a program off `/usr/test` and diffing it against a
  `.expected`, and `kernel/test/session` for anything that must *write* and then be fscked on the
  host afterwards.

Most of C5 lands in the first; all of C1, C3, C4 and C6 land in the second. Where a program can
run in both, **do both** — that is the whole point of task 25c, and the first time the libc suite
was run in both worlds it found two bugs nothing else had exercised.

### 10. The manual page comes with the source

Each v7 command ships its `.1`, and it is **already in the program's directory** — see "The
sources are already here" above, including the ten programs that have no page of their own.
Follow the [../lib/libc/man/](../lib/libc/man/) precedent: **correct it in place** — ANSI SYNOPSIS, every
wrong claim fixed where it stands and marked `Note:`, block counts in 3072-byte blocks, `DIRSIZ`
18 where it shows. Nothing installs any of them; they are read with `nroff -man`. A `README.md`
is worth writing only when the port *taught* something, which is the standard `sh` and `ls` set.

---

## C1. The file-management set

**Why first.** There is no way to make a directory, remove a file, rename one or change a mode on
the running machine. Everything else in this file assumes these exist.

All ten are small, none uses `long`, and the grep for `char *` ordering comes back clean on all
but `basename`-adjacent string work. What they *do* all touch is directories, so **none of them
can be tested under `b6sim`** — every test here is a SIMH dialogue in the `kernel/test/console`
mould, or a `session`-style write-then-fsck.

### C1a. `mkdir`, `rmdir` — and the first setuid programs

`mkdir.c` (73 lines) and `rmdir.c` (106). `mkdir` is `mknod(040777)` followed by linking `.` and
`..` into place and `chmod`ing down; `rmdir` unlinks `name/..`, `name/.` and then `name`. Both
need super-user, hence §8 above, and the manifest stanzas are `mode 04755`.

Do these two first and alone, so that "setuid works on this image" is established before anything
else depends on it. The test asserts the directory appears with the right link count, and that a
non-root shell can still use both.

### C1b. `rm`, `ln`, `mv`, `cp`

`rm.c` (164), `ln.c` (58), `mv.c` (299), `cp.c` (92). **The order inside the task is forced by two
`execl`s:**

* `rm -r` execs `/bin/rmdir` (`rm.c:150`), so C1a must be on the image first.
* `mv` across devices execs `/bin/cp` (`mv.c:107`), so `cp` lands before `mv`.

`rm` and `mv` both read directories — §5 — and `mv` calls `setuid(getuid())` at entry, which is
the v7 way of refusing to be setuid; keep it. `mv.c` is the only one with any real logic (the
rename-a-directory dance of link/unlink pairs, which must be checked against a kernel whose
`link` on a directory is super-user-only).

### C1c. `chmod`, `chown`, `chgrp`, `touch`

`chmod.c` (179 — symbolic mode parsing is the whole of it), `chown.c` (57), `chgrp.c` (55),
`touch.c` (72). `chown`/`chgrp` read `/etc/passwd` and `/etc/group` through `getpwnam`/`getgrnam`,
both already in libc. `touch` uses `utime(2)`, which the kernel has.

**Size.** Small ×10. Expect the whole task to be dominated by the tests, not the ports.

---

## C2. The small utilities the shell and `/etc/rc` want

**Why.** `/etc/rc` is nine-tenths a comment explaining what it cannot do yet, and a shell without
`test` cannot write a conditional. These are the cheapest programs in the tree and most of them
run under `b6sim`, so this is also where the userland test corpus starts.

### C2a. `date`, `sleep`, `kill`

`date.c` (165), `sleep.c` (23), `kill.c` (42). `date` is the interesting one: it sets the clock
with `stime(2)` (one word here, not two — see [../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md)),
formats with `ctime`, and consults the `timezone` already in libc. It also has one `char *`
comparison to check. `date` without arguments belongs in `/etc/rc`; `date` *with* them is the
first program that changes global machine state, so its test asserts the clock moved and moved
back.

### C2b. `test`, `basename`, `tty`, `yes`, `time`

`test.c` (191) is the one that matters — it is what makes shell scripts branch, and the v7 shell
has no built-in for it. `basename.c` (31, one pointer comparison), `tty.c` (20), `yes.c` (8),
`time.c` (80, `times(2)` plus a `fork`/`exec`).

All five except `time` test cleanly under `b6sim`.

### C2c. Revive `/etc/rc`

Not a port: as each of C1 and C2 lands, un-comment the line of [../etc/rc](../etc/rc) that wanted
it, and extend `kernel/test/boot`'s expectations accordingly. **This is the visible progress
indicator for the whole file** — the boot script getting longer is what "the system is becoming
usable" looks like from the console. Mind the two rules the file's own header states: no comment
character, and no terminal on descriptors 0–2.

**Size.** Small ×8, and it should be possible to do the whole task in one sitting once C1 has
established the staging rhythm.

---

## C3. `ed` — the editor

**Why it is its own task, and why it is the pivot.** Nothing can be *authored* on this machine.
Every file on the image was written on the build host and staged in; the moment `ed` runs, the
BESM-6 can produce text of its own — a shell script, an `/etc/ttys`, a C source — and that is the
precondition for C9 meaning anything at all.

`ed.c` is 1,764 lines and self-contained: its own regular-expression engine, its own temp-file
paging, `setjmp`/`signal` for the interrupt handling, a `realloc`'d array of line pointers
(`nlall`, growing by 512), and no dependency outside libc. `/tmp` is already on the image at mode
0777.

**What will bite, in order:**

1. **Ten `char *` relational comparisons**, listed in §2 — the densest concentration in the whole
   survey, and every one of them bounds a buffer that regex or substitution is writing into. Each
   is silent corruption, not a fault. Rewrite them all as index counts, as
   [../kernel/prim.c](../kernel/prim.c) and `ls`'s `makename()` did, *before* trying to run
   anything.
2. **`char *` versus `int *` in the same program.** `zero`/`dot`/`dol`/`addr1`/`addr2` are `int *`
   — thin word pointers that compare and subtract correctly — while `linebuf`, `genbuf` and
   `expbuf` cursors are fat. The file mixes them freely and the two behave differently; do not
   assume a fix for one shape applies to the other.
3. **The compiled-expression encoding.** `expbuf` packs opcodes and character classes into a
   `char` array with `CCL` bitmap arithmetic (`1 << (c & 07)`, `c >> 3`). That is byte work inside
   a fat-pointer buffer and it is exactly the shape §2 warns about.
4. `long count;` and the `lseek` prototype at the head of the file — §3.
5. `ed`'s temp file blocks are its own 512-byte ones; **do not** "fix" them to `BSIZE` (§4).

**How to verify.** A SIMH dialogue in the `kernel/test/console` mould: `a`, a few lines, `.`,
`w /tmp/x`, `q`, then `cat` the result — and then the reverse, editing a file `cat` wrote. Follow
it with a `session`-style run so that what `ed` wrote is fscked on the host. A regex torture case
(anchors, `*`, back-references, character classes, the `s///g` path) belongs in the same script,
because the engine is where the pointer work concentrates.

**Size.** Large — the largest single-file port in this document, and the one whose failures will
be silent. Budget the same care `sh` got.

---

## C4. Filesystem maintenance

**Why.** The system cannot repair or extend itself. `kernel/test/session` fscks the image *on the
host*, with `b6fsutil`; the machine itself cannot look at its own filesystem, cannot make a new
one, and cannot mount anything.

Everything in this task encodes the on-disk layout, and there is a rule for that:
**`_Static_assert` against `<sys/param.h>` rather than re-deriving the constants**, which is what
[fsutil/params.cpp](fsutil/params.cpp) does on the host side and why a kernel that retunes `INOPB`
or `DIRSIZ` breaks the build instead of the images. The raw devices these need are already on the
image — `/dev/rmd0` and `/dev/rmb0`, `cdevsw[4]` and `[5]`.

### C4a. `df`, `du`, `quot`

`df.c` (99), `du.c` (169, five `long`s and three directory walks), `quot.c` (237). `df` reads the
superblock off the raw device; `du` walks and sums; `quot` sums per uid. All three report in
blocks — 3072-byte ones (§4), and their manual pages must say so.

### C4b. `dd`

`dd.c` (543). Calls `sbrk` four times for its buffers (§2's third hazard), has a conversion-table
`switch` per byte, and its `bs=`/`count=` arithmetic is the place a `BSIZE` assumption would hide.
Worth having early: it is how anything gets copied to or from a raw device.

### C4c. `mkfs`

`mkfs.c` (618). The first program that can *create* a filesystem on the machine. It has an oracle:
`b6fsutil -n` builds the same layout on the host, so a filesystem `mkfs` writes must pass
`b6fsutil -c` after being pulled back off the disk, and vice versa. Do C4c before C4d — a `fsck`
with nothing to fix is only half tested.

### C4d. `fsck`

`fsck.c` (1,684 lines) — the largest program in C1–C8, and the one with the best test oracle in the
tree: [fsutil/check.cpp](fsutil/check.cpp) already implements the same checks on the host. Every
case the two disagree on is a bug in one of them, and finding out which is the task. Deliberately
corrupt an image with `b6fsutil` and require both to report the same thing.

Measure its size against the 28,672-word ceiling **before** porting rather than after (§6): it is
the first candidate in this file with a real chance of not fitting, and if it does not, the answer
is to split it as v7 split `icheck`/`dcheck` rather than to shrink it.

### C4e. `icheck`, `dcheck`, `ncheck`, `clri`

`icheck.c` (478), `dcheck.c` (218), `ncheck.c` (324), `clri.c` (81). The pre-`fsck` tools, each
doing one of `fsck`'s jobs standalone. Cheap once C4d has taught the layout, and `ncheck`
(i-number → path name) is genuinely useful on its own.

### C4f. `mount`, `umount`

`mount.c` (67), `umount.c` (56), plus `/etc/mtab`. Small, and the point of them is that there is a
second thing to mount — which today there is not: one EC-5052 is the whole store and swap lives on
the drums. Do this task after C4c, so `mkfs` can make the second filesystem that makes `mount`
mean something.

**Size.** Large overall; C4a, C4b and C4f are each small, C4d is the whole weight.

---

## C5. The text filters

**Why.** Two reasons, and the second is the real one. They are the commands that make a Unix feel
like Unix — but more importantly **almost all of them run under `b6sim`**, so this is the phase
that builds a userland regression corpus cheaply, in the harness that does not need a two-minute
boot.

### C5a. The trivial six — `wc`, `cmp`, `sum`, `tee`, `split`, `rev`

`wc.c` (88), `cmp.c` (123, seven `long`s), `sum.c` (50), `tee.c` (97), `split.c` (83), `rev.c`
(46). An afternoon, all six, with a `b6sim` test each. Start here to establish the filter test
pattern, then reuse it for everything below.

### C5b. `tr`, `uniq`, `comm`, `tail`, `od`, `look`, `col`

`tr.c` (134), `uniq.c` (144), `comm.c` (168), `tail.c` (186), `od.c` (252), `look.c` (164),
`col.c` (316). Two notes: **`od` is worth extra care** — ten `long`s, and its whole job is to print
words in octal, which on a 48-bit machine means the default format wants rethinking, not just
porting (a `-w` word dump in 16 octal digits is what this machine needs, beside the byte formats).
And `tail -b` counts in **512-byte** blocks by definition (`n <<= 9`, `tail.c:57`) — that is the
manual page's own unit and not a filesystem block, so §4 does *not* apply to it; decide whether to
keep 512 or move it to `BSIZE`, and say which in the manual page. None of the seven carries a §2
comparison.

### C5c. `grep`, `fgrep`

`grep.c` (480), `fgrep.c` (365). Each carries its own matcher — three §2 comparisons in `grep`,
four in `fgrep` (one of which, `smax >= &w[MAXSIZ-1]`, is over a `struct words *` and must be left
alone) — and a few `long`s. `egrep` is a yacc grammar and is deferred to C10 with the others.

### C5d. `sort`

`sort.c` (903). The heavyweight of the phase: `sbrk`, eight `signal` calls for temp-file cleanup,
its own merge over temp files, and **the worst concentration of §2 in the tree — fifteen `char *`
comparisons, every one of them inside `cmp()`, which is the routine that decides the program's
entire output.** The record arena around it is `char **` and is fine, which is exactly what makes
this one dangerous to skim. Its 28,672-word fit should be measured early. Do it after C5a–C5c, so
the harness is established when the hard one arrives.

### C5e. `sed`

`sed/` (1,690 lines: `sed0.c`, `sed1.c` and `sed.h`). Same regex family as `ed`, so **do it after C3** and reuse
what that taught — including the three `char *` comparisons in `sed1.c`, which are the same
`genbuf` bound `ed` has.

### C5f. `pr`, `diff`, `cal`, `tsort`, `join`, `find`, `file`

`pr.c` (424), `diff.c` (647), `cal.c` (206), `tsort.c` (198), `join.c` (216), `find.c` (725,
`sbrk` and directory walks), `file.c` (323).

**`file` gets one deliberate change rather than a faithful port:** teach it this machine's magic
numbers — `FMAGIC`/`NMAGIC` from [../cross/besm6/b.out.h](../cross/besm6/b.out.h), the archive
`ARMAG` from `ar.h` — and delete the PDP-11 ones. [../doc/File_Magic.md](../doc/File_Magic.md) is
the specification and already exists.

`diff` shells out to `diffh` for large files in v7; port `diffh.c` (264) with it or drop that path.

**Size.** Medium ×24, but the per-program cost is the lowest in this document and the test payoff
the highest.

---

## C6. Multiuser userland

**Gated on [../kernel/TODO.md](../kernel/TODO.md) task 29a** — the terminal driver. There is one
terminal today, the operator's Consul, and `dev/sr.c` is a skeleton. Nothing in this task can be
tested until 29a lands.

**Kernel task 29b is the first half of this task seen from the other side** and should not be
duplicated: it covers `getty`, `login` and `/etc/ttys`, and records that the libc side is already
in place — `crypt`, `getpwnam`/`getpwent`, `ttyname`, `getlogin` and `<utmp.h>` all exist and
`init.c` already uses them. When 29b is done, this task is what remains.

* `getty.c` (240) and `login.c` (151) — kernel 29b.
* `passwd.c` (172), `su.c` (52), `newgrp.c` (57) — the account trio; `passwd` needs a writable
  `/etc/passwd` and the `crypt` already in libc.
* `stty.c` (303) — reads and writes the `sgttyb` the kernel's `sc.c`/`sr.c` implement. Its capability
  list must be cut down to what those drivers actually honour rather than carried whole.
* `who.c` (64), `write.c` (186), `wall.c` (70), `mesg.c` (57) — the social four, all `/etc/utmp`.
* `mail.c` (556) — only if `/usr/spool/mail` is wanted; it is the one program here with no reason
  to exist on a single-terminal machine.

**Size.** Medium, and mostly mechanical once the driver works — but do not start it before 29a, or
a `login` that never prompts is indistinguishable from a driver that never delivers a character.

---

## C7. `tar`

**One program.** `tar/` is a single 935-line source, and the reason it is worth having is not
tape: it is that a `tar` on the machine is how a tree gets moved between the BESM-6 and a
host-built image **without** `b6fsutil` — `tar cf /tmp/x`, or straight onto a raw disk device with
`tar cf /dev/rmd0`. Both work today; nothing in the program needs a device this kernel has not
got.

Two things to settle while porting it:

* **The block size.** `tar` writes 512-byte records in 20-record blocks by definition of the
  format, and that is the *archive's* unit, not the filesystem's — §4 does not apply, and changing
  it would make the archives unreadable anywhere else. Keep 512, and let `TBLOCK`/`NBLOCK` stand.
* **`DIRSIZ` is 18 but a `tar` header name field is 100 bytes**, so names survive the round trip
  in one direction only. Say so in the manual page rather than discovering it on a restore.

The tape half of v7's archiving — `tp`, `dump`, `restor`, `dumpdir` — is **not in this plan**; see
the exclusion table.

**Size.** Medium, and unblocked.

---

## C8. Process and system inspection

**Two findings shape this task**, and neither is a line count.

**First: libc has no `nlist(3)`.** Nothing in [../lib/libc/](../lib/libc/) reads a symbol table,
and every program here needs one to find a kernel variable by name in `/unix`. That is a real libc
addition — `nlist()` over this machine's `a.out` symbol format ([../doc/Linker_Manual.md](../doc/Linker_Manual.md)),
and `cmd/nm` is the host-side reader to copy from. **It is the first sub-task and everything else
waits on it.**

**Second: do not port v7's `ps`.** `ps.c` (408 lines, 17 `long`s) reads
the proc table and then fetches each u-area *through the swap device* using PDP-11 memory-management
assumptions that have no counterpart here — this kernel's u-area is two fixed physical pages at
`074000` and its swap layout is its own. Write `ps` against **this** kernel instead, through
`/dev/kmem`, which works and is proven: `lib/test/memt` already reads its own `struct user` at
`074000`, follows `u_procp` into the proc table, and reads physical memory above `0100000` through
`copyphys()`. That is exactly the ladder a `ps` climbs. Keep v7's *output format* and its `.1`;
replace its middle.

The rest, in order of value: `dmesg.c` (116) — needs the kernel to keep a message ring, which
`prf.c` does not do yet, so it carries a small kernel task with it; `nice.c` (28) — trivial and
independent of `nlist`, do it in C2 if convenient; `pstat.c` (385) and `iostat.c` (289) — both
deeply tied to kernel structures and both worth rewriting rather than porting; `ac.c` (251),
`sa.c` (489), `accton.c` (16) — process accounting, which the kernel's `acct()` supports, and which
nothing needs.

**Size.** Medium, and front-loaded: `nlist` is the task, the rest follows.

---

## C9. Self-hosting: the toolchain on the machine itself

**State the exclusion first, because it is the whole shape of this task.** v7's `cc.c` (387),
`as/` (4,095), `ld.c` (1,257), `nm.c` (229), `ar.c` (707), `size.c` (48), `strip.c` (113),
`ranlib.c` (160) and `adb/` (3,547) **are not ports.** They speak PDP-11 `a.out`, PDP-11 opcodes
and PDP-11 registers; nothing in them survives retargeting. The BESM-6 versions already exist, in
this directory, and — with the exception of `cmd/sim` and `cmd/fsutil`, which are C++ and therefore
out of reach until there is a C++ compiler — **they are all plain C**. So the task is not to port
anything: it is to build what is already here a *second* time, for the target. That is the fourth
category [cpp/TODO.md](cpp/TODO.md) opens by naming.

### C9a. `cpp`

**See [cpp/TODO.md](cpp/TODO.md)**, which is a complete plan already: three external-compiler bugs
(B1–B3), one libc gap (G1), and two address-space limits (L1, L2), each with a minimal repro. Do
not restate any of it here. It is the gate for the rest of this task, and the three compiler bugs
are gates for far more than `cpp`.

### C9b. `as`, `ld`

[as/](as/) (12 sources) and [ld/](ld/) (9). Both plain C, both already reading and writing this
machine's `a.out` through [libaout/](libaout/), which builds natively too. Expect the same two
limits `cpp` hit: a symbol table that must live inside 32,767 words, and frames that must fit
4,096. Both are already designed around fixed tables rather than unbounded growth, which helps.

### C9c. The binutils and the driver

`ar`, `nm`, `size`, `strip`, `ranlib`, `lorder` (a shell script, so free), and `cc` — the driver,
which needs nothing but `fork`/`exec`/`wait` and a path search. Once `as` and `ld` run on the
machine, this is the short tail that makes them usable.

**What it does not include.** `b6sim` and `b6fsutil` are C++ and stay host-only. `b6disasm` is C
and could come along for free.

**Size.** Large, and its first three-fifths are blocked on a foreign repository. But it is the
task that changes what this port *is*.

---

## C10. The rest of the manual

Everything else worth having, in no fixed order, once C1–C5 are in place.

**A decision this task must record first, and it reaches further than it looks: six of these are
yacc grammars** — `expr.y` (669), `egrep.y` (594), `bc.y` (600), `make/gram.y`, `m4/m4y.y` and
`awk/awk.g.y` — and `awk` is a **lex** scanner besides (`awk.lx.l`). There is no native `yacc` and
no native `lex`; v7's own are 2,249 and 2,980 lines and would each have to be ported first, which
is a worse deal than any program they would generate. Two ways out: check the generated C into the
tree beside the `.y`, or add a host `yacc`/`bison`/`flex` dependency to the build.
**Recommend checking in the generated parser**, with the grammar beside it and a note in the
program's README saying which host `yacc` produced it — the build stays dependency-free, which is
a property this project has kept so far and should not spend lightly.

Note that this catches `make` and `m4`, the two most valuable items in the table below. Settle the
decision before either is started, not during.

| | | lines | note |
|---|---|---|---|
| `make/` | the build tool | 2,047 | the highest-value item here; measure against the word ceiling early |
| `awk/` | | 2,700 | yacc; also the most float-dependent program in the tree — read [../lib/libm/README.md](../lib/libm/README.md) on what overflow does here |
| `m4/` | macro processor | 995 | |
| `dc/`, `bc.y` | calculators | 1,943 + 600 | `dc` is the engine, `bc` the yacc front end |
| `expr.y` | shell arithmetic | 669 | yacc; wanted by scripts almost as much as `test` |
| `egrep.y` | | 594 | yacc; finishes C5c |
| `units.c` | | 466 | needs `/usr/lib/units` staged |
| `crypt.c`, `makekey.c` | | 93 + 21 | libc's `crypt` already exists |
| `at.c`, `atrun.c`, `cron.c`, `calendar.c` | scheduling | 307 + 110 + 254 + 54 | want a running multiuser system and a correct clock; after C6 |
| `update.c` | periodic `sync` | 38 | trivial, and `/etc/rc` wants it — could go in C2 |
| `strip`, `size`, `nm` | | | **not these** — see C9 |

---

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` alone is larger than everything in C1–C4 together, it drives a CAT phototypesetter that does not exist, and **there is no `nroff` in this source tree at all** — only `troff`. This repo's own manual pages are read with the *host* `nroff`, which is the right answer for the foreseeable future. `spell` additionally needs its whole word list. |
| `tp/`, `dump.c`, `restor.c`, `dumpdir.c` | 800 + 641 + 1,150 + 475 | Tape. **This kernel has no tape driver** and no `bdevsw`/`cdevsw` row for one, and all four are built around a tape's sequential access rather than merely willing to use it — `dump`/`restor` are a filesystem-level backup pair whose whole design is the reel. `tp` is the pre-`tar` archiver and is superseded by it in any case. If a magnetic-tape driver is ever written (a `kernel/TODO.md` item nobody has raised; [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) is the reference), reconsider `dump`/`restor` and not the other two. |
| `uucp/`, `cu.c` | 6,415 + 541 | Dial-out over a modem link nothing models. `cu` becomes conceivable if the serial multiplexor (kernel 29a) is ever wired to something outside. |
| `lpr/`, `vpr.c` | 1,315 + 334 | Printer spooling. **Worth revisiting:** SIMH *does* model the АЦПУ drum printer, so `lpr` becomes a small task the day a kernel printer driver exists — which is a `kernel/TODO.md` item nobody has written yet. |
| `graph.c`, `plot/`, `spline.c`, `tc.c`, `tk.c` | 695 + 608 + 335 + 638 + 250 | Plotters and Tektronix terminals; no hardware, and the output would go nowhere. |
| `learn/` | 1,066 | Needs the entire `/usr/lib/learn` lesson corpus, which is not in this tree. |
| `adb/` | 3,547 | PDP-11 instruction decoding, PDP-11 core files, PDP-11 `ptrace` semantics. A BESM-6 debugger is **new work**, not a port — and [disasm/](disasm/) plus `ptrace` (kernel task 33) is where it would start. |
| `lint/`, `mip/`, `struct/`, `ratfor/` | 1,164 + 7,615 + 4,721 + 1,200 | `lint` and `mip` are the PDP-11 C compiler's own internals; `struct`/`ratfor` are Fortran-to-Ratfor tooling with no Fortran here. |
| `osh.c` | 846 | The pre-Bourne shell. [sh/](sh/) supersedes it. |
| `xsend/` | 414 | Secret mail. Needs `mail` first, and wants nothing. |
| `cc.c`, `as/`, `ld.c`, `nm.c`, `ar.c`, `size.c`, `strip.c`, `ranlib.c`, `arcv.c` | | PDP-11 `a.out`. The BESM-6 tools are in `cmd/` already — **see C9**. |
| `random.c`, `sp.c`, `tk.c`, `sa.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |

---

## Where to start

C1a. It is two small programs, it establishes setuid on the image, and it is the first time this
system can make a directory of its own.
