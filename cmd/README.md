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

**Almost every program named by [TODO.md](TODO.md) is in this directory**, one directory per
program — the source, its auxiliary files and its manual page. Everything not yet ported is a
**verbatim upstream copy**, so the first diff on it is the porting diff. Such a task starts by
writing a `CMakeLists.txt`, not by fetching anything.

They came from `tmp/v7x86-0.8a/usr/src/cmd/`, an unpacked reference tree that is **not in the
repository** (`tmp/` is git-ignored). **Five directories are the exception.** Two have no v7
original behind them at all: [novi/](novi/), Dave W Plummer's full-screen editor for 2.11BSD, and
[more/](more/), Berkeley's pager by way of RetroBSD — v7 had neither an editor of that kind nor a
pager. They are the two tasks in [TODO.md](TODO.md) with a number and no table row. The third is
[man/](man/), and it is a different case: v7 *had* a `man`, but it is a shell script around
`nroff` and there is no `nroff` here, so the source is Berkeley's 1987 C rewrite by way of
RetroBSD and the reference tree's copy was not used.

The fourth and fifth are **[yacc/](yacc/)** and **[lex/](lex/)**, the two tasks that began by
fetching: RetroBSD's yacc is 4.2BSD's with the ANSI pass already done, which is most of §1 gone
before the work starts, and its lex is very nearly v7's — K&R and all — but carries the
`<paths.h>` fixes and a few corrections, and there is no better starting point in existence. So
C10a and C10b took both from there rather than from the reference tree. In both, the manual page
still comes from v7's `usr/man` — RetroBSD keeps its pages in a central tree rather than beside
the source.

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
* **Three programs carry a data file**: [units/units](units/units), [cron/crontab](cron/crontab)
  and [calendar/calendars/](calendar/calendars/). The first two are v7's own and are on the image
  as `/usr/lib/units` and `/usr/lib/crontab`; `calendar`'s eight files are not v7's — the
  reference tree held an x86 binary under that name — and went on with task C23, as
  `/usr/lib/calendars/*`, PLURAL because `/usr/lib/calendar` is the program.
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
between two `char *` in a v7 source is not a bug to hunt. It is still an out-of-line call more
than the comparison needs, so rewrite one that runs once per byte.

**And rewrite it as a shadow counter, not as an index.** The obvious reading — turn the cursors
into `int` subscripts — buys nothing: `b$padd` sits beside `b$pinc` and `b$pdec` in every
program here, so `buf[i]` is a call exactly as `*p++` is one and they cost the same. Only the
*comparison* is removable, so the shape that pays is an `int` kept alongside the pointer and
tested in its place. [m4/README.md](m4/README.md) is the worked example and carries the number:
`m4`'s `getchr()` was a `b$pdiff` per input character, and retiring that one relational and two
more took **21% off the instruction count** of a whole run.

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
  source for `%D` and `%O`; it costs a second, and it is still finding them — `expr`'s was the
  whole output of `expr a + b`.

**And a `double` is one word too**, with no IEEE anything behind it: `5.42e-20` to `9.22e+18`,
twelve digits, `sizeof(double) == sizeof(float) == 6`. Two consequences bind every port that
computes with real numbers. **An overflow is a machine FAULT, not a signal** — it vectors through
0500 and `kernel/trap.c` turns it into `SIGFPE`, which kills the process — so a range test goes
*before* the operation and never after it; underflow is the opposite and raises nothing at all,
quietly becoming zero. And **an intermediate may leave the range where the answer does not**:
`units`' own table has `1.6021917-19`, a representable number whose v7 scaling built `1e26` first
([units/README.md](units/README.md), [../lib/libm/README.md](../lib/libm/README.md)).
`printf` carries `%e`/`%f`/`%g` and clamps to twelve significant digits; `strtod` is declared and
not implemented, so use `atof`.

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

