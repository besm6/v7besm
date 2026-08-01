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
(`df/`, `du/`, `quot/`), the one of C4b (`dd/`), the one of C4c (`mkfs/`), the one of C4d
(`fsck/`), the four of C4e (`icheck/`, `dcheck/`, `ncheck/`, `clri/`), the two of C4f
(`mount/`, `umount/`), the six of C5a (`wc/`, `cmp/`, `sum/`, `tee/`, `split/`, `rev/`), the seven of C5b
(`tr/`, `uniq/`, `comm/`, `tail/`, `od/`, `look/`, `col/`) and
the two of kernel task 29b (`getty/`, `login/`) today. That is the only marker;
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

### 2. An `int` is not a `char *` — and `<` between two of them works now

A `char *` here is a **fat pointer** — byte offset in bits 47–45, word address in bits 15–1 —
and the offset **decrements** as the pointer advances, so the raw word does not sort. The
compiler deals with it:

> **A relational operator between two byte pointers orders them correctly.** `<`, `>`, `<=`
> and `>=` between two operands of type `char *`, `signed char *`, `unsigned char *` or
> `void *` (arrays decay first) lower to **`b$pdiff`** — the same helper as `-`, which decodes
> both operands to absolute byte positions — followed by a sign test. `==` and `!=` are raw
> word compares and are right too, because the encoding is canonical: two pointers to the same
> byte are the same word.

**This was not always true, and most of this directory was ported while it was not.** Until the
external compiler's fix of 2026-06-17 (`translator/expr.c` in
[besm6/c-compiler](https://github.com/besm6/c-compiler/), which also fixed `b$pdec`'s in-word
decrement) a relational compiled to an integer comparison of the whole word, the offset field
dominated the address field, and the ordering came out scrambled and inverted within a word —
`p < end` on a buffer cursor was silently, unpredictably wrong, and one instance made
`getpass()` return the empty string every time, for months, in a library everything links.
**Everything below is the record of that period.** The rewrites it produced are worth keeping,
because a comparison is still two out-of-line calls (`b$pdiff` then `b$lt`) where an `int`
index is a register test — but no port after that date has to make them, and a `<` between two
`char *` in a v7 source is no longer a bug to hunt.

Two things the fix does not cover, and both are live:

* **A fabricated pointer matches nothing.** `dd`'s `sbrk` failure test spelled `(char *)-1`;
  equality against a word made out of an integer can never meet a real fat pointer, whose
  marker and offset the compiler put there.
* **A relational between two `void *` is a hard error** — `Invalid types for comparison`, from
  the front end's constraint check, not from the lowering: C11 6.5.8 wants complete object
  types and `void` is not one. gcc and clang take it as an extension, so a v7 source that
  compares two `void *` needs a cast to `char *`.

A count of the candidates, as they stood while the hazard was live:

| source | `char *` comparisons |
|---|---|
| `sort.c` | **fifteen** — all in `cmp()` and `newfile()`: `pa<la`, `ipa>pa`, `cp>=ce`, `cp < tspace+ntext`, … |
| ~~`fsck.c`~~ | ~~five~~ — **found and fixed, task C4d**: two of them were `dirscan()`'s backward byte copy of a directory entry, which is a struct assignment now (an entry is four words); the others bounded a name, a line and the digits of a reconnected i-number, and are index counts. The twelve other relationals in that file compare `daddr_t *`, `ino_t *` and `DIRECT *` and were left alone |
| ~~`ed.c`~~ | ~~**ten**~~ — **twenty**, and this table undercounted by half; the two `-x` took with it left nineteen to rewrite. **Found and fixed, task C3**: they are index counts and `int` differences now, and [ed/README.md](ed/README.md) lists them. Every one bounds a buffer the regex engine or the substitute path writes into |
| `fgrep.c` | four — `p > &buf[512]` ×2, `p <= nlp`, `nlp < &buf[1024]` |
| `grep.c` | three — `ep >= &expbuf[ESIZE]`, `sp > cstart`, `lp >= curlp` |
| `sed/sed1.c` | three, all `sp >= &genbuf[LBSIZE]` |
| `pr.c` | two, both `>= &buffer[BUFS]` |
| ~~`dd.c`~~ | ~~one — `ip > ibuf`~~ — **found and fixed**, task C4b: it zero-fills the input buffer before every read under `conv=noerror`/`conv=sync`, and is a word loop over `btow(ibs)` words now |
| ~~`date.c`~~ | ~~one — `sp < ep`~~ — **found and fixed**, task C2a: it bounded the in-place reversal of `argv[1]`, and is an index pair now |
| ~~`basename.c`~~ | ~~**two**, not the one this table used to claim — `p1>p2 && p3>argv[2]`~~ — **found and fixed**, task C2b: both are in the *same expression*, the backwards suffix compare, and both are index counts now |
| ~~`mount.c`, `umount.c`~~ | ~~five~~ — **found and gone, task C4f**, though not one of them was rewritten: three (`np > argv[1]`, `np < &mp->spec[NAMSIZ-1]`, `np < &mp->file[NAMSIZ-1]`) bounded the basename stripping and the fixed-width copy into the mount table, and the other two are `umount.c`'s copies of the same. The table became a **text** file for an unrelated reason (§2's sibling hazard — see [mount/README.md](mount/README.md) §2) and every one of the five went with it. Worth recording because it is the cheap way out and it is not always available: **a hazard in code that exists only to serve a file format can be deleted by changing the format**, when the format is the program's own business |
| ~~`wc.c`, `cmp.c`, `sum.c`, `tee.c`, `split.c`, `rev.c`~~ | **none** — grepped, task C5a, and this is the *second* negative result in the table and a more surprising one than C4e's. Six **text filters**, 487 lines of buffer arithmetic and character loops, and not one `char *` relational between them. `tee.c` is the one that walks a buffer and it does so with `int` indices (`r`, `w`, `p`, `i`); every other pointer test in the six is `==`/`!=` against `NULL`. The reason is the same as C4e's and is worth generalising: **a v7 source acquires this hazard when it parses, not when it reads bytes.** `sort`, `grep`, `sed` and `pr` all hold a cursor inside a buffer they are deciding about; a filter that copies its input holds an index into a buffer it is filling |
| ~~`tr.c`, `uniq.c`, `comm.c`, `tail.c`, `od.c`, `look.c`, `col.c`~~ | **none** — grepped, task C5b, and this is the *third* negative result and the one that settles the shape. Seven more text filters, 1,364 lines, and not one `char *` relational between them either. Two are worth naming because they look like counter-examples and are not: `comm.c` holds two cursors in `compare()` and reaches them by forming `lb1 - 1` and incrementing back — a pointer before its buffer, which is UB and was rewritten as an index pair, but never a *relational*; and `col.c` walks `lbuff` with a file-scope `char *line` whose every bound (`lp > cp`, `lp < cp`) compares the **column count** rather than the pointer. So the rule holds with a sharper edge: a v7 source grows a byte cursor when it parses, and a cursor is not a hazard until something ORDERS two of them |
| ~~`icheck.c`, `dcheck.c`, `ncheck.c`, `clri.c`~~ | **none** — grepped, task C4e. The only pointer relational in the four is `ncheck.c`'s `++hp >= &htab[HSIZE]`, over a `struct htab *`, which is thin and correct; it went anyway when the hash table became one sized from the superblock and indexed by i-number. Worth recording as a *negative* result: four v7 sources full of block and inode arithmetic and not one `char *` cursor between them, because none of them parses anything |

