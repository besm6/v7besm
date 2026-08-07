# Porting the v7 userland to the BESM-6

`cmd/` holds two kinds of subdirectory:

* **host tools** — `cc`, `as`, `ld`, `cpp`, `disasm`, the binutils, `sim`, `fsutil` — built by the
  build machine's own compiler. They have their own `README.md`s and their own chapters in
  [../doc/](../doc/); nothing below is about them.
* **native BESM-6 programs** — the v7 commands that go on the disk image, built by the `b6*`
  toolchain and staged into `build/rootfs/`.

**This file is the manual for the second kind.** [TODO.md](TODO.md) beside it is the work plan.
Read [sh/README.md](sh/README.md) (the largest port there has been) and [ls/README.md](ls/README.md)
first; everything below is written on top of both.

## The sources are already here

**Every program named by [TODO.md](TODO.md) is in this directory**, one directory per program —
the source, its auxiliary files and its manual page. Everything not yet ported is a **verbatim
upstream copy**, so the first diff on it is the porting diff. A task starts by writing a
`CMakeLists.txt`, not by fetching anything.

They came from `tmp/v7x86-0.8a/usr/src/cmd/`, an unpacked reference tree that is **not in the
repository** (`tmp/` is git-ignored). **Three directories are the exception.** Two have no v7
original behind them at all: [novi/](novi/), Dave W Plummer's full-screen editor for 2.11BSD, and
[more/](more/), Berkeley's pager by way of RetroBSD — v7 had neither an editor of that kind nor a
pager. They are the two tasks in [TODO.md](TODO.md) with a number and no table row. The third is
[man/](man/), and it is a different case: v7 *had* a `man`, but it is a shell script around
`nroff` and there is no `nroff` here, so the source is Berkeley's 1987 C rewrite by way of
RetroBSD and the reference tree's copy was not used.

**A directory is part of the build when it holds a `CMakeLists.txt`** — that is the only marker;
[../CMakeLists.txt](../CMakeLists.txt) names its subdirectories one by one.

Notes on the copies:

* The v7 `makefile` came along for each multi-file program as the record of its source list and
  flags. It is a PDP-11 recipe, kept for reading; nothing runs it.
* **Ten programs have no manual page of their own.** Eight are documented inside another program's
  page — `rmdir` in [rm/rm.1.umm](rm/rm.1.umm), `chgrp` in `chown.1`, `umount` in `mount.1m`,
  `fgrep`/`egrep` in `grep.1`, `diffh` in `diff.1`, `atrun` in `at.1`, `accton` in `sa.1m`.
  **`yes` and `dmesg` have no page anywhere in v7** and need one written.
* **The file-format pages are in [../include/man/](../include/man/)**, not here — they document the
  headers rather than a program. Five of them are nothing but `.so /usr/include/…`, and there is no
  `/usr/include` here: write the structure out, as [../lib/libc/man/](../lib/libc/man/) did, with
  this machine's `DIRSIZ` 18 and one-word `off_t`/`time_t`.
* **Two data files** came with their programs: [units/units](units/units) and
  [cron/crontab](cron/crontab). `calendar`'s database is missing — what the reference tree holds
  under that name is an x86 binary.
* The `.y`, `.l` and header-ish files carry **no v7 copyright banner**, unlike the `.c` sources.
  The top-level `COPYRIGHT` covers them; do not add one.

---

## The porting recipe

Twelve things that are true of **every** port, collected here so that no task in
[TODO.md](TODO.md) has to repeat them.

### 1. The C11 pass, which is mechanical

`b6parse` is strict C11: no implicit `int`, no K&R parameter lists, no untyped `register i;`, no
`char *malloc();` re-declarations. Prototypes and explicit return types everywhere, `static` on
file-scope objects. [init/README.md](init/README.md) is the small worked example,
[sh/README.md](sh/README.md) the large one.

Watch for **names C11 reserved that v7 used freely** — `chmod.c` defined `abs()`, `chown.c` defined
`isnumber()`. Neither fails to link, so the collision waits silently for whatever wants the real
one. Rename on sight.