**A new port does not inherit it, because it uses `opendir(3)`** — `readdir()` skips the free
slots, plants the terminator and hands back `d_namlen`, and all three of those are what the eight
hand-rolled readers C24 will convert get wrong. [`make/files.c`](make/files.c)'s `srchdir()` is
the worked example: v7 `fread`s 32 raw `struct direct` at a time and copies each name through a
15-byte buffer, and the port is a `readdir()` loop with the copy gone. Do that from the start
rather than porting the hand-rolled version and adding a ninth to C24's list.
[`atrun`](atrun/) is the first port to have taken that instruction, and it is also where the
cost of not taking it shows: the same program had `DIRSIZ` written a second time as the `14` in
`sprintf("/bin/mv %.14s %s", …)`, which no directory-reading fix would have found.

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
  `MAXDEPTH`, `cpp`'s `MAXARGDEPTH`. **Sometimes no ceiling will do**: `lex`'s parse-tree walk is
  as deep as the file has rules, and `awk.lx.l` alone wants 96 frames, so any ceiling that admits
  the one scanner this machine must compile already overflows. That walk is **iterative**, with an
  explicit worklist in bss ([lex/README.md](lex/README.md), "Walking the tree") — and note how it
  failed before: not a fault, but the heap under the stack overwritten, surfacing as a garbled
  diagnostic several passes later.
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
   terminal**, so anything meant to be seen redirects to `/dev/console` for itself. The
   assertion goes in `kernel/test/multi`, which inherited the job when `kernel/test/console` was
   deleted: it is the one surviving test that types the `^D` that gets past the single-user
   shell, and it now asserts both of that script's lines — the `date` it prints and, since task
   C20, the `/etc/update` it leaves running ([update/README.md](update/README.md)).
5. The test, per §9.

One list must grow with the program and nothing catches it but a failing build: `ROOTFS_FILES`
in [../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt). The hard-coded `ls /bin`
expectations that used to catch it as well went with `kernel/test/console` and `session`.

The disk is one EC-5052: **2000 blocks, 6,144,000 bytes**, and there are **98 free** — it was 187
until the `lib/test` programs moved to the test pack, and `yacc` and `lex` have since taken 68 of
what that gave back, `expr` 14, `egrep` 14, `m4` 19, `make` 24, `dc` 32, `bc` 20 with one more
block for the `/usr/lib/lib.b` its `-l` reads, `units` 14 with three of them the
`/usr/lib/units` table it cannot run without, `awk` **42**, one of them its enlarged
manual page — the largest single addition yet, and a reminder that the number is worth reading
before a port rather than after — `crypt` 16, of which the program itself is 7 and
`/usr/lib/makekey` 3 — **the other 5 are `ed`**, which grew by half again when task C19 gave it
`-x` back, because the restored mode links `crypt(3)` and `getpass(3)` and `getpass(3)` brings
the whole of stdio with it. **A shared object can cost more than it looks**, and the place to
read that is `b6size` on the other program, not on the new one. `at` and `atrun` are **28**
between them (task C21): 12 and 13, each one block past the six direct addresses an inode holds
and so paying an indirect block as well, and **three of those blocks are directories** —
`/usr/spool`, `/usr/spool/at` and its `past` — which is the first time this tally has had to
count any. `cron` is **10** (task C22): 8 for the program, one of them the same indirect block,
1 for `/usr/lib/crontab`, and **1 for a file that is not the program's at all** — `/etc/rc`,
whose comment block crossed into a third block when the task explained itself there. `mail` is
**18** (task C28) and the largest single addition since `awk`: 16 for the program, one of them
that indirect block again, **one for `/usr/spool/mail`** — the second time this tally has had to
count a directory, after C21's three — and **one for its own manual page**, which crossed 3072
bytes when the port corrected it. That last block is C22's `/etc/rc` lesson repeating: the page
was already on the image and looked free, so the task budgeted seventeen and measured eighteen.
`calendar` is **42** (task C23), the largest single addition this tally has ever recorded and the
only one where the program is the small part of it: 8 for `/usr/lib/calendar`, 1 for the
`/usr/bin/calendar` script, 1 for `/usr/lib/calendars` — the third directory this tally has had to
count — 1 more for a manual page that crossed 3072 bytes exactly as `mail`'s did one task earlier,
and **29 for the database**, which is data v7 never had. Four of the eight files would have cost
4 and covered 114 days of the year against 362; the whole set was chosen on that measurement and
not on the size. `update` is
the other end of the range and the cheapest thing here: **1 block**, 152 words, no stdio. `awk` is also the one whose *own* ceiling was never this
number: what is left below the stack after its image is the whole of its heap
([awk/README.md](awk/README.md)).
The whole of `/usr/man` is 302 blocks, `man` 12 and
`manview` 17. That is room for a
good deal of what [TODO.md](TODO.md) has open, but it is not room for anything: weigh a large
addition against it rather than assuming. The number is printed by `b6fsutil` every time
`root.img` is built, so it is measured and not estimated — **and the running total above is the
one that drifts**: task C22 measured the pre-C22 image at 168 where this line had said 169, an
edit to some staged file having crossed a block boundary without anyone re-reading the count.
Take the number from a build, not from this paragraph.

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