What the table is still good for is the shape it found, which outlived the bug: **a v7 source
grows byte cursors when it parses, not when it reads bytes.** `sort`, `grep`, `sed` and `pr`
each hold a cursor inside a buffer they are deciding about; the six filters of C5a, the seven of
C5b and the four checkers of C4e copy or count and hold `int` indices already. **Nineteen v7
sources have now been grepped for it and come back empty**, which is worth stating as a
prediction rather than a tally: the four programs the table still lists — `sort`, `grep`, `sed`,
`pr` — are the ones that *decide* about a buffer, and they are where the remaining instances are. That is where to expect the
*other* three hazards too.

Those three — a flag packed into bit 0 of a pointer, a bit mask used to round to a word when
`BYTESPERWORD` is 6, and a cast to a pointer that *floors* rather than rounds
([sh/README.md](sh/README.md)) — come round again in anything that manages its own arena, and
none of them is fixed in the compiler. `sort` and `find` both call `sbrk` and are the places to
expect them. **`dd` called it too and turned out to have none of the three**: its use is two
flat allocations and no arena at all, and what it did have was this section's own hazard, plus
the `(char *)-1` above. Grepping for the arena hazards is still right; expecting them because a
program calls `sbrk` is not.

### 3. A `long` is one word, and `%D` is not a conversion

`long` is `int` is one 41-bit word. Two consequences, and the second is nastier than it looks:

* `%ld` / `%7ld` is harmless — `l`, `h`, `L`, `j`, `z` and `t` are parsed and ignored — but it
  means nothing, and should be written `%d`.
* **`%D` prints the two characters `%D`.** [../lib/libc/stdio/doprnt.c](../lib/libc/stdio/doprnt.c)
  does not know that PDP-11 conversion, and an unknown conversion is echoed verbatim **and
  consumes no argument** — so it desynchronises every later conversion in the same format string.
  v7 wrote `%D` freely. `ls` had two.