**A multi-file v7 program almost certainly defines its globals in a header** (`sh/defs.h`,
`sed/sed.h`): `char genbuf[LBSIZE];` in a file every source includes, with the PDP-11 linker
merging the copies. **C11 has no tentative definition across translation units and `b6ld` has no
common symbols**, so left alone each source gets its own storage, with no diagnostic from anything.
`extern` in the header, defined once in a file of its own (`sh/glob.c`, `sed/sedglob.c`). One line
of `b6nm` over the finished binary is the check.

**A header of the program's own is a build blind spot.** `b6_obj`'s header dependency is the
*system* header tree, so editing `sed/sed.h` or `sh/defs.h` rebuilds nothing. Touch a `.c` after
changing one.

### 2. An `int` is not a `char *`

A `char *` here is a **fat pointer** — byte offset in bits 47–45, word address in bits 15–1 — and
the offset *decrements* as the pointer advances, so the raw word does not sort. The compiler deals
with it: a relational between two byte pointers lowers through **`b$pdiff`** and orders them
correctly; `==`/`!=` are raw word compares and are right too, the encoding being canonical. A `<`
between two `char *` in a v7 source is not a bug to hunt. It is still two out-of-line calls where
an `int` index is a register test, so rewrite one that runs once per byte.

Live hazards the compiler does not cover:

* **A fabricated pointer matches nothing** — `(char *)-1` (v7's `sbrk` failure test) can never
  equal a real fat pointer.
* **A relational between two `void *` is a hard error** (C11 6.5.8 wants complete object types).
  gcc and clang take it as an extension; cast to `char *`.
* **A pointer formed below the base of its array** is UB and the guard need not fire on a
  word-address machine (`comm.c`'s `lb1 - 1`, `diff.c`'s shellsort underflow). Use an index pair.
* **Three arena hazards**, in anything that manages its own storage: a flag packed into bit 0 of a
  pointer, a mask that rounds to a word assuming `BYTESPERWORD`, and a **cast to a thin pointer,
  which FLOORS a fat one to its word** — `find`'s parse tree stored a `char *` pattern through a
  struct pointer and lost the byte offset. Grep for them; do not merely expect them because a
  program calls `sbrk`.

The shape worth keeping: **a v7 source grows byte cursors when it parses, not when it reads
bytes.** `sort`, `grep`, `sed` and `pr` hold a cursor inside a buffer they are deciding about;
plain filters hold `int` indices already.

### 3. A `long` is one word, and `%D` is not a conversion

`long` is `int` is one 41-bit word.

* `%ld` is harmless — `l`, `h`, `L`, `j`, `z`, `t` are parsed and ignored — but means nothing;
  write `%d`.
* **`%D` prints the two characters `%D`.** [../lib/libc/stdio/doprnt.c](../lib/libc/stdio/doprnt.c)
  does not know that PDP-11 conversion, and an unknown conversion is echoed verbatim **and consumes
  no argument**, desynchronising every later conversion in the same format string. Grep each new
  source for `%D` and `%O`; it costs a second.

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
— three — of them per block, so the output is in **1024-byte blocks**. Three rules come with it:

* **The multiply goes at the `printf` and nowhere else**, so a variable called `blocks` holds
  blocks. `quot -c`'s histogram is *indexed* by a block count and forces this.
* **Assert the unit divides**: `_Static_assert(BSIZE % KBYTE == 0, …)`.
* **Say it in the manual page**, in a `BLOCKS ARE 1024 BYTES` section.

**A block that is the program's own business is converted to neither** — `ed`'s temp file, `tar`'s
record, `tail -b`, `dd`'s `bs=`. **But a default is not the user's**: `dd`'s `ibs`/`obs` and `b`
suffix are `BSIZE`, not v7's 512, because 512 is not a whole number of words and `physio()` refuses
it. The rule: **a constant is the user's business only while it still names something on this
machine.** And **a size that names a *device* stays in filesystem blocks** — `mkfs special nblocks`
takes `s_fsize` verbatim, and its page owes the mirror section, `BLOCKS HERE ARE 3072 BYTES`.

### 5. `DIRSIZ` is 18

`struct direct` is four words — a full-word `d_ino` and three words of name. So `%.14s` becomes
`printf("%.*s", DIRSIZ, …)`, `char name[15]` becomes `char name[DIRSIZ + 1]`, and a name read out of
a directory **is not NUL-terminated** unless the port terminates it. Anything that walks
directories inherits this.

### 6. Ceilings, of which only two are checked

* **28,672 words** of `const + text + data + bss` — 32 pages less the four the stack takes.
* **Word 32,767** — no relocatable symbol above the reach of a 15-bit pointer.
* **A struct may not exceed 4,096 words.** A member is named by a 12-bit offset from a base
  register and there is no longer form, so `b6as` refuses the offset (`short address out of
  range`) and nothing downstream can rescue it. This is the architecture, not a compiler defect.
  It bites **every v7 program that keeps its state in one big struct**, it is invisible until the
  assembler speaks, and the answer is the same every time: **move the big arrays out of the struct
  to file scope**, where an index register reaches them at any size. [cpp/README.md](cpp/README.md)
  is the worked example — `struct cppstate` was ~38,630 words and is ~400 with its four arrays
  lifted out.
* **4,096 words of stack** at `070000`. **Nothing checks this.** One big automatic array blows it
  in silence, and so does a modest one multiplied by a recursion. Read the prologue (`15 utm 0NNN`
  in the `.dis` `b6_prog()` writes) rather than estimating the frame, and bound every fixed path
  buffer v7 filled with an unbounded `sprintf`/`strcpy` from `argv`. **Scalars are not free**:
  every distinct compiler temporary is a permanent frame slot, so a long function costs 1.5–2
  words per source line with no arrays at all — `sed`'s `fcomp` is 700. The three moves that pay
  are (a) big scratch to the heap when the function recurses, (b) `static` when it does not, and
  (c) splitting a long function that stays resident: `cpp`'s `main` went 531 → 41 words that way.
  **And a recursion whose depth the input chooses needs a ceiling of its own** — `grep`'s
  `MAXDEPTH`, `cpp`'s `MAXARGDEPTH`.
* **The heap**, which is not checkable: `rootfs_<name>_size` cannot see a byte of allocated
  storage. `col`'s worst case is past the whole address space; `sort` takes every page the break
  will give. **Ask what a program will still need to allocate after it has taken what it wanted** —
  a stream whose `malloc(BUFSIZ)` fails does not fail, it becomes one `read(2)` per byte. And
  **measure the break, not the request**: `malloc` grows the arena a 1,024-word page at a time
  (`lib/libc/gen/malloc.c`), so a 1,196-word block costs two pages while an 854-word one costs
  one. `cpp` chose `EXPTXT_MULT` on that alone.
* **A bound the user chooses at run time**, checkable only by the program itself: `pr`'s
  look-ahead ring capacity is a function of `-l` and the column count.

`b6_prog()` registers `check-size.sh` for the first two as ctest `rootfs_<name>_size`. For scale:
`ld` is 23,951 words and `cpp` 23,826, the two largest things on the image; `fgrep` is 20,019,
and 15,000 of them are two arrays; `as` is 19,824, `sed` 14,120, `fsck` 10,842, `sh` 7,928,
`sort` 6,822. **The three toolchain programs are where the ceilings really bind** — each carries
a `besm6` size profile, and `cpp`'s is below what C11 asks for
([cpp/README.md](cpp/README.md), [as/README.md](as/README.md), [ld/README.md](ld/README.md)). **What a program prints with dominates what it
does** — everything that links stdio carries ~1,030 words of bss and ~2,500 of common text, while
`test`, `tee`, `tail` and `getty` write with `write(2)` and cost a fraction of it (`getty` 434
words, an eleventh of `cat`). Measure a large candidate early. And **measure a struct rather than
deriving it**: `char` members pack six to a word inside a struct exactly as in an array; only the
struct's overall size rounds up.

Where a fixed buffer can be sized so that nothing has to test it, do that instead of adding a test:
**a bound test that is not on every path reads exactly like one** ([grep/README.md](grep/README.md)).

### 7. How a program gets onto the image — five steps

1. `cmd/<x>/CMakeLists.txt`: one `b6_prog(<x> DEST bin/<x> SOURCES <x>.c)` call
   ([../scripts/BesmCross.cmake](../scripts/BesmCross.cmake)). `PURE` only for something that runs
   in several processes at once; it costs a page-aligned data segment.
2. `add_subdirectory(cmd/<x>)` in [../CMakeLists.txt](../CMakeLists.txt), **inside the
   `libruntime.a` guard and after `add_subdirectory(lib)`**.
3. A stanza in [../root.manifest](../root.manifest): `mode`, `file /bin/<x>`, `source
   ../../rootfs/bin/<x>`. Paths resolve against `b6fsutil`'s working directory
   (`build/kernel/test`), not against the manifest.
4. A line in [../etc/rc](../etc/rc) if the boot script wants it — `/etc/rc` runs with **no
   terminal**, so anything meant to be seen redirects to `/dev/console` for itself. Nothing
   asserts `/etc/rc` any more: `kernel/test/console` was its one home and is deleted.
5. The test, per §9.

One list must grow with the program and nothing catches it but a failing build: `ROOTFS_FILES`
in [../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt). The hard-coded `ls /bin`
expectations that used to catch it as well went with `kernel/test/console` and `session`.

The disk is one EC-5052: **2000 blocks, 6,144,000 bytes**, and since the manual went on it (§10)
and `man(1)` and `manview(1)` after it there are **187 free** — the whole of `/usr/man` is 302 of
them, `man` 12 and `manview` 17. That is still room for a good deal of what [TODO.md](TODO.md) has
open, but it is no longer room for anything: weigh a large addition against it rather than
assuming. The number is printed by `b6fsutil` every time `root.img` is built, so it is measured
and not estimated.

### 8. Setuid works, and it is asserted

The kernel honours `ISUID` in `getxfile()` ([../kernel/sys1.c](../kernel/sys1.c)) and `b6fsutil`
carries the bit through, so a manifest `mode 04755` is the whole of the work. `mkdir`, `rmdir` and
`mv` borrow root for one `suser()`-gated syscall (this system has no `mkdir(2)`, `rmdir(2)` or
`rename(2)`); `passwd`, `su` and `newgrp` change **who the caller is**, and `su` cannot give it back
— `setuid(2)` moves the real id and there is no saved id.

`getxfile()` takes the ISUID branch only `if (u.u_uid != 0)`, so **a test must reach it
deliberately**: [../lib/test/suidt.c](../lib/test/suidt.c) drops to uid 7 itself and execs
`/bin/mkdir`. A bit asserted through a login dialogue is asserted through `getty`, `login`, `crypt`
and `/etc/passwd` as well, and any of them failing looks the same.

**Most programs do not want it.** `chmod(2)` is gated on `owner()` and `chown(2)` on `suser()` —
and that second gate is what stops a user giving a file away, so a setuid `chown` would defeat its
own purpose. Ask what call actually needs privilege before reaching for `04755`.

**And when a program *does* need privilege, ask what it is actually reading.** `ps` and `df` were
both root-only, and both said in capitals that they must never become setuid — rightly: a setuid
`ps` hands out every process's memory through a program that already knows the layout, and a
setuid `df` hands out the filesystem to anyone able to think of an offset. But neither wanted the
*device*. `ps` wanted four fields of a u-area and `df` four counts out of a superblock, so the
kernel was made to answer instead — `KCTL_PSINFO` on the existing `kctl(2)`, and `statfs(2)` — and
both are now ordinary user commands at `mode 0755`, with **no bit set and no device mode
loosened**. `/dev/kmem`, `/dev/mem` and `/dev/rmd0` are exactly as they were, which is the *first*
thing [../lib/test/unprivt.c](../lib/test/unprivt.c) asserts: without that negative control, "`ps`
printed a table" would read the same whether the kernel had grown an interface or somebody had
quietly widened a node.

The corollary is worth stating because it is the cheaper half: **an operation on a call that
already exists costs no system-call number.** `KCTL_PSINFO` is one `#define`, one struct in
`<sys/kctl.h>` and one arm in [../kernel/kctl.c](../kernel/kctl.c) — no libc stub, no `b6sim`
arity entry, and the existing `lib/test/kctlt` conformance test extended to cover it.
[../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md) §6 says where the line between
the two shapes falls.

### 9. Which world a test runs in

* **`b6sim`** runs one BESM-6 `a.out` and services its syscalls on the *host*. Good for filters.
  **`b6_progtest(<prog> <case>)` is the harness**: files `<case>.args` / `<case>.expected` /
  optional `<case>.in` / `<case>.status` in `cmd/<x>/test/`, ctest `cmd_<x>_<case>` under label
  **`cmd`**. It runs the program **as staged for the image**. [sh/test/](sh/test/) is the other
  shape, a whole shell script per case.

  It can be handed a whole filesystem — a flat `b6fsutil` image is a file, so `df`, `quot`, `mkfs`
  and `fsck` are tested against real fixtures, `mkfs` byte-for-byte against `b6fsutil -n` and `fsck`
  against `b6fsutil -c` after `b6fsutil -D` damage. **A test that repairs must assert that there was
  something to repair.**

  What it cannot do: read a **directory** (so `ls`, `du`, `find` have no case — and note that
  `opendir(3)` does **not** rescue this: on the host `open(2)` and `fstat(2)` on a directory both
  *succeed* and only `read(2)` refuses, so a `b6sim` walk returns no entries rather than an error,
  and a case would pass while proving nothing); exec `/bin/…`; set
  global state (`stime(2)` is a no-op reporting success); name a pid (`kill(2)` is the host's). Its
  `argv[0]` is the staged absolute path, so a program that dispatches on or prints its own name
  belongs under the kernel. Its `.args` line is split on whitespace with no quoting (globbing is off
  — `set -f`). A program that does not terminate cannot be a case (`yes`). Standard output is
  captured as **`<case>.out` in the working directory**, so no fixture may be named that.

  **The general question: when a program names a fixed absolute path or a global system state, ask
  whose it is under `b6sim`.** `mount(2)`/`umount(2)` would graft a filesystem onto the build
  machine. Two classes are exempt, both answered by b6sim itself
  ([../doc/Aout_Simulator.md](../doc/Aout_Simulator.md) §7): `kctl(2)` and the memory devices, from
  an imitation kernel — so `ps`/`pstat` can be checked here for formatting, but not for plurality,
  `proc[]` holding one live entry — and the six static `/etc` files, from
  [sim/etcfiles.cpp](sim/etcfiles.cpp). `getpwent(3)` opens the literal `/etc/passwd`, which used to
  be the *build machine's* and made `passwd` a hazard rather than a limit; it is the target's now,
  the same bytes the image carries, so `quot`'s per-uid report and `fsck`'s `OWNER=` are ordinary
  assertions. `passwd(1)` is left a clean **limit**: it `creat`s that file and gets `EROFS`.

  **Ask which side of those limits a program falls on before designing its cases.** A program can
  land wholly outside: `mount`/`umount` and `find` have no `b6sim` half at all and so no `test/`
  directory of their own. More often it is a **branch** rather than a program — `tar`'s `c` path
  walks a tree while `t`, `x` and `r` do not; `ps` reads a u-area at a `p_addr` that is not the
  caller's, which one process can never produce.

* **SIMH**, under the booted kernel — for everything the above cannot say. Join the existing test
  in `kernel/test/` whose subject matches rather than taking a new volume: `console` (a typed
  dialogue), `libtest` (a program off the test pack diffed against a `.expected`), `session`
  (anything that writes and is fscked on the host afterwards), `files` (a tree or an inode),
  `utils` (clock, signal, process, pipe), `login` (terminal modes, or a process that is not
  root's), `edit` (authoring a file, and here-documents), `fsinfo` (reading a device, or reporting
  on a filesystem), `dd` (bulk data through a device), `mkfs` (writing a device, or a second
  drive), `fsck` (repairing a device), `mount` (the buffer cache), `filters` (anything whose
  subject is bytes — the only place an argument can be **quoted** and the only place a temp file
  lands in the image's own `/tmp`), `accounts` (`/etc/passwd` as a file that changes), `tar` (a
  whole tree, and a pack that is written and never read), `inspect` (a second process, and one of
  them asleep).

  **Joining one of those costs a section in its `.sh`; a boot of its own costs two minutes and a
  volume number**, so take one only for something they cannot show — `tar` needed a second drive,
  `inspect` a plurality of processes, `toolchain` a `cc` whose search path is the *image's*
  `/usr/bin`, `/lib` and `/usr/include` rather than the build machine's. Each has its own copy of
  the image at its own volume number; **3102 is the highest used, 3103 is the next free.** Most graft their script with `b6fsutil -a`, at a path distinct from the program
  under test (`/etc/mkfstest`, not `/etc/mkfs`). `login`, `multi` and `accounts` type every
  character instead, and each needs a fixed TCP port for Consul 2 (4199, 4200, 4201) — as much of
  the `RESOURCE_LOCK`'s reason as the CPU is. An oracle that is a property of the whole image
  should be **recomputed, not checked in**.

Where a program can run in both worlds, **do both** — the first time the libc suite ran in both it
found two bugs nothing else had exercised.

**An oracle takes one of four shapes; ask which the program admits before writing a case.** A
**designed fixture** where a reviewer can check the answer by hand (`sort`); a **second
implementation** where nobody can (`od`, `pr`); the **host's own program** replayed over the whole
suite as a cheap third opinion (it found `join` and `diff` agreeing byte for byte with BSD's on
everything the two dialects share); and an **invariant** where the answer is not unique at all —
`diff`'s `-e` script, applied with the host's `ed`, must produce the second file.

### 10. The manual page comes with the source

Each command ships its page in its own directory, named `<name>.<section>.umm` — `ls.1.umm`,
`fsck.1m.umm`, `init.8.umm`. Follow the [../lib/libc/man/](../lib/libc/man/) precedent: **correct it
in place** — ANSI SYNOPSIS, every wrong claim fixed where it stands and marked `Note:`, 1024-byte
block counts per §4, `DIRSIZ` 18 where it shows. Rewrite a page rather than correct it only when the
DESCRIPTION itself stopped being true. A `README.md` is worth writing only when the port *taught*
something structural.

**The format is [../doc/Manual_Page_Format.md](../doc/Manual_Page_Format.md)**, a small semantic
dialect of Markdown, and it replaced the roff `-man` macros the pages were inherited in. The source
is UTF-8 and nothing special is needed for it; §11 already covers what eight-bit text means here.
`b6man2umm -l` checks a page against the format's canonical shape and runs as a `ctest`
(`man_lint_<page>`) over every page in the tree, so a page must pass it.

**The pages are on the image, [man/](man/) finds them and [manview/](manview/) formats them.** All
two hundred and one are staged as `/usr/man/man<N>/<name>.<section>` in v7's layout: the `.umm`
suffix dropped, the section digit picking the directory and the subsection letter left on the file,
so `ls.1.umm` is `/usr/man/man1/ls.1` and `fsck.1m.umm` is `/usr/man/man1/fsck.1m`. That layout is
`man`'s whole search rule, so **the name of a page decides where it can be found**. They are staged
as **sources** and not preformatted, which is what the dialect is for: `man - ls` shows the file
itself, and `man ls` runs it through `manview` and pipes that into `more` for a terminal.

**A new page costs two edits, not none.** `B6_STAGE_MAN` in the top-level
[../CMakeLists.txt](../CMakeLists.txt) globs `cmd/*/*.umm`, so a page in a new command's directory
is already inside it — but that glob runs at *configure* time, so the page needs a re-configure to
be staged and a stanza in [../root.manifest](../root.manifest) to reach the disk. Neither is
automatic and nothing fails if you skip them; the page simply is not there.

**A v7 program you port arrives with a roff page.** [man2umm/README.md](man2umm/README.md) is the
procedure: convert it with `b6man2umm`, check it against the host's `groff` with
[../scripts/mancheck.py](../scripts/mancheck.py), read what the converter could not do, and only then
delete the roff. The one roff file left in this tree is `file/test/page.1`, a three-line fixture for
`file(1)`'s roff detector.

### 11. Everything carries eight bits, and a `char` is unsigned

The console path, the clists, the filesystem and `/bin/sh` are byte-transparent
([../kernel/dev/sc.c](../kernel/dev/sc.c)), so a Cyrillic string survives being typed, written,
stored, globbed and passed as an argument. What is left is a rule about **your** program, and v7
sources break it: a byte above `0177` is a value in `128..255`, not a negative number — but v7 masks
with `0177`, tests `c > 0` for "not EOF", or indexes a 128-entry table with a character. All three
are silent on ASCII and wrong on the first Cyrillic byte.

* **A table indexed by a character needs 256 entries *and* an index that lands in them** — and its
  width may be written down nowhere, as a loop condition (`!(c & 0200)`) and a pointer bump
  (`ep + 0200`). **Where a table's size is not written down, read the routine that allocates it.**
  Four shapes are on the record: `grep`'s `CCL`, right-sized and stored into unmasked; `sort`'s,
  256 entries reached through a `+128` bias so a grep for the size finds nothing; `sed`'s `y///`
  table, whose width is a loop condition and a pointer bump and a number nowhere at all; and
  `file`'s `english()`, a v7 wild write that this machine's unsigned `char` repairs by itself and
  that must not be "fixed" back.
* **A `<ctype.h>` call is the quiet form of the same table**: `lib/libc/gen/ctype_.c` is **129
  entries** and only `isascii()` may be applied above `0177`. Ask what the option *means* for a
  multi-byte letter, not merely whether the call is in bounds.
* **A program that steals bit `0200` for a flag of its own** is the worst form — `/bin/sh`'s quote
  mark, `col`'s Greek half-shift, `grep`'s `CCL` bitmap, `sed`'s twice. The fixes were, variously,
  moving the mark, replacing it with a `QESC` prefix, and deleting the feature. Which is right
  depends on what still feeds the mechanism.
* **A masked byte does not have to vanish to be wrong**: `c &= 0177` turns `привет` into
  `P?QP8P2P5Q` — junk that looks like output. The assertion has to be a case with a known answer,
  not an eyeball.

Two more, worth knowing before writing a test: the shell's pattern language matches **bytes**, so
`?` is one byte and not one letter; and the terminal driver refuses a typed `0377`
([../kernel/dev/tty.c](../kernel/dev/tty.c)), the raw queue's delimiter — a script read from disk
may contain one, a console user cannot type one.

### 12. On-disk layout is asserted, not re-derived

Anything that encodes the **on-disk layout** — a block number computed from an i-number, an entry
count per block, a name length — **`_Static_assert`s against
[../include/sys/param.h](../include/sys/param.h) rather than re-deriving the constants**, so a
kernel that retunes `INOPB` or `DIRSIZ` breaks the build instead of the images. A guest program
gets this for free: it includes the real headers and inherits their assertions.
[fsutil/params.cpp](fsutil/params.cpp) is elaborate only because `fsutil` is host C++ and cannot.
The rule applies to test scripts as much as to C, which is why `b6fsutil -D`'s damage targets are
**symbolic**.

The devices these programs are pointed at are all on the image: `/dev/rmd0`, `/dev/rmd1` and
`/dev/rmb0` (`cdevsw[3]` and `[4]`), and the block nodes `/dev/md0`, `/dev/md1` and `/dev/md2`.