* **SIMH**, under the booted kernel — for everything the above cannot say, and **there is very
  little of it left**. `kernel/test/` held eighteen tests that booted — `console`, `session`,
  `files`, `utils`, `login`, `edit`, `fsinfo`, `dd`, `mkfs`, `fsck`, `mount`, `filters`,
  `accounts`, `tar`, `inspect` and the rest — and **all of them are deleted**. Three remain:
  `boot`, a smoke test that a shell prompt is reached; `multi`, which goes on into multi-user
  mode and types at two Consuls; and `core`, which mounts the test pack and runs one program off
  it. **So a port has one world and not two now**, and a fact `b6_progtest` cannot assert — a
  quoted argument, an empty one, a temp file in the image's own `/tmp`, a second process — has
  nowhere to go. Say so in the port's `README.md` and check it by hand, as
  [expr/README.md](expr/README.md) does under "What this harness cannot say".

  **A test with a boot of its own costs two minutes and a volume number**, each having its own
  copy of the image; **3102 is the highest used, 3103 is the next free.** The deleted ones
  grafted their scripts with `b6fsutil -a` at a path distinct from the program under test
  (`/etc/mkfstest`, not `/etc/mkfs`); `login`, `multi` and `accounts` typed every character
  instead, and each needed a fixed TCP port for Consul 2 (4199, 4200, 4201) — as much of the
  `RESOURCE_LOCK`'s reason as the CPU is. An oracle that is a property of the whole image should
  be **recomputed, not checked in**.

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
  Five shapes are on the record: `grep`'s `CCL`, right-sized and stored into unmasked; `sort`'s,
  256 entries reached through a `+128` bias so a grep for the size finds nothing; `sed`'s `y///`
  table, whose width is a loop condition and a pointer bump and a number nowhere at all;
  `file`'s `english()`, a v7 wild write that this machine's unsigned `char` repairs by itself and
  that must not be "fixed" back; and `expr`'s `CCL`, the same bitmap a third time and the only
  copy with an interval repeat, so the width is written into eight places rather than five.
  **`expr` is also where the assertion shape was settled** ([expr/README.md](expr/README.md)):
  the wild store goes *forward*, into bytecode not yet written, so an undersized class comes out
  **empty** and matches nothing. A case that pins the width has to be a positive one, and the
  mask needs a negative control beside it.
  **`m4`'s `type[]` is the sixth shape and the one to check for first**, because its index is
  not a `char` at all but `getc(3)`'s result — masked with `0377` above, and `EOF` below, so 128
  entries were short at one end and negative at the other ([m4/README.md](m4/README.md)). Its
  symptom was not a misclassification either: the out-of-range read made the scanner push a
  literal `0377` into its own input, so the fault surfaced as a stray byte in the output of a
  program whose output logic was correct. **Ask what a table is indexed *by* before asking how
  wide it is.**
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

---

## What the finished tasks settled

One paragraph per finished task, newest first, kept here rather than in [TODO.md](TODO.md)
because a work plan should shrink as it is worked and this does not. Each points at the port's
own `README.md` for the detail; what is here is the part a *later* port needs to know.

C23 put `calendar(1)` on the image — forty-two blocks, of which twenty-nine are the database —
and finished by opening a task rather than closing one. Four things it settled
([calendar/README.md](calendar/README.md)):

* **`case` corrupts this shell when its arm holds a `while` and a pipeline**, which is C29 in
  [TODO.md](TODO.md) and is the first defect this file has had to record in a program that was
  already ported and already tested. What a later port needs is the second half of it: **no case
  in [sh/test/](sh/test/) contains a pipeline**, because that suite runs under `b6sim` where
  nothing can be `exec`'d, so the shell is covered for syntax and not for what syntax *runs*. A
  script that misbehaves on this machine is not automatically the script's fault, and the way to
  find out is to type its constructs one at a time at a live prompt — §9's "which world a test
  runs in", applied to debugging rather than to testing.
