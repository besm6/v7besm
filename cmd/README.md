# Porting the v7 userland to the BESM-6

`cmd/` holds two kinds of subdirectory:

* **host tools** — `cc`, `as`, `ld`, `cpp`, `disasm`, the binutils, `sim`, `fsutil` — built by the
  build machine's own compiler. They have their own `README.md`s and their own chapters in
  [../doc/](../doc/); nothing below is about them.
* **native BESM-6 programs** — the v7 commands that go on the disk image, built by the `b6*`
  toolchain and staged into `build/rootfs/`.

**This file is the manual for the second kind**: the porting recipe and the table of what was
refused and why. Read [sh/README.md](sh/README.md) (the largest port there has been) and
[ls/README.md](ls/README.md) first.

## The sources are already here

**Almost every program this port ever considered is in this directory**, one directory per
program — the source, its auxiliary files and its manual page, taken from the v7/x86
distribution's `usr/src/cmd/`. Anything not ported is a **verbatim upstream copy**, so taking one
up starts by writing a `CMakeLists.txt` rather than by fetching anything.

**Five directories are the exception**, having no v7 original or a better source than one:
[novi/](novi/) (full-screen editor), [more/](more/) (Berkeley pager via RetroBSD), [man/](man/)
(Berkeley's 1987 C rewrite — v7's `man` is a shell script around an `nroff` that does not exist
here), and [yacc/](yacc/) + [lex/](lex/), both from RetroBSD because its yacc is 4.2BSD's with the
ANSI pass already done and its lex carries the `<paths.h>` fixes. In the last two the manual page
still comes from v7's `usr/man`.

**A directory is part of the build when it holds a `CMakeLists.txt`** — that is the only marker;
[../CMakeLists.txt](../CMakeLists.txt) names its subdirectories one by one.

Notes on the copies:

* The v7 `makefile` came along with each multi-file program as the record of its source list and
  flags. It is a PDP-11 recipe, kept for reading; nothing runs it.
* **Ten programs have no manual page of their own.** Eight are documented inside another program's
  page — `rmdir` in [rm/rm.1.umm](rm/rm.1.umm), `chgrp` in `chown.1`, `umount` in `mount.1m`,
  `fgrep`/`egrep` in `grep.1`, `diffh` in `diff.1`, `atrun` in `at.1`, `accton` in `sa.1m`.
  **`yes` and `dmesg` have no page anywhere in v7** and need one written.
* **The file-format pages are in [../include/man/](../include/man/)**, not here. Five are nothing
  but `.so /usr/include/…`, and there is no `/usr/include` here: write the structure out, as
  [../lib/libc/man/](../lib/libc/man/) did, with `DIRSIZ` 18 and one-word `off_t`/`time_t`.
* **Three programs carry a data file**: [units/units](units/units), [cron/crontab](cron/crontab)
  and [calendar/calendars/](calendar/calendars/), staged as `/usr/lib/units`, `/usr/lib/crontab`
  and `/usr/lib/calendars/*` — PLURAL, because `/usr/lib/calendar` is the program.
* The `.y`, `.l` and header-ish files carry **no v7 copyright banner**; the top-level `COPYRIGHT`
  covers them, so do not add one.

---

## The porting recipe

Twelve things that are true of **every** port.

### 1. The C11 pass, which is mechanical

`b6parse` is strict C11: no implicit `int`, no K&R parameter lists, no untyped `register i;`, no
`char *malloc();` re-declarations. Prototypes and explicit return types everywhere, `static` on
file-scope objects. [init/README.md](init/README.md) is the small worked example,
[sh/README.md](sh/README.md) the large one.

**Watch for names C11 or libc already owns that v7 used freely** — `chmod.c` defined `abs()`,
`chown.c` defined `isnumber()`, `mail.c` defined `lock()` against the real `lock(2)` stub. None
fails to link; the program's own definition wins until something drags the other object in. Check a
new port's file-scope names with `b6nm` over `libc.a` rather than trusting the link.

**A multi-file v7 program almost certainly defines its globals in a header** (`sh/defs.h`,
`sed/sed.h`): `char genbuf[LBSIZE];` in a file every source includes, with the PDP-11 linker
merging the copies. **C11 has no tentative definition across translation units and `b6ld` has no
common symbols**, so left alone each source gets its own storage, silently. `extern` in the header,
defined once in a file of its own (`sh/glob.c`, `sed/sedglob.c`); `b6nm` is the check.

**A header of the program's own is a build blind spot.** `b6_obj`'s header dependency is the
*system* header tree, so editing `sed/sed.h` or `sh/defs.h` rebuilds nothing. Touch a `.c` after
changing one.

### 2. An `int` is not a `char *`