Sources carrying the most `long`: `ps.c` (17), ~~`od.c` (10)~~ (**done, task C5b** — the count
was exact and every one was a single word, but they were the *least* of that port: see
[od/README.md](od/README.md), where five other things carried the 16-bit word and one of them
truncated silently), ~~`cmp.c` (7)~~ (**done, task
C5a** — the count was exact, and all seven were `int`: a byte offset, a line number, the two
skip counts, `otoi()`'s declaration, its definition and its accumulator), `find.c` (7),
~~`du.c` (5)~~ (**done, task C4a** — all five were `int`), `strip.c` (5), `nm.c` (4),
`grep.c` (4). C5a's other five carry six between them (`wc.c` six, `sum.c` one, `tee.c` one),
and C5b's other six carry eleven (`look.c` six, `tail.c` five).

**And thirteen filters have now been grepped for `%D` and `%O` with no hit at all** — including
`od`, the one program in the tree whose entire output is numbers, which escaped §3's
verbatim-echo trap because it uses no numeric `printf` conversion: it has its own recursive
`putn()`. That is the second negative result in this section and it is worth the same caution
the §2 table's are: **the trap is real and the sources that spring it are the ones that print a
number they did not compute themselves.**

The other direction is worth a glance too: **plain `char` is unsigned here**
([../doc/Besm6_Data_Representation.md](../doc/Besm6_Data_Representation.md)), so the
`(unsigned char)` a `<ctype.h>` call wants is habit rather than necessity — and a
`signed`→`unsigned` conversion is a bare reinterpretation of the word, not C11's modulo
adjustment, because an `int` occupies bits 41–1 and an `unsigned` all 48. Prefer `int` wherever
v7 wrote `unsigned` for no reason.

### 4. A filesystem block is 3072 bytes; a *reported* one is 1024

`BSIZE` is 3072 — not a power of two, and [../include/sys/param.h](../include/sys/param.h) says
outright that there cannot be a `BSHIFT`/`BMASK` to go with it. So every `>>9`, `<<9` and `&0777`
that means *a filesystem block* is wrong and becomes a divide or a remainder. (`BWSHIFT` 9 and
`BWMASK` 0777 exist, and are **word** offsets within a block, which is a different quantity.)
Two of task C4a's three programs met `BSHIFT` as a *compile error*, which is the best way to
meet it.

**But a count REPORTED TO A USER is not in that unit.** `df`, `du`, `quot` and `ls -s` all
count filesystem blocks internally and then print `KBPB` — three — of them per block, so that
what they print is in **1024-byte blocks** and means something without knowing `BSIZE`.
`KBYTE` and `KBPB` are in [../include/sys/param.h](../include/sys/param.h) beside `BSIZE`, and
`KBPB` is derived from it so that retuning the block size cannot leave four programs quietly
lying. Three rules come with it, and a port that reports blocks should follow all three:

* **The multiply goes at the `printf`, and nowhere else.** Every count stays in filesystem
  blocks right up to the moment it is printed, so a variable called `blocks` holds blocks.
  `quot -c` is what forces this rather than style: its histogram is *indexed* by a file's block
  count, and converting at the source would cut what `TSIZE` covers to a third while making two
  buckets in three unreachable.
* **Assert the unit divides.** `_Static_assert(BSIZE % KBYTE == 0, …)`, beside whatever layout
  assertions the program already carries. 3072 is three KiB exactly today; nothing but that
  assertion says it must stay so.
* **Say it in the manual page**, in a `BLOCKS ARE 1024 BYTES` section — the unit, that the
  filesystem's block is three of them, that every count is therefore a multiple of three, and
  that the numbers are *half* a PDP-11's rather than a sixth, v7 having counted 512-byte
  blocks. All four pages have one; `ls.1`, which had never stated a unit at all, got one too.

**A block that is a program's own business is neither of these and is converted to neither.**
`ed`'s temp file is 512-byte blocks by its own choice and stays that way; so do `tar`'s record
and `tail -b`, which [TODO.md](TODO.md) already rules on, and `dd`'s `bs=` is the user's.

**But a *default* is not the user's, and `dd` is where that was settled.** No number `dd` prints
changes unit — it reports records — so the `KBPB` multiply above does not reach it at all. What
did have to change is what the user gets when they name no unit: `ibs`/`obs` default to `BSIZE`
and the `b` suffix multiplies by `BSIZE`, where v7's were 512, because 512 is not a whole number
of words and `physio()` refuses it before the driver sees it — so with v7's defaults the
commonest thing `dd` exists to do could not reach the disk. `w` came along free, `sizeof(int)`
being 6. The rule that generalises: **a constant is the user's business only while it still
names something on this machine.** 512 named a PDP-11 disk block and names nothing here.

**And a size that names a *device* stays in filesystem blocks, which is `mkfs`'s exception.**
`mkfs special nblocks` takes the superblock's `s_fsize` verbatim, and what it prints — the
i-list extent, the first data block — is `s_isize` and an i-node count. These are not
measurements of a filesystem, they are the *description* of one, and `KBPB` does not appear in
`mkfs.c` at all: a `mkfs` that had to be told 6000 for a drive holding 2000 blocks would be
lying about the thing it is writing, and the number would then disagree with every other
number about the same volume. `b6fsutil` reports the same way and for the same reason. The
manual page still owes a section, but it is the mirror one — `BLOCKS HERE ARE 3072 BYTES`,
saying that `df(1M)` will report three times what `mkfs` was given.

### 5. `DIRSIZ` is 18

`struct direct` is four words — a full-word `d_ino` and three words of name.  So `%.14s` is `%s`,
`char name[15]` is `char name[DIRSIZ + 1]`, and a name read out of a directory **is not
NUL-terminated** unless the port terminates it (a v7 bug `ls` had to fix rather than carry).
Anything that walks directories — `rm -r`, `du`, `find`, ~~`ncheck`~~, ~~`dcheck`~~, `mv` —
inherits this. The two struck through are done (task C4e) and took the same fix: `%.14s`
became `printf("%.*s", DIRSIZ, …)`, which is `%s` with the bound restored rather than `%s`
alone, a name out of a directory being neither NUL-terminated nor the program's own storage.

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
| `fsck` | 125 | 6,323 | 587 | 3,807 | **10,842** |
| `sh` | 121 | 7,039 | 637 | 131 | **7,928** |
| `ls` | 110 | 4,632 | 298 | 2,638 | **7,678** |
| `login` | 106 | 5,063 | 408 | 1,321 | **6,898** |
| `ed` | 95 | 4,027 | 50 | 633 | **4,805** |
| `quot` | 102 | 5,208 | 227 | 4,375 | **9,912** |
| `icheck` | 92 | 3,725 | 305 | 4,143 | **8,265** |
| `ncheck` | 86 | 3,468 | 215 | 4,228 | **7,997** |
| `dcheck` | 84 | 3,222 | 213 | 4,136 | **7,655** |
| `du` | 83 | 3,041 | 198 | 3,132 | **6,454** |
| `mkfs` | 85 | 2,956 | 222 | 2,571 | **5,834** |
| `df` | 80 | 2,633 | 177 | 2,570 | **5,460** |
| `dd` | 83 | 3,451 | 503 | 1,056 | **5,093** |
| `clri` | 86 | 3,021 | 219 | 2,575 | **5,901** |
| `mount` | 93 | 3,428 | 374 | 1,079 | **4,974** |
| `umount` | 93 | 3,389 | 364 | 1,079 | **4,925** |
| `cat` | 83 | 2,989 | 165 | 1,544 | **4,781** |
| `od` | 87 | 3,715 | 167 | 1,039 | **5,008** |
| `col` | 87 | 3,500 | 168 | 1,434 | **5,189** |
| `uniq` | 84 | 3,240 | 187 | 1,371 | **4,882** |
| `look` | 85 | 3,432 | 185 | 1,162 | **4,864** |
| `comm` | 83 | 3,099 | 168 | 1,037 | **4,387** |
| `split` | 91 | 3,092 | 190 | 1,054 | **4,427** |
| `rev` | 84 | 2,981 | 159 | 1,204 | **4,428** |
| `cmp` | 80 | 3,036 | 177 | 1,038 | **4,331** |
| `wc` | 81 | 2,908 | 167 | 1,032 | **4,188** |
| `sum` | 81 | 2,842 | 168 | 1,032 | **4,123** |
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
| `tee` | 22 | 397 | 54 | 1,029 | **1,502** |
| `tail` | 30 | 666 | 34 | 697 | **1,427** |
| `tr` | 43 | 1,588 | 152 | 1,169 | **2,952** |
| `getty` | 34 | 361 | 28 | 11 | **434** |

**`mount` and `umount` are the two smallest programs of task C4** and the reason is the
mirror of `ed`'s: they carry no block buffer at all. Every other program in that task holds
one or more aligned `BSIZEW` slots for the raw path — `icheck`'s five come to 2,560 words —
and these two read no device, the kernel doing all the filesystem work they ask for. What is
left is stdio and about 3,400 words of text.

Most of `cat` is libc's stdio, and `fsck` is the largest program on the image — measured
before it was ported, as task C4d's brief demanded, and it came in at a third of the ceiling
even though it is the longest source in C1–C8. `sort`, `awk` and `make` are the three left to
measure early rather than late. Nothing before task C6 is in danger of the first ceiling.

**The bottom rows say what stdio costs.** Every program above `basename` links `printf`
and a `FILE` buffer, which is the ~1,030 words of bss and most of the text they have in common;
`test`, `tee` and `getty` link neither — `test`'s whole output is four `write(2)` calls,
`tee`'s is `write(2)` on the descriptors it was handed, and `getty`'s is one `write(2)` per
character — so `getty` is the smallest program on the image, an eleventh the size of `cat`.
That is the difference `lib/libc/README.md` measures for `hello`, seen from the other end.

**`tee` (task C5a) is the cleanest measurement of it there is**, because it is the one program
here whose bss is *entirely* its own: 1,024 of its 1,029 words are the two `BSIZE` buffers it
copies through, and the five left over are `openf[]`, `n`, `t` and `aflag`. So its 397 words
of **text** are the whole program — a quarter of `basename`'s, which does far less — and the
comparison to make is with `cat`, which does the same job through stdio and costs 2,989. Three
times the text and half a kiloword more bss, for a `getc`/`putc` pair. It is not an argument
for writing the rest of the userland against `write(2)`; it is the number to have in mind when
a program is close to the ceiling and its output is a stream rather than a report.

**And `ed` is the third angle on it**, being the one *large* program that pays nothing either:
it has no format string at all (`putd()` over `write(2)`), so its 4,027 words of text — 1,038
more than `cat`'s — come with 911 words *less* bss, and the two totals land twenty-four words
apart despite `ed` being five times the source. When measuring a candidate against the ceiling,
ask what it prints with before extrapolating from a line count.

**`tail` (task C5b) is now the smallest program on the image that does real work**, at 1,427
words — a third of `cat` and below `getty`'s neighbours — and for `tee`'s reason carried
further: it links **no stdio at all**, `read(2)` and `write(2) `end to end, and 683 of its 697
words of bss are the 4,097-byte ring it holds a file's tail in. 666 words of text is the whole
of a program with a forward mode, a backward mode, a reverse mode and four unit suffixes. Set
beside `comm`, which does less and costs 4,387 because it holds **three** `FILE`s, it is the
sharpest form of the rule above: **what a program prints with dominates what it does.**

**And there is a fourth ceiling, which task C5b found and which nothing checks.** §6 names
three; the heap is a fourth. `rootfs_<name>_size` weighs `const + text + data + bss` and cannot
see a byte of allocated storage, and `col` is the first program here whose footprint is not
statically knowable — `page[256]` is a sliding window of `malloc`'d half-lines. Its worst case
is 34,304 words, **past the 28,672 the address space allows**, on top of the 5,189 the table
gives it. What stops that being a wild store is that `col` already checks its `malloc` and exits
with a diagnostic; what stops it being a surprise is saying so. Anything in C10 that manages its
own storage inherits this — `sort`, `find` and `make` all call `sbrk` — and the general form is
that **the size ctest is a bound on the image, not on the program.**

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
  list and real i-list with no boot. C4c took it further: a program that *writes* a device
  writes a file too, so `cmd_mkfs_layout` hands `mkfs` a blank of exactly N blocks and then
  compares what comes back with `b6fsutil -n` **byte for byte** — which is the strongest oracle
  anywhere under this harness and costs a tenth of a second. C4d took the third step: it
  **damages** a fixture with `b6fsutil -D`, has `fsck` repair it, and requires `b6fsutil -c` —
  a separate implementation of the same checks, in host C++ — to find nothing afterwards. Two
  rules came out of that and hold for anything similar. **Assert that there was something to
  repair**: every case there requires the host's checker to *fail* first, or a damage spec that
  had drifted out of step with its fixture would leave a test that fixes nothing and passes.
  A fourth limit came with that, and it is about the *host* rather than the simulator:
  **`getpwent(3)` opens the literal `/etc/passwd`**, so under `b6sim` a program that maps a uid
  to a name reads the build machine's password file and no case may assert what comes back.
  There is no uid to steer around it — see [df/README.md](df/README.md).
  **Task C4f is where that limit became total**, and it is the only program in `cmd/` for
  which this harness can say nothing: `mount(2)` and `umount(2)` are the *host's* under
  `b6sim`, so a case that reached either would be asking the build machine to graft a
  filesystem onto itself. `cmd/mount/test` has four cases and every one stops before the
  syscall — which is only possible because the port settles its arguments before it opens
  `/etc/mtab`, where v7 reads the table first, a reordering made for exactly this.
  The general question, and it is the one `getpwent(3)` asks too: **when a program names a
  fixed absolute path or a global system state, ask whose it is under `b6sim`** — and if the
  answer is "the build machine's", the case belongs under the booted kernel or nowhere.
  Two more limits C2a ran into: **`stime(2)` is a no-op that reports success**, so a
  program that sets global state cannot be asserted there at all; and **`kill(2)` is the build
  machine's own**, so no case may name a pid. And two C2b ran into: a program that **does not
  terminate** cannot be a case at all, however it is invoked, which is why `yes` has no `test/`
  directory; and **`argv[0]` is the staged path**, so a program that dispatches on the name it
  was called by — `test` and `[` — can only be tested through one of its names here.
  **Task C5b widened that one**: it is not only a program that *dispatches* on `argv[0]` but
  any program that *prints* it, since the staged path is an absolute build directory and cannot
  go into a checked-in `.expected`. `col` names itself that way in both its diagnostics, so
  both belong under the booted kernel — where the shell hands `exece()` the word as it was
  typed (`cmd/sh/service.c`'s `execs()`) and the message reads `col:`.
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
  whose oracles are *recomputed* rather than checked in — and `kernel/test/dd` (task C4b) for
  anything that moves **bulk data** through a device, or that must be handed a record size the
  hardware refuses; its oracle is the disk itself, the guest's transfer being `cmp`'d against the
  same offset in the container the boot was handed, and it is the first test here to assert a
  **refused** transfer at all — and `kernel/test/mkfs` (task C4c) for anything that **writes** a
  device, or that needs a **second drive**: it is the only test here that attaches two, the only
  one whose subject is a filesystem that did not exist when the machine booted, and the one that
  holds `kernel/dev/md.c` to stamping each drive's own volume label into what it writes. Its oracle is a **byte-for-byte `cmp` against `b6fsutil -n`**, which is available
  because the guest program and the host tool are transcriptions of each other and the only
  thing between them is a timestamp six bytes into the superblock — and `kernel/test/fsck`
  (task C4d) for anything that must **repair** what is already on a device, or that needs a
  pack arriving with something *wrong* on it: it attaches its scratch drive without `-n`,
  because the damaged filesystem comes from the host, and its oracle is `b6fsutil -c` on what
  comes back. It is also the only test here that examines **the filesystem the machine is
  running on**, which costs it an ordering constraint no other test has — see the warning in
  `kernel/test/fsck.sh`'s header.
  And `kernel/test/mount` (task C4f) for anything that goes through the **buffer cache**
  rather than `physio()`, or that needs a filesystem the machine can *reach* rather than only
  measure: it is the only test that attaches **three** drives — a root, a scratch pack
  carrying a host-built filesystem, and a **blank** one attached with `-n` that exists to be
  mounted and refused — and the first thing in this tree ever to call `mount(2)`, `bdevsw[0]`
  minor 1 having never carried a block. It is also the one test whose program has **no**
  `b6sim` half at all (see above), so unlike `fsck.ini` it is not the second word on its
  subject but the only word.
  And `kernel/test/filters` (task C5b) for anything whose subject is **bytes** — it is the
  one test here that runs thirteen programs, the only one that puts a **pipe** on a program's
  standard input, and the only one whose oracle is masked **nowhere**, every number in its log
  being computed by a filter from a corpus the script writes for itself in the same run.
  **Task C4e was the one task that stopped at the first harness**, and saying so out loud is
  what got it closed: `icheck`, `dcheck`, `ncheck` and `clri` were asserted under `b6sim`
  alone, where nothing exercises the raw path — and two of them *write* it.
  [TODO.md](TODO.md) carried that as a named loose end with the volumes a closing test would
  take, and **task C4f folded it in** rather than spending a second two-minute boot on it:
  section 6 of `kernel/test/mount.sh` runs all four over the real device, on a pack that has
  just been mounted, written and unmounted. **A deferral said out loud is the difference
  between a known gap and an unknown one** — C4d's `hostblind` marker made the same point
  about a disagreement rather than a gap, and both were paid off by the next task that
  happened to be in the neighbourhood.
  Each test has its own copy of the
  image at its own volume number; **3097 is the highest used** (`edit` took 3086, `fsinfo` 3087,
  `dd` 3088, `mkfs` two — 3089 root and 3090 scratch — `fsck` two more, 3091 and 3092,
  `mount` **four** — 3093 root, 3094 scratch, 3095 the blank pack it mounts to be refused and
  3096 the second scratch `NMOUNT` 8 allowed — and `filters` 3097).  The next free number is
  3098.
  All but `login` graft a script onto that copy
  with `b6fsutil -a`, and `login` is the exception because what it tests is the thing that reads
  what is typed: it types every character it needs. `mkfs` grafts its script at
  `/etc/mkfstest` rather than at `/etc/mkfs`, which is the program; `fsck` and `mount` do the
  same, for the same reason.

Most of task C5 lands in the first; C1, C3, C4 and C6 land in the second. Where a program can run
in both, **do both** — the first time the libc suite was run in both worlds it found two bugs
nothing else had exercised.

### 10. The manual page comes with the source

Each v7 command ships its `.1`, and it is **already in the program's directory** — see "The
sources are already here" above, including the ten programs that have no page of their own.
Follow the [../lib/libc/man/](../lib/libc/man/) precedent: **correct it in place** — ANSI SYNOPSIS,
every wrong claim fixed where it stands and marked `Note:`, block counts in the 1024-byte block
§4 describes,
`DIRSIZ` 18 where it shows. Nothing installs any of them; they are read with `nroff -man`.

**A page containing non-ASCII wants `nroff -Kutf8 -man` and says so in a `.\"` comment of its
own.** Plain `nroff -man` reads the input a byte at a time and renders a Cyrillic letter as
two, so the page becomes mojibake in exactly the passage that is about text not being mojibake.
Task C5a's `wc.1` and `rev.1` are the first two pages in this tree to contain any, and both
carry the comment; there is no reason to avoid the character, and every reason not to leave the
reading command a matter of luck.

Rewrite
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

**The third form is the worst and `col` is its worked example: a program that STEALS bit `0200`
for a flag of its own.** `/bin/sh` marked a quoted character with it and `col` marks a Greek
half-shift with it; `grep` and `sed` will have the `CCL` bitmap, which is the same hazard from
the table side. The shell's was fixed by moving the mark, `ed`'s replacement-text escape by
replacing bit `0200` with a `QESC` prefix, and `col`'s by **deleting the feature**, there being
no Model 37 Teletype on this machine and no producer of `SO`/`SI` for it
([col/README.md](col/README.md)). Which of the three is right depends on what still feeds the
mechanism, and that is the question to ask first.

**And a masked byte does not have to vanish to be wrong.** `col`'s `c &= 0177` turns `привет`
into `P?QP8P2P5Q` — the six characters become ten bytes of plausible ASCII with two dropped, not
an empty line and not a diagnostic. That shape is worth expecting: **a `0177` mask usually
produces junk that looks like output**, which is why the assertion has to be a case with a known
answer and not an eyeball.

Two more things that follow, and are worth knowing before writing a test: the shell's pattern
language matches **bytes**, so `?` is one byte and not one letter; and the terminal driver
refuses a typed `0377` ([../kernel/dev/tty.c](../kernel/dev/tty.c)), which is the raw queue's
delimiter — a script read from disk may contain one, a console user cannot type one.

**A `<ctype.h>` call is the quiet form of the 128-entry table**, and two of C5b's seven had one:
`lib/libc/gen/ctype_.c` is **129 entries** and says outright that only `isascii()` may be applied
to a byte above `0177`, so `isdigit(argv[1][1])` in `uniq` and `isalnum`/`isupper` in `look` read
off the end of it for a byte the caller chose. Guarding with `isascii()` is enough for `uniq`,
whose argument is a flag letter; it is *not* enough for `look`, where `-d` (letters and digits
only) and `-f` (fold case) have to **mean** something for a Cyrillic dictionary — the rule that
port takes is that a byte above `0177` is a word constituent with no case. **Ask what the option
means for a multi-byte letter, not merely whether the call is in bounds.**

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
them: [../etc/rc](../etc/rc) prints the date now, which is the whole of what the boot script was
waiting for from this task. (It printed the motd as well until task 29b gave the system a
`login(1)` that prints it per session; the boot script's copy was then the second on the console
within a second or two, and went.) Five findings outlive it, and three of them are
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
[time/README.md](time/README.md) is the account; `od` and `sort` are flagged in
[TODO.md](TODO.md) for the same kind of trouble. **`dd` was too, and it was there**: the PDP-11's
512-byte disk block sat in the `ibs`/`obs` initialisers *and* in the `b` case of `number()`'s
suffix switch, which share no variable and are two hundred lines apart, so changing either alone
would have left the command language contradicting itself. Task C4b changed both together and
`_Static_assert`ed the third, `w`, which had been hiding as `sizeof(int)`.

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
`awk` that does not skip the preamble reads `Magic: 0xBE50006F11E5` as an object with no third
field, invents a uid-0 entry, and comes out one file ahead of the guest. **The guest was right
and the oracle was wrong**, which is the failure mode a recomputing oracle has and a checked-in
one does not; budget for debugging the oracle, not just the program.

---

## What task C4c taught

`/etc/mkfs` is on the image and [../kernel/test/mkfs.sh](../kernel/test/mkfs.sh) holds it
there. It is the first program here that *makes* a filesystem, the first that **writes** a raw
device at all, and the first thing on this system that has ever had two disks.
[mkfs/README.md](mkfs/README.md) is the account of the write path; four findings are general.

**A raw write has all four of C4a's conditions and a fifth of its own, and the fifth is about
where the buffer *lives*.** `physio()` never tests `rw` — it only ORs it into `b_flags` — so
alignment, count and seek behave identically in both directions. What it also refuses is a
`base` below `u_tsize`, which for a read means "do not scribble on the text" and for a write
means **a raw write may never be sourced from a string literal or a `const` array**. It cannot
fire today: `b6_prog()` links `FMAGIC` and `getxfile()` forces `u_tsize = 0` for that magic. It
goes live the day something that writes a device is linked **pure**, as `/bin/sh` already is,
and the symptom would be an `EFAULT` from a `write(2)` whose buffer looked perfectly aligned.
**When a path has only ever run one way, re-read its gate for the other direction** rather than
assuming symmetry.

**A second instance of a device can expose a bug that one instance cannot hold.** The disk's
sector header is written from a fixed buffer that belongs to the **controller**, not the drive,
and `kernel/dev/md.c` maintained exactly one word of it. With one drive that was an "open edge,
deliberately left"; with two it meant every zone the guest wrote onto the scratch pack carried
the *root* pack's volume number. It survived only because `b6fsutil` reads the volume out of
zone 0 alone and nothing wrote block 0 — which stopped being true when the superblock moved
there, so the driver now keeps the label per drive and `run-mkfs.sh`'s grep asserts it.
**Adding the second of anything is a test in itself.**

**An oracle can be byte-exact when the two implementations are transcriptions of each other.**
`b6fsutil -n` is the host's mkfs and `mkfs.c` is a transcription of it, so the comparison is
`cmp` and not a field diff — and the only thing standing in the way was a timestamp, which is
six bytes at a known offset and can be read back out of the guest's own output and fed to
`-T`. That is worth going after: a field diff says the fields it was taught to say, and `cmp`
says everything. The one discipline it needs is that the fixture start as **zeros** and that
the program not zero anything it need not — `mkfs` deliberately does not clear the data area,
which `mkfs.1m` lists under BUGS and which is exactly what keeps the comparison honest. Do the
byte comparison in the **cheap world first**: `cmd_mkfs_layout` under `b6sim` settles the
layout in a tenth of a second with a diffable failure, and the two-minute boot afterwards is
then about the device and nothing else.

**A kernel constant a program must respect but must not duplicate can be turned into a
probe.** `MDNBLK` lives only in `kernel/dev/md.c` and no header exports it, so `mkfs` reads the
**last block before writing the first** and lets `mdstrategy()` refuse it — one exchange, three
different failures caught with one diagnostic. That is safe only because the superblock is
written **last**, so a run that dies partway leaves a volume with no magic rather than a
plausible wreck. **Commit-last buys you the right not to check first**, and the two together
are cheaper than a constant in two places.

---

## What task C4d taught

`/etc/fsck` is on the image and [../kernel/test/fsck.sh](../kernel/test/fsck.sh) holds it
there. It is the first program here that *repairs* a filesystem, and the first that writes a
filesystem it did not make. [fsck/README.md](fsck/README.md) is the account; four findings
generalize.

**A second implementation is the best oracle there is, and it earns its keep by
disagreeing.** `cmd/fsutil/check.cpp` had implemented the same checks on the host for three
tasks before this one, in C++, sharing no line with the guest — so a damaged filesystem could
be handed to both, and every disagreement was a bug in one of them. Four turned up, three of
them in `fsck`: a free-inode count one too high on every clean filesystem this system has ever
made, a superblock magic number v7's `fsck` never looked at, and a reconnected file that could
have been left unreachable by `namei()`. **Where two implementations of the same job already
exist, the test to write is the one that makes them argue.**

**A test that repairs must assert that there was something to repair.** Damaging a fixture and
requiring a program to fix it is only meaningful if the damage was real, and a spec that has
drifted out of step with its fixture produces a test that repairs nothing and passes — with no
symptom, a clean image repaired to a clean image looking exactly like success. So every case
requires the *other* checker to fail first and succeed afterwards, and requires a second run to
find nothing, a repair that is not idempotent not having finished. **Assert the precondition,
not only the result.**

**A deliberate disagreement is marked, not hidden.** Two fields the tools differed about on
purpose — counters this port maintained nowhere — carried a `hostblind` marker in the harness,
written so that the case would fail the day the disagreement stopped being true. It did: the
kernel took `s_tfree`/`s_tinode` up, the marker went, and the harness printed the instructions
for removing itself. That is the difference between a known gap and an unknown one, and it
cost three lines of shell.

**A tool that builds things has to learn to break them.** There was no way to corrupt an image
from `b6fsutil`'s command line; the recipes existed only as C++ inside its own gtests, where a
guest program cannot reach them. The `-D` verb is the answer, and its one design decision worth
repeating is that the targets are **symbolic** — `sb.nfree`, `i5.nlink`, `e3.2` — resolved
through the tool's own word-offset tables, because a shell script computing the byte offset of
an inode field would put the on-disk layout in a second home. §4's rule about not re-deriving
constants applies to test scripts as much as to C.

---

## What task C4e taught

`/etc/icheck`, `/etc/dcheck`, `/etc/ncheck` and `/etc/clri` are on the image — the four
one-job tools `fsck` grew out of, and the first task here whose whole assertion is
`b6sim`'s. [icheck/README.md](icheck/README.md) is the account; five findings generalize.

**A field that is inert upstream can be load-bearing here, and reading the diff will not
show it.** v7's `icheck -s` writes the superblock's free-inode count as zero. That is
harmless in v7, where nothing maintains the field — v7's own `filsys(5)` calls both counters
uncurrent — and it is *fatal* here, because task C4d put `kernel/alloc.c` in charge of them
and taught `b6fsutil -c` to fault an image on either. So the salvage would have produced,
every time, a volume the host's checker rejects. Nothing in the v7 source says the field
matters, because there it does not; what caught it was the oracle. **When a port has made
something live that was dead upstream, grep the new sources for it rather than trusting the
reading.**

**"Share the buffer" and "one buffer per level" are each wrong in the other's program, and
the question that decides it is whether the walk re-enters itself.** `fsck` shares one
indirect buffer and re-fetches it every iteration, because its directory walk re-enters
`iblock()` from inside a parent's and two walks of a level are live at once. `icheck`'s walk
is a closed nested loop, so a buffer per level is safe — and `fsck`'s idiom there would
re-read the outer indirect block on all 512 inner iterations. Both files carry a comment
pointing at the other, because each looks like a mistake from the other's side.

**A table sized for another machine's i-list is worth re-deriving rather than carrying.**
v7's `ncheck` has a fixed 2503-entry hash table, which is 12,515 words here — 44% of the
address space — on a volume whose i-list can never exceed about 1,024 entries, so three
fifths of it was unreachable by construction. Sized from the superblock and indexed by
i-number it needs no hash, no probe loop and no `out of core -- increase HSIZE` exit, and it
fails at startup rather than partway through. The general form is §6's: **a fixed table is a
ceiling somebody chose against different hardware; ask what bounds it here before keeping
the number.**

**A program whose success is the checker failing needs its harness written backwards.**
Every case in `cmd/fsck/test` requires `b6fsutil -c` to pass after the guest has run.
`clri`'s job is to *make* a filesystem inconsistent, so the same assertion inverted — the
host's checker must fault the volume afterwards — is the whole oracle, and the case that
does it also has `icheck` and `dcheck` say what was done. **Ask what the program is for
before copying the neighbouring test's polarity.**

**A cross-program case belongs in the last-configured directory.** `make test` builds
`build_tests` and nothing else, and a test directory can only hang a program on that target
if the program's target already exists — so a case needing three of the four had to live in
`cmd/icheck/test`, `cmd/clri` and `cmd/dcheck` being added to the top-level `CMakeLists.txt`
before it. That is a two-minute discovery and an easy one to make late.

---

## What task C4f taught

`/etc/mount` and `/etc/umount` are on the image and
[../kernel/test/mount.sh](../kernel/test/mount.sh) holds them there. It is the last of task
C4 and the first thing in this tree that ever called `mount(2)`: `smount()` had been compiled
into every `unix` this port built and had no caller, `mount[1]` had never been filled, and
`bdevsw[0]` minor 1 had never carried a block. [mount/README.md](mount/README.md) is the
account; four findings generalize.

**A byte count computed from a field width is not a struct's size here, and nothing
diagnoses it.** v7's `/etc/mtab` is a binary record blitted out of a
`struct { char file[32]; char spec[32]; }` with the length written by hand as `2 * NAMSIZ`.
A `char[32]` occupies ⌈32/6⌉ = 6 words = 36 char-units on this machine and the next member
starts on a word boundary, so `sizeof` that struct is **72 and not 64**: the program writes
64-byte records out of 72-byte objects and every entry after the first comes back eight bytes
out of step. It is §2's machine seen from the layout side rather than the pointer side, and
the search is different: **grep for a `read`/`write` whose length is arithmetic rather than
`sizeof`.**

**A hazard in code that exists only to serve a file format can be deleted by changing the
format.** Both programs' `char *` comparisons — five of them, none previously in §2's table —
were in the basename stripping and the fixed-width copy into that record. Making `/etc/mtab`
text removed the record, and all five went with it; nothing had to be rewritten as an index
count. This is only available when the format is the program's own business, which `/etc/mtab`
is and an on-disk inode is not, but it is worth asking before rewriting a loop.

**A whole harness can have nothing to say about a program, and that has to be said out
loud.** §9's rule is to test in both worlds where a program runs in both. These two do not
run in `b6sim` at all: `mount(2)` there is the *host's*, so a case that reached it would ask
the build machine to mount something. Four cases assert the argument handling and stop, which
is only possible because the port settles its arguments before opening `/etc/mtab` — a
reordering of v7 made for the test. `ctest -L cmd` therefore says nothing about the subject
of this task, and the `kernel/test` case is not the second opinion but the only one.

**A recomputed oracle is a measurement of an instant, and the instant has to be chosen.**
`run-mount.sh` holds the guest's free count against the host's walk of the pack that came
back. The obvious reading — `df` through the mount — is *wrong* for that comparison by one
block, because `clri` threw a file away and `fsck` reclaimed its blocks afterwards; the two
numbers are true measurements of different moments. The mounted reading is asserted as a
literal by the log diff instead, and the recomputed comparison uses a final `df` taken after
everything. **Ask what instant an oracle measures**, which is the sharper form of C4a's rule
about which kind of number it holds.

---

## What task C5a taught

`wc`, `cmp`, `sum`, `tee`, `split` and `rev` are on the image — the first six of the text
filters, and the first task here whose whole assertion is `b6sim`'s **by choice** rather than
by oversight. [rev/README.md](rev/README.md) is the account of the one divergence in the set;
five findings generalize.

**A task can have no §2 in it at all, and a survey cannot tell you that.** Six v7 filters, 487
lines of buffer arithmetic, and not one `char *` relational between them — the second negative
result in §2's table, and a more surprising one than C4e's, because these programs *do* walk
buffers where `icheck` and its three only walked block numbers. `tee` is the proof of the rule
that came out of it: it is the one of the six with a cursor and it uses `int` indices, because
it is **copying** rather than **deciding**. So **byte cursors arrive with parsing and not with
byte handling** — the compiler has since made them harmless, but the shape still says where a
program keeps its state: `sort`'s fifteen are all inside `cmp()`, `grep`'s three bound a
compiled expression, `sed`'s three bound `genbuf`.

**A width dependence that is *not* there is worth a comment, because the next reader will look
for one.** `sum`'s checksum is sixteen bits computed in an `unsigned` that is 41 bits here and
was 16 on a PDP-11, and it comes out bit for bit identical — but only because v7 masks with
`0xFFFF` *inside* the loop rather than leaning on the register's width. Nothing about the code
says "this is why I am portable"; the mask reads as belt and braces. `sum.c`'s header now says
it, and the general form is that **the interesting thing about a port is sometimes what did
not have to change, and that is exactly what a diff cannot show.**

**§4 reaches programs that are not filesystem tools.** `sum` prints a block count, and the
constant it divided by was `BUFSIZ` — which was 512 on a PDP-11 and *was also* its `BSIZE`, so
the number named a filesystem block by coincidence rather than by design. Here `BUFSIZ` is
3072 and so is `BSIZE`, so the coincidence still holds and the division still named a real
block; what did not hold is that a number **reported to a user** is in 1024-byte blocks on
this machine, which is what `df`, `du`, `quot` and `ls -s` were all taught in C4a. The rule
generalises past the four programs that motivated it: **ask what unit a number is in whenever
a program prints one, not only when the program's subject is the disk.**

**An oracle over a program's *files* is a separate harness, and it is cheap.**
`b6_progtest()` diffs standard output and checks an exit status, which is everything for `wc`
and `cmp` and nothing at all for `tee` and `split`, whose entire job is what they create. The
answer is the shape `cmd/mkfs/test/run-mkfs-test.sh` already had: a custom `add_test` that
runs the guest under `b6sim` and then lets the **host** look at the directory afterwards —
[tee/test/run-tee-test.sh](tee/test/run-tee-test.sh) and
[split/test/run-split-test.sh](split/test/run-split-test.sh). Two things it made possible that
no stdout diff could: that `tee -a` really *appends* (the file must be the input twice, which
only a seek to the end can produce — a dropped `lseek` leaves a file that still compares equal
to the input), and that `split` refuses its 677th piece after writing exactly 676. `col`,
`sed -n w` and `sort -o` all inherit it.

**And the fixture goes in the build directory, not through `@srcdir@`, whenever the program
prints the name it was given.** `run-prog-test.sh` substitutes `@srcdir@` into a `.args` file,
which is right for a program that only reads what it is pointed at — and wrong the moment the
program *echoes* the path, because an absolute build path then lands in a checked-in
`.expected` and the case passes on one machine. `wc`, `cmp` and `sum` all print filenames, so
every fixture here is copied into the test's binary directory and named relatively. It is two
lines of CMake and it is the difference between a portable case and a local one.

**Three upstream bugs, and one of them was a hang.** `split -0` did not terminate — a zero
count makes the per-piece loop zero-trip, so nothing is read, nothing is opened, and the
program goes round forever — which is more than a bug: §9 says a program that does not
terminate cannot be a `b6sim` case at all, so in the shape v7 left it the ctest would have
*hung* rather than failed, and the whole directory would have had to be written around it.
`split`'s output-name buffer and `tee`'s twenty-entry descriptor table were both unbounded
against `argv`, the second writing straight into the three variables that follow it. **A
program that hangs on an argument is worth grepping for before designing its tests**, and the
three cheap places to look are a loop bound taken from a number the user supplied, a `fclose`
of something that may be null, and a `goto` back over both.

---

## What task C5b taught

`tr`, `uniq`, `comm`, `tail`, `od`, `look` and `col` are on the image, `/bin` holds forty-three
entries, and [../kernel/test/filters.sh](../kernel/test/filters.sh) holds all **thirteen** of
task C5 there — the booted pass C5a deferred, closed here. [od/README.md](od/README.md) and
[col/README.md](col/README.md) are the two accounts. Six findings generalize.

**A flag can keep its meaning while the thing it names changes underneath it, and that is the
cheaper redesign.** `od`'s `-o`, `-d` and `-x` meant *the machine word, in this radix* on a
PDP-11 and mean exactly that here; nothing about them was redefined, and what moved is only the
width of the column — 6 octal digits to 16, 5 decimal to 15, 4 hex to 12. The tempting
alternative was to make `-o` byte-sized so that the new `-w` could be the word, and that would
have been the one change that really *did* break a v7 flag. **Ask whether the definition or the
machine moved** before renaming anything; `-w` is a synonym and costs nothing.

**A truncation that is silent is worse than a limit that is loud, and v7's `od` had one in the
one program that could hide it best.** `putn()` recursed exactly as deep as the field was wide
and discarded every digit above it, so a 48-bit word through `putn(n, 8, 6)` printed its low six
octal digits and stopped — a plausible, wrong number, in a program whose entire output is numbers
nobody can check by eye. It prints at least the field width and never fewer digits than the
value needs now. **Where a program formats into fixed columns, ask what it does when the value
does not fit**, and prefer a misaligned column to a quiet lie.

**So the oracle has to be a second implementation when the output is unreadable.** Every one of
`od`'s expectations was computed by a Python implementation of the same packing, written from
`od.1` rather than from `od.c`. That is task C4d's rule — the best oracle is one that can
disagree — reached from the opposite direction: not because two implementations already existed,
but because *no reviewer could check the first one*. `mkfs`'s `cmp` against `b6fsutil -n` is the
stronger form of the same thing, and `sort` and `pr` are the two below with the property that
demands it.

**A v7 feature that steals a bit must be re-examined on a machine whose text uses that bit, and
the question is what still produces input for it.** `col` packs its Greek half-shift into bit
`0200` of every stored character and masks its input with `0177`. Keeping it was possible — `ed`'s
`QESC` prefix is the precedent for moving such a flag out of the character — and it was rejected
because there is no Model 37 Teletype here, no way to attach one, and no *producer*: v7's `col`
filters `nroff`, and there is no `nroff` in this source tree at all. That is `getty`'s cut to
the speed table, and the third deliberate divergence in `cmd/` after `touch` and `rev`.

**And the failure a faithful port would have produced was not the obvious one.** The reading
that "the mask deletes the text" is wrong: `привет` masked with `0177` becomes `P ? Q \0 P 8 P 2
P 5 Q \2`, and the printability test drops only the two that fell below a space — so v7's `col`
prints `P?QP8P2P5Q`, ten bytes of plausible ASCII, two characters shorter than what went in.
**Not an empty line and not a diagnostic: a wrong answer shaped like a right one.** It was
asserted only because the byte arithmetic was worked out rather than assumed, and the first
draft of this section claimed the output was empty. C1's rule — claiming more than the fix does
is worse than carrying the bug — applies to describing the bug too.

**A program can want a data file, and that turns the harness question into a design question.**
`look`'s default is the absolute path `/usr/dict/words`, which under `b6sim` is the **build
machine's** — on macOS it does not exist at all. So either the one-argument form, which is the
form `look` mostly exists in, goes untested, or the dictionary joins the image. It joined:
`cmd/look/words` is a thousand sorted entries with a Cyrillic tail, staged by
`cmd/look/CMakeLists.txt` exactly as `etc/` stages `/etc/passwd`, and asserted under the booted
kernel and nowhere else. §9's rule — when a program names a fixed absolute path, ask whose it is
— has been cited five times now; this is the first task where the answer was to **give the guest
one of its own**.

## Where task C5a stopped, and what closed it

**C5a's six were asserted under `b6sim` alone**, deliberately, and this file recorded the
deferral rather than leaving it to be discovered — task C4e's lesson. **Task C5b closed it**:
`kernel/test/filters` runs all thirteen in one boot at volume 3097, and five things it says are
unreachable under `b6sim`, which is what makes it more than a second opinion:

* **`look` against its default dictionary**, the one assertion the simulator is barred from.
* **`tail` on a pipe.** It probes descriptor 0 and branches on `ESPIPE`; under `b6sim` standard
  input is *always a file*, `run-prog-test.sh` redirecting one, so half that branch was
  unreachable there. **A harness limitation can hide a whole code path rather than a whole
  program**, which is a quieter gap than C4f's and worth looking for by grepping a candidate
  for `ESPIPE`, `isatty` and `fstat` on descriptor 0.
* **`col`'s two diagnostics**, for the widened `argv[0]` reason in §9.
* **Six pipelines between one filter and another**, which is how anybody uses them and which no
  single-program harness can represent.
* **And that the image's copy of C5a's six executes at all** — which the two `ls /bin` listings
  assert the *existence* of and not the execution of.

**Nothing in that test is masked**, which is the difference from `kernel/test/utils` and is
worth stating as a target rather than an accident: `utils` masks `date`'s seconds and `time`'s
intervals because those two print what the clock and the host decided, and every number in
`filters.log` is computed by a filter from a corpus the script writes for itself in the same
run. A test whose oracle needs no projection is one whose subject is deterministic; **if a
filter ever needs a mask here, that is a finding about the filter.**