* **A shipped data file can disagree with the program that reads it, silently and by one
  character.** v7's generator tolerated a leading zero on the day of the month and not on the
  month, and every line of the database is `01/06`: for nine months of the year the program
  worked, printed nothing, and was right to. Where a port brings a data file its ancestor did not
  have, **run the program against it before believing either** — the image's own binaries under
  `b6sim` will do it, and did.
* **Curation is a measurement.** Four of the eight database files cover 114 days of the year and
  all eight cover 362, at four blocks against twenty-nine. `root.manifest`'s manual-page preamble
  has said "IT DOES NOT CURATE" since C25; this is the first time the alternative was costed
  rather than asserted, and the number is what decided it.
* **`kernel/test/multi` can assert what `b6sim` cannot, including for a task that is already
  finished.** C28 left *"that a letter ever arrives"* in its "cannot say" list; four stages in
  this task's dialogue assert it, because `calendar -` ends in `| mail $z`. A booting test's
  stages are cheap once the dialogue exists, and the thing to look for when adding one is the
  **echo trap** — the console echoes what is typed, so a marker must be one the typing cannot
  produce (`CAL'OK'` typed, `CALOK` matched).

C28 put `/bin/mail` on the image, eighteen blocks, and it is the first task here that began by
**overturning a row of [TODO.md](TODO.md)'s "Not ported, and why" table** rather than by taking one
of its open numbers. Four things it settled ([mail/README.md](mail/README.md)):

* **A refusal is a claim about the machine, and the machine moves under it.** Three of the row's
  four reasons were still true and cost a `dir` stanza between them; the fourth — *"there is one
  user on this machine and nowhere for a letter to go"* — had quietly stopped being true two tasks
  earlier, `kernel/test/multi` having logged root and guest in **at the same instant** since 29c.
  The line counts in that table are there so a row can be re-costed; **the sentences are what want
  re-reading**, and nothing re-reads them on its own.
* **A program that drops privilege and then needs it back re-execs itself.** `mail` is setuid root
  for `chown(2)` and the lock's `chmod(2)`, and `printmail()`'s *first statement* gives that away
  permanently, there being no saved id. So the interactive `m user` command cannot deliver — it
  forks and runs `mail user`, and a fresh setuid process does it. v7's `sendrmt()` looks like a
  uucp artifact and is really a privilege boundary, which is why dropping uucp did not drop the
  fork. The general form: **the drop point is the design**, and where a later program needs
  privilege on both sides of user input, a second process is the answer rather than a later drop.
* **A `b6_progtest` case whose program can write to its own input file cannot read that file out of
  the source tree.** Every mutating command in `mail` rewrites the mailbox `-f` named, so a single
  `d` in a `.in` would edit a checked-in fixture and every later run would test something else.
  `mail/test/` copies its fixtures into the build directory and names them relatively — the only
  test directory here that does — and that also sidesteps `@srcdir@` being an absolute path against
  a program that bounds its arguments. Any future port that *edits* what it is given wants the same
  shape.
* **A name a program defines may already be in libc, and linking proves nothing.** `mail`'s
  `lock()` collides with the real `lock(2)` stub in `lib/libc`; §1 named `abs()` and `isnumber()`
  and this is the third. Nothing failed — the program's own definition simply wins until the day
  something drags that object in. Check a new port's file-scope names against `b6nm` on `libc.a`
  rather than against the link succeeding.

C22 put `/etc/cron` and `/usr/lib/crontab` on the image, ten blocks between them, and with it
**the second daemon this port has had** and the periodic caller `at(1)` has claimed since C21.
Four things it settled ([cron/README.md](cron/README.md)):

* **The `setuid(1)` question, which was the task's first.** v7's `cron` drops to uid 1 and
  `atrun`'s `setuid(2)` needs `suser()`, so the two did not compose and the crontab line that
  runs `atrun` was exactly the composition. The call is **gone**, and the argument is that the
  privilege which matters is not `cron`'s but **who may write the crontab**: `/usr/lib/crontab`
  is 0644 and root's, so running root's own commands as root grants nobody anything, and uid 1
  on this image is an account with an unmatchable hash and not one file to its name. The setuid
  bit on `atrun` stays refused, which was the point of asking.