A `char *` here is a **fat pointer** — byte offset in bits 47–45, word address in bits 15–1 — and
the offset *decrements* as the pointer advances, so the raw word does not sort. The compiler deals
with it: a relational between two byte pointers lowers through **`b$pdiff`** and orders them
correctly; `==`/`!=` are raw word compares and are right too. A `<` between two `char *` is not a
bug to hunt — but it is an out-of-line call, so rewrite one that runs once per byte, **as a shadow
counter and not as an index**. Turning the cursors into subscripts buys nothing: `b$padd` sits
beside `b$pinc`/`b$pdec`, so `buf[i]` is a call exactly as `*p++` is one. Only the *comparison* is
removable, so keep an `int` alongside the pointer and test it in the pointer's place.
[m4/README.md](m4/README.md) carries the number: retiring three relationals took **21% off the
instruction count** of a whole run.

Live hazards the compiler does not cover:

* **A fabricated pointer matches nothing** — `(char *)-1` (v7's `sbrk` failure test) can never
  equal a real fat pointer.
* **A relational between two `void *` is a hard error** (C11 6.5.8 wants complete object types);
  cast to `char *`.
* **A pointer formed below the base of its array** is UB and the guard need not fire on a
  word-address machine (`comm.c`'s `lb1 - 1`, `diff.c`'s shellsort underflow). Use an index pair.
* **Three arena hazards**, in anything that manages its own storage: a flag packed into bit 0 of a
  pointer, a mask that rounds to a word assuming `BYTESPERWORD`, and a **cast to a thin pointer,
  which FLOORS a fat one to its word** — `find`'s parse tree stored a pattern through a struct
  pointer and lost the byte offset. Grep for them rather than expecting them wherever `sbrk` is
  called.
* **An offset survives a `realloc` and a pointer does not** — the one case where the advice above
  inverts, and `cron`'s `init()` differenced a pointer it had already freed. Note also that
  `realloc(NULL, n)` is `malloc(n)`, and a **failing `realloc` has already freed the old block**.

Where to look: **a v7 source grows byte cursors when it parses, not when it reads bytes.** `sort`,
`grep`, `sed` and `pr` hold a cursor inside a buffer they are deciding about; plain filters hold
`int` indices already.

### 3. A `long` is one word, and `%D` is not a conversion

`long` is `int` is one 41-bit word.

* `%ld` is harmless — `l`, `h`, `L`, `j`, `z`, `t` are parsed and ignored — but means nothing;
  write `%d`.
* **`%D` prints the two characters `%D`.** [../lib/libc/stdio/doprnt.c](../lib/libc/stdio/doprnt.c)
  does not know that PDP-11 conversion, and an unknown conversion is echoed verbatim **and consumes
  no argument**, desynchronising every later conversion in the same format string. Grep each new
  source for `%D` and `%O`; it is still finding them.

**And a `double` is one word too**, with no IEEE anything behind it: `5.42e-20` to `9.22e+18`,
twelve digits, `sizeof(double) == sizeof(float) == 6`. Two consequences bind every port that
computes with real numbers. **An overflow is a machine FAULT, not a signal** — `kernel/trap.c`
turns it into `SIGFPE`, which kills the process — so a range test goes *before* the operation;
underflow is the opposite and raises nothing, quietly becoming zero. And **an intermediate may
leave the range where the answer does not**: `units`' table has `1.6021917-19`, a representable
number whose v7 scaling built `1e26` first ([units/README.md](units/README.md)). `printf` carries
`%e`/`%f`/`%g` and clamps to twelve digits; `strtod` is declared and not implemented, so use
`atof`.

The other direction: **plain `char` is unsigned here**
([../doc/Besm6_Data_Representation.md](../doc/Besm6_Data_Representation.md)), so the
`(unsigned char)` a `<ctype.h>` call wants is habit rather than necessity — and a `signed`→
`unsigned` conversion is a bare reinterpretation of the word, not C11's modulo adjustment, an `int`
occupying bits 41–1 and an `unsigned` all 48. Prefer `int` where v7 wrote `unsigned` for no reason.

### 4. A filesystem block is 3072 bytes; a *reported* one is 1024

`BSIZE` is 3072 — not a power of two, and there is no `BSHIFT`/`BMASK`
([../include/sys/param.h](../include/sys/param.h)). Every `>>9`, `<<9` and `&0777` that means *a
filesystem block* becomes a divide or a remainder. (`BWSHIFT` 9 and `BWMASK` 0777 exist and are
**word** offsets within a block, a different quantity.)

**But a count reported to a user is not in that unit.** `df`, `du`, `quot` and `ls -s` print `KBPB`
— three — per block, so the output is in **1024-byte blocks**. Three rules come with it:

* **The multiply goes at the `printf` and nowhere else**, so a variable called `blocks` holds
  blocks. `quot -c`'s histogram is *indexed* by a block count and forces this.
* **Assert the unit divides**: `_Static_assert(BSIZE % KBYTE == 0, …)`.
* **Say it in the manual page**, in a `BLOCKS ARE 1024 BYTES` section.

**A block that is the program's own business is converted to neither** — `ed`'s temp file, `tar`'s
record, `tail -b`, `dd`'s `bs=`. **But a default is not the user's**: `dd`'s `ibs`/`obs` and `b`
suffix are `BSIZE`, not v7's 512, because 512 is not a whole number of words and `physio()` refuses
it. **A constant is the user's business only while it still names something on this machine.** And
**a size that names a *device* stays in filesystem blocks** — `mkfs special nblocks` takes
`s_fsize` verbatim, and its page owes the mirror section, `BLOCKS HERE ARE 3072 BYTES`.

### 5. `DIRSIZ` is 18, and `opendir(3)` is the only reader

`struct direct` is four words — a full-word `d_ino` and three words of name — and a name read out
of a directory **is not NUL-terminated**.

**No port has to care, because they all use `opendir(3)`**: `readdir()` skips the free slots,
plants the terminator and hands back `d_namlen`. **Not one program in `cmd/` reads a
`struct direct` out of a pathname**, so hand-rolling `<sys/dir.h>` is a regression rather than a
shortcut. [`make/files.c`](make/files.c)'s `srchdir()` is the worked example.

**Five programs still include `<sys/dir.h>`, and must**: `fsck`, `mkfs`, `ncheck`, `dcheck` and
`pstat` read a `struct direct` out of a block they fetched from `/dev/rmd*` themselves, having no
descriptor to open. **That is the test**: a *pathname* takes the library; a *block* takes the
header.

Two things the library does not do for you:

* **`DIRSIZ` written somewhere that is not a directory read at all** survives the conversion —
  [`atrun`](atrun/) took `opendir(3)` and still had the `14` in `sprintf("/bin/mv %.14s %s", …)`.
  **Grep for the number, not for the loop.** So too for a bound whose correctness depends on a
  constant it does not name: `sh/expand.c` terminated a name once before its loop, which worked
  only while `DIRSIZ` was 15 against a 14-byte name.
* **It hides a descriptor budget.** `du` and `find` descend arbitrarily deep because above a
  descriptor ceiling they drop the directory and re-take it, which a plain
  `while ((dp = readdir(dirp)))` cannot do — the cursor lives in the `DIR`. Both fill an array of
  names, *then* recurse over it, with `telldir()`/`seekdir()` across the `closedir()`/`opendir()`
  pair. That also bounds the **heap**: a `DIR` costs twelve words plus a block-sized buffer, and
  without the budget a deep tree holds one per level.

### 6. Ceilings, of which only two are checked

* **28,672 words** of `const + text + data + bss` — 32 pages less the four the stack takes.
* **Word 32,767** — no relocatable symbol above the reach of a 15-bit pointer.
* **A struct may not exceed 4,096 words.** A member is named by a 12-bit offset from a base
  register and there is no longer form, so `b6as` refuses it (`short address out of range`). This
  is the architecture, not a compiler defect; it bites **every v7 program that keeps its state in
  one big struct**, it is invisible until the assembler speaks, and the answer is always the same:
  **move the big arrays out of the struct to file scope**, where an index register reaches them at
  any size. `cpp`'s `struct cppstate` was ~38,630 words and is ~400 that way.
* **4,096 words of stack** at `070000` — the ceiling that bites hardest, because nothing warns.
  See below.
* **The heap**, which is not checkable: `rootfs_<name>_size` cannot see a byte of allocated
  storage. `col`'s worst case is past the whole address space; `sort` takes every page the break
  will give. **Ask what a program will still need to allocate after it has taken what it wanted** —
  a stream whose `malloc(BUFSIZ)` fails does not fail, it becomes one `read(2)` per byte. And
  **measure the break, not the request**: `malloc` grows the arena a 1,024-word page at a time, so
  a 1,196-word block costs two pages while an 854-word one costs one.
* **A bound the user chooses at run time**, checkable only by the program itself: `pr`'s look-ahead
  ring capacity is a function of `-l` and the column count.

A stack overflow does not fault: a user address is 15 bits and the process owns all 32 pages, so
a store past `077777` **wraps mod 2^15 onto word 0** and rewrites the program's own const image.
What comes back is a garbled message, a table that reads as something else, or a signal from a
place that makes no sense — never a diagnostic. So:

* **Read the prologue** (`15 utm 0NNN` in the `.dis` that `b6_prog()` writes) rather than
  estimating a frame, and re-read it after any edit — a conversion can move it either way.
* **Scalars are not free.** Every distinct compiler temporary is a permanent frame slot, so a long
  function costs 1.5–2 words per source line with no arrays at all; `sed`'s `fcomp` is 700.
* **A `switch` pays for every arm on every call**, which makes a big `switch` inside a recursion
  the shape to look for. Splitting `sh`'s `execute()` into a function per arm — the code unchanged
  — took its resident frame 402 → 99 words and its nesting ceiling from eight levels to sixteen;
  `cpp`'s `main` went 531 → 41 the same way. The other moves that pay: big scratch to the heap when
  the function recurses, `static` when it does not. And bound every fixed path buffer v7 filled
  with an unbounded `sprintf`/`strcpy` from `argv`.
* **A recursion whose depth the input chooses needs a ceiling of its own** — `grep`'s `MAXDEPTH`,
  `cpp`'s `MAXARGDEPTH`, `sh`'s `deepchk()` — and a `_Static_assert` holding two ceilings in order
  (`tar`'s `MAXDEPTH <= STACKDEPTH`) makes it a check rather than a hope. **Sometimes no ceiling
  will do**: `lex`'s parse-tree walk is as deep as the file has rules and `awk.lx.l` wants 96
  frames, so that walk is **iterative**, with an explicit worklist in bss
  ([lex/README.md](lex/README.md)).
* **The argv/envp block sits at the base of that same stack** (`argc` is at absolute `070000`,
  [../kernel/sys1.c](../kernel/sys1.c)), so a program has several hundred words less under a login
  shell's environment than under a test harness's `env -i`. `sh` passed every test in the tree and
  died on the machine for that reason. **Padding the environment reproduces it on the host**, and
  `b6sim -d` measures the peak: `grep -o 'M17 = [0-7]*' trace | sort -u | tail -1`, less `070000`.

`b6_prog()` registers `check-size.sh` for the first two ceilings as ctest `rootfs_<name>_size`. For
scale, against the 28,672-word ceiling: `ld` 23,951 words, `cpp` 23,826, `fgrep` 20,019, `as`
19,824, `sed` 14,120, `fsck` 10,842, `sh` 9,008, `sort` 6,822. **The three toolchain programs are
where the ceilings really bind** — each carries a `besm6` size profile, and `cpp`'s is below what
C11 asks for. **What a program prints with dominates what it does**: everything that links stdio
carries ~1,030 words of bss and ~2,500 of common text, while `test`, `tee`, `tail` and `getty`
write with `write(2)` and cost a fraction of it (`getty` 434 words, an eleventh of `cat`). Measure
a large candidate early, and **measure a struct rather than deriving it** — `char` members pack six
to a word inside a struct exactly as in an array; only its overall size rounds up.

Where a fixed buffer can be sized so that nothing has to test it, do that instead of adding a test:
**a bound test that is not on every path reads exactly like one** ([grep/README.md](grep/README.md)).

### 7. How a program gets onto the image — five steps

1. `cmd/<x>/CMakeLists.txt`: one `b6_prog(<x> DEST bin/<x> SOURCES <x>.c)` call
   ([../scripts/BesmCross.cmake](../scripts/BesmCross.cmake)). `PURE` only for something that runs
   in several processes at once; it costs a page-aligned data segment.
2. `add_subdirectory(cmd/<x>)` in [../CMakeLists.txt](../CMakeLists.txt), **inside the
   `libruntime.a` guard and after `add_subdirectory(lib)`**.
3. A stanza in [../scripts/root.manifest](../scripts/root.manifest): `mode`, `file /bin/<x>`, `source
   ../../rootfs/bin/<x>`. Paths resolve against `b6fsutil`'s working directory
   (`build/kernel/test`), not against the manifest.
4. A line in [../etc/rc](../etc/rc) if the boot script wants it — `/etc/rc` runs with **no
   terminal**, so anything meant to be seen redirects to `/dev/console` for itself. Its assertion
   goes in `kernel/test/multi`, the one test that types the `^D` past the single-user shell.
5. The test, per §9.

One list must grow with the program and nothing catches it but a failing build: `ROOTFS_FILES`
in [../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt). **A port is not done when it
compiles**: it is done when `make` builds, `ctest` passes, and the program is on the image with a
test asserting it.

**The disk is one EC-5052: 2000 blocks, 6,144,000 bytes, and about 92 are free.** That is room for
a good deal but not for anything: weigh a large addition *before* the port, and **take the number
from a build** — `b6fsutil` prints it every time `root.img` is made — rather than from this
paragraph, which has drifted before. What to budget for:

* A program is not the only cost. Any **directory** it needs is a block; a program past 6 blocks
  pays an **indirect block** as well, an inode holding six direct addresses; and an edit to a file
  **already on the image** can cross 3072 bytes and cost a block that looked free.
* A **data file can dwarf the program**: `calendar` is 42 blocks of which 29 are the database.
  Curation is a measurement, not a preference — four of its eight files cover 114 days of the year
  against 362 for all eight, at 4 blocks against 29, and the whole set was chosen on that number.
* **What a program links is decided by its smallest call.** `ed` grew five blocks when `-x` came
  back, because `getpass(3)` brings the whole of stdio with it; `sh` paid 1,028 words for
  `opendir(3)` because `opendir()` calls `malloc()` and the shell had never linked an allocator,
  where six other converts paid 190–284. Read `b6size` before and after, not the diff. The cheapest
  thing here is `/etc/update`: one block, 152 words, no stdio.

### 8. Setuid works, and it is asserted

The kernel honours `ISUID` in `getxfile()` ([../kernel/sys1.c](../kernel/sys1.c)) and `b6fsutil`
carries the bit through, so a manifest `mode 04755` is the whole of the work. `mkdir`, `rmdir` and
`mv` borrow root for one `suser()`-gated syscall (this system has no `mkdir(2)`, `rmdir(2)` or
`rename(2)`); `passwd`, `su` and `newgrp` change **who the caller is**, and `su` cannot give it back
— `setuid(2)` moves the real id and there is no saved id. `getxfile()` takes the ISUID branch only
`if (u.u_uid != 0)`, so **a test must reach it deliberately**:
[../lib/test/suidt.c](../lib/test/suidt.c) drops to uid 7 itself and execs `/bin/mkdir`.

**Most programs do not want it.** `chmod(2)` is gated on `owner()` and `chown(2)` on `suser()` —
and that second gate is what stops a user giving a file away, so a setuid `chown` would defeat its
own purpose. Ask what call actually needs privilege before reaching for `04755`. Four rules for the
ones that do:

* **Ask what the program is actually reading.** `ps` and `df` were root-only and must never be
  setuid — but neither wanted the *device*: `ps` wanted four fields of a u-area and `df` four
  counts out of a superblock, so the kernel was made to answer instead (`KCTL_PSINFO`,
  `statfs(2)`) and both are ordinary `0755` commands now, with the device modes untouched — which
  is the *first* thing [../lib/test/unprivt.c](../lib/test/unprivt.c) asserts, since without that
  negative control "`ps` printed a table" reads the same whether the kernel grew an interface or
  somebody widened a node.
* **An operation on a call that already exists costs no system-call number.** `KCTL_PSINFO` is one
  `#define`, one struct in `<sys/kctl.h>` and one arm in [../kernel/kctl.c](../kernel/kctl.c) — no
  libc stub, no `b6sim` arity entry. [../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md)
  §6 says where the line between the two shapes falls.
* **The drop point is the design.** With no saved id, a program that drops privilege and needs it
  back must re-exec: `mail` drops in `printmail()`'s first statement, so its interactive `m user`
  command forks a fresh `mail user` to deliver. Where privilege is needed on both sides of user
  input, a second process is the answer rather than a later drop.
* **A world-writable spool plus "run this as its owner" is a hole.** `link(2)` needs no permission
  on the source and carries the owner along, so any user could hard-link a root-owned file into
  `/usr/spool/at` and have `atrun` `setuid(0)` and hand it to `/bin/sh`. A job `at` created has
  exactly one link, so anything with more is refused.

### 9. Which world a test runs in

* **`b6sim`** runs one BESM-6 `a.out` and services its syscalls on the *host*. Good for filters.
  **`b6_progtest(<prog> <case>)` is the harness**: files `<case>.args` / `<case>.expected` /
  optional `<case>.in` / `<case>.status` in `cmd/<x>/test/`, ctest `cmd_<x>_<case>` under label
  **`cmd`**. It runs the program **as staged for the image**. [sh/test/](sh/test/) is the other
  shape, a whole shell script per case.

  It can be handed a whole filesystem — a flat `b6fsutil` image is a file, so `df`, `quot`, `mkfs`
  and `fsck` are tested against real fixtures, `mkfs` byte-for-byte against `b6fsutil -n` and `fsck`
  against `b6fsutil -c` after `b6fsutil -D` damage. An oracle that is a property of the whole image
  should be **recomputed, not checked in**, and **a test that repairs must assert that there was
  something to repair.**

  What it cannot do: read a **directory** (so `ls`, `du`, `find` have no case — and `opendir(3)`
  does **not** rescue this: on the host `open(2)` and `fstat(2)` on a directory both *succeed* and
  only `read(2)` refuses, so a walk returns no entries rather than an error and the case would pass
  while proving nothing); exec `/bin/…`; set global state (`stime(2)` is a no-op reporting success);
  name a pid (`kill(2)` is the host's). Its `argv[0]` is the staged absolute path, so a program that
  prints its own name belongs under the kernel; `.args` is split on whitespace with no quoting; a
  program that does not terminate cannot be a case (`yes`); standard output is captured as
  **`<case>.out` in the working directory**, so no fixture may be named that. And **it does not
  reproduce the machine's environment**, which is where `sh`'s stack defect hid (§6).

  **The general question: when a program names a fixed absolute path or a global system state, ask
  whose it is under `b6sim`** — `mount(2)` would graft a filesystem onto the build machine. Two
  classes are exempt, both answered by b6sim itself
  ([../doc/Aout_Simulator.md](../doc/Aout_Simulator.md) §7): `kctl(2)` and the memory devices, from
  an imitation kernel — so `ps`/`pstat` can be checked for formatting, but not for plurality,
  `proc[]` holding one live entry — and the static `/etc` files, from
  [sim/etcfiles.cpp](sim/etcfiles.cpp), so `getpwent(3)` reads the same bytes the image carries.
  `passwd(1)` is left a clean **limit**: it `creat`s that file and gets `EROFS`.

  **Ask which side of those limits a program falls on before designing its cases.** It can land
  wholly outside — `mount`/`umount` and `find` have no `b6sim` half at all — but more often it is a
  **branch**: `tar`'s `c` path walks a tree while `t`, `x` and `r` do not. And **a case whose
  program can write to its own input cannot read that input out of the source tree**, or the first
  run edits a checked-in fixture and every later run tests something else; [mail/test/](mail/test/)
  copies its fixtures into the build directory and names them relatively.

* **SIMH**, under the booted kernel — for everything the above cannot say, and **there is very
  little of it left**. Three tests boot: `boot`, a smoke test that a shell prompt is reached;
  `multi`, which goes into multi-user mode and types at two Consuls; and `core`, which mounts the
  test pack. So a fact `b6_progtest` cannot assert — a quoted argument, an empty one, a temp file
  in the image's own `/tmp`, a second process — has nowhere to go. Say so in the port's
  `README.md` and check it by hand, as [expr/README.md](expr/README.md) does.

  **A stage in `multi` is the cheap way to assert what `b6sim` cannot**, once the dialogue exists.
  Two traps come with it. **An expect string can be satisfied by the shell's own echo**:
  `ps -ax | grep update` puts the word `update` into the console stream before `grep` runs, so with
  no daemon at all the test still passes — type the pattern one letter short, or use a marker the
  typing cannot produce. And **check a new stage by breaking what it asserts and watching `multi`
  fail**, after `make test`, since a plain `make` does not rebuild `root.img`. Note also that the
  guest clock is *calibrated* to the host's rather than fast, so waiting on a five-minute crontab
  boundary costs five real minutes.

  **A test with a boot of its own costs two minutes and a volume number**; **3102 is the highest
  used, 3103 is the next free.** A graft goes in with `b6fsutil -a` at a path distinct from the
  program under test (`/etc/mkfstest`, not `/etc/mkfs`), and a second Consul needs a fixed TCP port
  — as much of the `RESOURCE_LOCK`'s reason as the CPU is.

Where a program can run in both worlds, **do both** — the first time the libc suite ran in both it
found two bugs nothing else had exercised.

**An oracle takes one of four shapes; ask which the program admits before writing a case**: a
**designed fixture** a reviewer can check by hand (`sort`); a **second implementation** where
nobody can (`od`, `pr`); the **host's own program** replayed over the whole suite as a cheap third
opinion; or an **invariant**, where the answer is not unique at all — `diff`'s `-e` script, applied
with the host's `ed`, must produce the second file.

### 10. The manual page comes with the source

Each command ships its page in its own directory, named `<name>.<section>.umm` — `ls.1.umm`,
`fsck.1m.umm`, `init.8.umm`. Follow the [../lib/libc/man/](../lib/libc/man/) precedent: **correct it
in place** — ANSI SYNOPSIS, every wrong claim fixed where it stands and marked `Note:`, 1024-byte
block counts per §4, `DIRSIZ` 18 where it shows. Rewrite rather than correct only when the
DESCRIPTION itself stopped being true. A `README.md` is worth writing only when the port *taught*
something structural.

**The format is [../doc/Manual_Page_Format.md](../doc/Manual_Page_Format.md)**, a small semantic
dialect of Markdown that replaced the roff `-man` macros; the source is UTF-8. `b6man2umm -l`
checks a page against its canonical shape and runs as a `ctest` (`man_lint_<page>`) over every page
in the tree, so a page must pass it.

**The pages are on the image, [man/](man/) finds them and [manview/](manview/) formats them.** All
205 are staged as `/usr/man/man<N>/<name>.<section>` in v7's layout: the `.umm`
suffix dropped, the section digit picking the directory and the subsection letter left on the file,
so `fsck.1m.umm` is `/usr/man/man1/fsck.1m`. That layout is `man`'s whole search rule, so **the name
of a page decides where it can be found**. They are staged as **sources** and not preformatted,
which is what the dialect is for: `man - ls` shows the file itself, and `man ls` runs it through
`manview` and pipes that into `more`.

**A new page costs two edits, not none.** `B6_STAGE_MAN` in the top-level
[../CMakeLists.txt](../CMakeLists.txt) globs `cmd/*/*.umm` at *configure* time, so a new page needs
a re-configure to be staged and a stanza in [../scripts/root.manifest](../scripts/root.manifest) to reach the disk.
Nothing fails if you skip them; the page simply is not there.

**A v7 program you port arrives with a roff page.** [man2umm/README.md](man2umm/README.md) is the
procedure: convert it with `b6man2umm`, check it against the host's `groff` with
[../scripts/mancheck.py](../scripts/mancheck.py), read what the converter could not do, and only
then delete the roff. The one roff file left in this tree is `file/test/page.1`, a fixture for
`file(1)`'s roff detector.

### 11. Everything carries eight bits, and a `char` is unsigned

The console path, the clists, the filesystem and `/bin/sh` are byte-transparent
([../kernel/dev/sc.c](../kernel/dev/sc.c)), so a Cyrillic string survives being typed, written,
stored, globbed and passed as an argument. What is left is a rule about **your** program: a byte
above `0177` is a value in `128..255`, not a negative number — but v7 masks with `0177`, tests
`c > 0` for "not EOF", or indexes a 128-entry table with a character. All three are silent on ASCII
and wrong on the first Cyrillic byte.

* **A table indexed by a character needs 256 entries *and* an index that lands in them** — and its
  width may be written down nowhere, only as a loop condition (`!(c & 0200)`) and a pointer bump
  (`ep + 0200`), so **where a table's size is not written down, read the routine that allocates
  it.** Six are on the record. `sort`'s is reached through a `+128` bias, so a grep for the size
  finds nothing; `expr`'s `CCL` writes its width into eight places; `file`'s `english()` is a v7
  wild write that this machine's unsigned `char` repairs by itself and that must not be "fixed"
  back; and **`m4`'s `type[]` is the one to check for first**, because its index is not a `char` at
  all but `getc(3)`'s result — masked with `0377` above and `EOF` below, so 128 entries were short
  at one end and negative at the other. **Ask what a table is indexed *by* before asking how wide
  it is.** Neither failure looked like a misclassification: `expr`'s undersized class came out
  **empty** and matched nothing, and `m4`'s out-of-range read pushed a literal `0377` into its own
  input. A case that pins the width has to be a positive one, with a negative control beside it
  ([expr/README.md](expr/README.md), [m4/README.md](m4/README.md)).
* **A `<ctype.h>` call is the quiet form of the same table**: `lib/libc/gen/ctype_.c` is **129
  entries** and only `isascii()` may be applied above `0177`. Ask what the option *means* for a
  multi-byte letter, not merely whether the call is in bounds.
* **A program that steals bit `0200` for a flag of its own** is the worst form — `/bin/sh`'s quote
  mark, `col`'s Greek half-shift, `grep`'s `CCL` bitmap, `sed`'s twice. The fixes were, variously,
  moving the mark, replacing it with a `QESC` prefix, and deleting the feature; which is right
  depends on what still feeds the mechanism.
* **A masked byte does not have to vanish to be wrong**: `c &= 0177` turns `привет` into
  `P?QP8P2P5Q` — junk that looks like output. The assertion has to be a case with a known answer,
  not an eyeball.

Two more before writing a test: the shell's pattern language matches **bytes**, so `?` is one byte
and not one letter; and the terminal driver refuses a typed `0377`, the raw queue's delimiter — a
script read from disk may contain one, a console user cannot type one.

### 12. On-disk layout is asserted, not re-derived

Anything that encodes the **on-disk layout** — a block number computed from an i-number, an entry
count per block, a name length — **`_Static_assert`s against
[../include/sys/param.h](../include/sys/param.h) rather than re-deriving the constants**, so a
kernel that retunes `INOPB` or `DIRSIZ` breaks the build instead of the images. A guest program
gets this for free by including the real headers; [fsutil/params.cpp](fsutil/params.cpp) is
elaborate only because `fsutil` is host C++ and cannot. The rule applies to test scripts too, which
is why `b6fsutil -D`'s damage targets are **symbolic**.

**The better answer, where there is one, is not to encode the layout at all** —
[`lib/libc/gen/dirdesc.h`](../lib/libc/gen/dirdesc.h) makes that assertion once for every
`opendir(3)` caller. So the first question is whether this program is one of the five `/dev/rmd*`
readers in §5. The devices they are pointed at are all on the image: `/dev/rmd0`, `/dev/rmd1` and
`/dev/rmb0` (`cdevsw[3]` and `[4]`), and the block nodes `/dev/md0`, `/dev/md1` and `/dev/md2`.

---

## Not ported, and why

**Every row is a decision that can be re-examined, not a closed door** — the first of them was
opened and `mail` ported. The line counts are there so a row can be re-costed; **the sentences are
what want re-reading**, because a reason expires when the machine moves under it and nothing
notices on its own.

| | lines | why not |
|---|---|---|
| `xsend/` | 414 | Secret mail. **The source is not in this tree**, and what it adds to `mail` is a public-key scheme whose `enroll`/`xget` half is equally absent — new work rather than a port. [`crypt/`](crypt/) carries this image's encryption and `mail` its delivery. |
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` drives a CAT phototypesetter that does not exist, and there was never an `nroff` in this source tree. **There is nothing left here for either to typeset**: the manual pages are in the dialect [../doc/Manual_Page_Format.md](../doc/Manual_Page_Format.md) describes. `spell` also needs its whole word list. |
| `tp/`, `dump.c`, `restor.c`, `dumpdir.c` | 800 + 641 + 1,150 + 475 | Tape. **This kernel has no tape driver** and no `bdevsw`/`cdevsw` row for one, and all four are built around a tape's sequential access rather than merely willing to use it. `tp` is superseded by `tar` in any case. If a driver is ever written ([../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md)), reconsider `dump`/`restor` and not the other two. |
| `uucp/`, `cu.c` | 6,415 + 541 | Dial-out over a modem link nothing models. `cu` is conceivable only if the serial multiplexor is ever driven and wired to something outside. |
| `lpr/`, `vpr.c` | 1,315 + 334 | Printer spooling. **Worth revisiting:** SIMH *does* model the АЦПУ drum printer, so `lpr` is a small task the day a kernel printer driver exists. |
| `graph.c`, `plot/`, `spline.c`, `tc.c`, `tk.c` | 695 + 608 + 335 + 638 + 250 | Plotters and Tektronix terminals; no hardware, and the output would go nowhere. |
| `learn/` | 1,066 | Needs the entire `/usr/lib/learn` lesson corpus, which is not in this tree. |
| `adb/` | 3,547 | PDP-11 instruction decoding, core files and `ptrace` semantics. A BESM-6 debugger is **new work**, not a port; [disasm/](disasm/) plus `ptrace(2)` is where it would start, and `ptrace`'s single-step is **refused with `EIO`** — the breakpoint contract to settle first is in [../doc/Besm6_Kernel_Reference.md](../doc/Besm6_Kernel_Reference.md). |
| `lint/`, `mip/`, `struct/`, `ratfor/` | 1,164 + 7,615 + 4,721 + 1,200 | `lint` and `mip` are the PDP-11 C compiler's own internals; `struct`/`ratfor` are Fortran tooling with no Fortran here — which is also why lex's `nrform` was dropped. |
| `osh.c` | 846 | The pre-Bourne shell. [sh/](sh/) supersedes it. |
| `mknod.c` | 44 | **There is no `mknod(2)` in this kernel.** Every device node is made by `b6fsutil` from [../scripts/root.manifest](../scripts/root.manifest), which is where a new one is added. |
| `prof.c` | 310 | Reads a `mon.out` that nothing produces: the kernel decided against profiling, `profil(2)` **refuses** with `EINVAL`, there is no `monitor`/`mcount` in libc and `cc` has no `-p`. `b6sim` profiles a program today with no kernel help. |
| `cb.c`, `diff3.c`, `tabs.c` | 366 + 423 + 196 | Curiosities with a real cost and no caller. `cb` is superseded by clang-format; `diff3` wants a merge nobody does here; `tabs` sets hardware tab stops on terminals this machine does not have. |
| `cc.c`, `as/`, `ld.c`, `nm.c`, `ar.c`, `size.c`, `strip.c`, `ranlib.c`, `arcv.c` | | PDP-11 `a.out`, opcodes and registers; nothing survives retargeting. The BESM-6 tools were written for this repo instead, and twelve of them are built a second time for the machine itself — see each tool's "Building for the BESM-6". |
| `ac.c`, `sa.c`, `accton.c` | 251 + 489 + 16 | Process accounting. The kernel side EXISTS and works — `acct(2)` is a real gate ([../kernel/acct.c](../kernel/acct.c)) — which makes this a decision rather than a gap. Nothing needs it: one operator, nobody to bill. It would also want a `/usr/adm` the manifest does not have and an `accton` line in [../etc/rc](../etc/rc). |
| `random.c`, `sp.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |

---

## Known gaps, for whoever trips over them

* **[../etc/rc](../etc/rc) still wants two lines**, and neither waits on a program: a boot-time
  `fsck` and the `rm -f /tmp/*`. Both wait on an assertion, and `kernel/test/multi` already runs
  the script and asserts three of its lines, so each is one more stage. Read §9 on the echo trap
  first, and note that an `fsck` which repairs the mounted root does not return.
* **A permanent child changes init's arithmetic.** With `/etc/update` running, `multiple()`'s
  `wait() == -1` never comes back, so init's `allgone` return to single-user and `merge()`'s two
  `/etc/ttys` diagnostics are set and never said. That is v7's own design and is written down in
  `multiple()` rather than fixed.
* **Three terminal bits go nowhere.** `TANDEM` is honoured by the kernel but **no program on this
  image can set it** ([stty/README.md](stty/README.md)'s cut being deliberately subtractive), and
  `ttioccomm()` accepts `TIOCEXCL`/`TIOCNXCL`/`TIOCHPCL` and sets `XCLUDE` or `HUPCLS` where
  nothing in the kernel ever tests either bit. Worth knowing before somebody reports them as bugs.