* **v7's `init()` was broken three ways, and the third one bites.** Its bound test runs once per
  line and its stores do not, so a record past a hundred bytes — an ordinary command with
  arguments — writes through the end of a `malloc`'d block; and it twice does `free(p)` followed
  by `realloc(p, …)`, recovering the cursor by differencing the pointer it just freed. The
  rewrite is one idea: **the cursor becomes an offset and every byte goes in through one
  bounds-checking `put()`**. §2 says an index buys nothing on this machine and that is true and
  beside the point — **an offset survives a `realloc` and a pointer does not.** Note what it
  also taught about this allocator: `realloc(NULL, n)` is `malloc(n)`, and a `realloc` that
  **fails has already freed the old block**, so the pointer must be nulled with it.
* **A daemon that forks other programs cannot leave 0, 1 and 2 closed**, which is where
  [update/README.md](update/README.md)'s precedent stops applying: a job inheriting a free
  descriptor gets it back from its own first `open(2)`, and its data file becomes somebody's
  standard output. `/dev/null` on all three, as `atrun` already did. v7's `freopen("/", …)`
  before each `exec` reads like a program giving its job a standard input and is not — it is
  plugging the hole its own `fclose(stdin)` opened.
* **A program with no diagnostic path is a program whose failures are control flow.** Descriptor
  2 is the bit bucket by construction, so `init()` grew a return value, `put()` a sticky flag,
  and `main` a rule about never walking a list a failed compile may have freed. Worth carrying
  to the next daemon.

The assertion is `kernel/test/multi` stage 12b, C20's `ps -ax` stage in a new suit and typed one
letter short for the same reason, and **checked by deleting the `/etc/rc` line and watching
`multi` fail** — which is also where it was rediscovered that a plain `make` does not rebuild
`root.img`, so the first attempt at that check passed against a stale image and proved nothing.
`make test` first, every time.

**What the stage does not say is that `cron` ever runs anything**, and the reason is a fact
about this harness worth carrying: the whole `multi` dialogue takes about **five seconds of wall
clock**, and the guest clock is *calibrated* to the host's rather than fast, so five seconds of
wall clock is five seconds of guest time. Waiting for a five-minute crontab boundary would turn
a five-second test into a five-minute one. **So the whole chain was run once by hand instead**,
and the trick that made that cheap is worth stealing: type a date whose minute is **04**, so the
next boundary the crontab names is under a minute off rather than up to five.
[cron/README.md](cron/README.md) has the four lines and what came out. The same schedule puts a
small race into stages 13 to 17, where a scheduled `atrun` can collect the `at` job the dialogue
meant to collect itself; `multi.ini` says so beside them.

C21 put `/bin/at` and `/usr/lib/atrun` on the image, 28 blocks between them, and what it
retired was **a blocker that was never there**. Three tasks stood behind "a clock this machine
has not got", and two-thirds of that sentence was wrong
([at/README.md](at/README.md)):

* **Elapsed time always worked.** The interval timer is what is *supposed* to advance the
  clock: it free-runs at `HZ` 250, SIMH calibrates it against the host's wall clock, and
  `kernel/clock.c` has incremented `time` every 250th tick since the port existed, with
  `kernel/test/uclock` asserting it. Only the **epoch** was a build constant.
* **The epoch was always settable**, by `date yymmddhhmm` → `stime(2)` → `suser()`. What was
  missing was not a mechanism but an **operator** — and v7's answer on a machine with no
  battery clock is that somebody types the date in single-user mode. So the answer was a
  fifteen-line SIMH script, [../kernel/date.ini](../kernel/date.ini), and not the kernel task
  nobody had written. **C22 and C23 were unblocked by that finding and not by their own work.**

The rule to carry is the shape of the mistake rather than the clock: **a blocker that names a
mechanism should be checked against the mechanism before it is inherited.** This one had been
copied into `cmd/TODO.md`, `etc/rc` and `kernel/test/CMakeLists.txt`, and three tasks waited on
it.

The port itself turned on one line. v7 names each spool file with `%02d` of `tm_year`, which is
**126** in 2026 and three characters, and `atrun` reads it back with `%2d` — so it takes `12`,
demands a `.`, finds `6`, and `continue`s. **Every job would have been skipped in silence**, no
diagnostic and nothing to look at but a file that never went away. Both formats carry the full
year now, `date(1)`'s own widened-year fix being the precedent, and `kernel/test/multi` matches
the four digits back out of the spool. That stage was checked by narrowing `atrun`'s `sscanf`
again and watching `multi` fail.

Two more things it settled, both about a directory nobody had needed before:

* **A 0777 spool plus "run this as its owner" is a hole**, not a wart. `link(2)` needs no
  permission on the source and carries the owner along, so any user could hard-link a
  root-owned file into `/usr/spool/at` and have `atrun` `setuid(0)` and hand it to `/bin/sh`.
  A job `at` created has exactly one link, so anything with more is refused.
* **A new port uses `opendir(3)`**, §5's instruction, and `atrun` is the first taker.
  `%.14s` — v7's `DIRSIZ` written into a `sprintf` — went with the `/bin/mv` fork it was
  formatting for; the move to `past/` is `link()` plus `unlink()`.

C20 put `/etc/update` on the image, one block and 152 words, and with it **the first daemon this
port has had** — the first program that outlives the shell that started it. What it settled was
not the port, which is a `sync()` in a loop, but the two claims that had kept the line out of
[../etc/rc](../etc/rc), and **both were wrong**
([update/README.md](update/README.md)):

* **A daemon in a script `init` re-runs does not accumulate.** `/etc/rc` really does run on every
  pass through init's loop — but `shutdown()` runs *first* on every pass, and `kill(-1, SIGKILL)`
  from root spares only `proc[0]` and `proc[1]`, so the previous copy is dead and reaped before
  the line is read again. The rule to carry: **read `shutdown()` before believing a claim about
  what survives a pass**.
* **`/etc/rc` has an assertion home again, and had one all along.** `kernel/test/console` was
  deleted, but `kernel/test/multi` types the same `^D`, runs the same script, and had been
  matching the `date` line for some time unnoticed. It now asks `ps -ax` for the daemon as well.
  **That reopens the two lines deferred for want of an assertion** — the boot-time `fsck` and
  `rm -f /tmp/*` — which wait on nothing now but somebody willing to write the stage.

The lesson worth carrying out of the *test*, and it cost a rewrite: **an expect string can be
satisfied by the shell's own echo**. `ps -ax | grep update` puts the word `update` into the console
stream before `grep` runs, so with no daemon at all the prompt follows the echo and the rule
matches — a green test asserting nothing. The pattern is typed one letter short for that reason,
and the stage was checked by deleting the `/etc/rc` line and watching `multi` fail.

The one true cost is a state transition rather than a leak: a permanent child means
`multiple()`'s `wait() == -1` never comes back, so init's `allgone` return to single-user and
`merge()`'s two `/etc/ttys` diagnostics are set and never said. That is v7's own arithmetic —
v7's `rc` started `update` and `cron` on the same reasoning — and it is written down in
`multiple()` rather than fixed.

C19 put `/bin/crypt` and `/usr/lib/makekey` on the image, and what it
settled was larger than two small programs. **A key schedule that overflows is not a key
schedule here**: v7's `crypt` derives its rotor through a `long` that wraps at 32 bits, and
`b$mul` on this machine keeps the HIGH bits of an overflowing product, so the arithmetic had to
be bounded to 32 bits explicitly — after which this `crypt` is bit-compatible with a PDP-11's
and the files interchange ([crypt/README.md](crypt/README.md)). It also **retired a premise**:
`ed` had dropped `-x` on the grounds that `makekey` would never exist and the seed arithmetic
could not be reproduced, and both were answered, so `-x` is back over the same `rotor.c` that
`crypt` links — one implementation, so the two manual pages' promise that they interoperate is
a property of the build. The cost is the lesson worth carrying: **a shared object can cost more
than it looks**, `ed` growing five blocks because `getpass(3)` brings stdio with it.

C18 put `/bin/units` on the image and, with it, the first floating-point
program this port has had — which is how it was discovered that **an arithmetic fault stopped the
machine**: `kernel/trap.c` decoded five ГРП causes and neither of the two arithmetic ones, so a
floating overflow or divide by zero in any user program reached `panic("trap")`. It decodes seven
now, both new ones as `SIGFPE`, asserted by `kernel/test/ufpe` — the only forge test that links
the real `trap.c` rather than a copy of it. **The rule that came out of it, and that every future
port doing arithmetic inherits**: an overflow here is a fault and not a signal, so a range gate
goes *before* the operation ([units/README.md](units/README.md),
[../lib/libm/README.md](../lib/libm/README.md)). Underflow is a silent zero and raises nothing.

**Every grammar is done**, too: C17 put `/bin/awk` on the image, the one
program that is a yacc grammar *and* a lex scanner, and with it C10 is spent to the last risk.
`b6yacc` and `b6lex` are host tools (C10a, C10b) and `/usr/bin/yacc` and `/usr/bin/lex` are on
the image with their skeletons (C10c, C10d). **C11, C26, C13, C14, C16 and C17 proved it** —
`expr`, `egrep`, `m4`, `make`, `bc` and `awk` are on the image, built from their grammars by
`b6_yacc()`, and the skeleton needed no change for any of them. Five things they settled, in
order:

* **A non-zero conflict count is not by itself a sign of trouble** (C26): `egrep.y` has two
  shift/reduce conflicts, both v7's own, both on the `error` token and both resolved by shifting
  ([egrep/README.md](egrep/README.md)). What matters is that the number does not move.
* **A grammar plus a hand-written translation unit in one `b6_prog()`** works (C13), needs no
  `-I` when the C file names no token, and wants `b6nm` over the result to check that each shared
  global is defined once ([m4/CMakeLists.txt](m4/CMakeLists.txt)). C14 is the same shape with six
  C files instead of one, and it does want an `-I` — on its own directory, the generated parser
  being compiled somewhere else and including the program's header.
* **A conflict count in the dozens is still only a number to hold still** (C16): `bc.y` reports
  12 shift/reduce and 30 reduce/reduce, `stat` and `e` both deriving `LETTER '=' e`, and running
  `b6yacc` over the unmodified upstream grammar gives the same two numbers
  ([bc/README.md](bc/README.md)).
* **`%union` works** (C14). It was the last of C10's risks: a union `YYSTYPE` turns the
  skeleton's three value copies into aggregate copies, and nothing had ever compiled one. It was
  retired ahead of the port on [yacc/rootfs/calcu.y](yacc/rootfs/calcu.y) rather than inside it,
  so that a miscompile could not read as a grammar bug, and it passed first time
  ([yacc/README.md](yacc/README.md) under "The contract").
* **A grammar and a scanner in one `b6_prog()`** (C17), which is all C10 ever had left to
  prove. Two `-I`s, in opposite directions: the source directory, because the generated
  parser and scanner include the program's own header, and `${<var>_DIR}`, because the
  hand-written units include the `y.tab.h` `b6_yacc()` leaves beside the parser -- and that
  generated header must be named in `KHDRS` or the units compile before `b6yacc` has run.
  `#define YYSTYPE` wants a **typedef** behind it, since yacc writes `YYSTYPE yylval, yyval;`
  and the second declarator of a pointer `#define` is not a pointer
  ([awk/README.md](awk/README.md)).

**Two loose ends about the terminal, one line each and neither worth a task of its own.** `TANDEM`
is honoured by the kernel — `ttyblock()` queues the stop character when the input queue passes
`TTYHOG/2` and `canon()` sends the start character back — and **no program on this image can set
it**, the cut in [stty/README.md](stty/README.md) being deliberately subtractive.
`TIOCEXCL`/`TIOCNXCL` and `TIOCHPCL` are the other shape: `ttioccomm()` accepts all three and sets
`XCLUDE` or `HUPCLS`, and **nothing in the kernel ever tests either bit**, so an exclusive-open
request and a hang-up-on-last-close request are both silently ignored. Both are worth knowing
before somebody reports them as bugs.

**[../etc/rc](../etc/rc) still wants two lines, and neither waits on a program any more** — C22
took the last one that did. A boot-time `fsck` and the `rm -f /tmp/*` line wait on nothing but an
assertion, and **that excuse expired with C20**: `kernel/test/multi` runs the script and asserts
three of its lines already, so a fourth and a fifth are a stage each in `multi.ini`. Whoever
writes them should read what C20 learned about an expect string an echo can satisfy, and note
that a `fsck` which repairs the mounted root does not return.
