# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. It is the companion of
[../kernel/TODO.md](../kernel/TODO.md), which carries the kernel's own list, and it starts where
that one's task 24 left off: the machine boots, mounts a root filesystem, execs `/etc/init` and
gives a shell prompt.

**[README.md](README.md) beside it is the reference** — what is already in this directory, the
porting recipe, the hazards a v7 source walks into on this machine, how a program gets onto the
image and which harness tests it. Read it before starting any task below; **nothing here repeats
it**, and a task names only what is unusual about itself. **A bare `§N` below is a section of
that file's porting recipe** — §2 the `char *` ordering hazard, §4 the 3072-byte block and the
1024-byte one reported in its place, §6 the
address-space ceilings, and so on.

**Tasks C1, C2, C3 and C4a are done and their writeups have been removed**; what each taught is
README.md's four closing sections. Thirty commands are on the image — twenty-nine entries
in `/bin`, since `[` is `test` under a second name, plus `/etc/getty` and `/etc/quot` beside them
— so the tree
can be built, rearranged and re-permissioned from the console, the machine can say what time it
is, wait and signal, a shell script can finally **branch**, and since kernel task 29b there is
a `login:` prompt on each Consul and a shell that need not be root's. **And since C3 the machine
can write its own text**: `ed` is on the image, so a shell script, an `/etc/ttys` or a C source
need no longer come from the build host — which is the precondition for C9 meaning anything.
**And since C4a it can measure its own store**: `df`, `du` and `quot` read the superblock, a
directory tree and the whole i-list, the first two off the raw device through `physio()` —
which turns out to have four alignment rules a v7 program knows nothing about
([df/README.md](df/README.md)).
[../etc/rc](../etc/rc) is a boot script that does something: it prints the motd and then the
date, which is a literal to the minute because the boot clock is the image's own `-T` stamp,
and `kernel/test/console` asserts both. What it
still wants is what the file itself now names — `fsck` and `mount` (C4), `cron` and `update`
(C10) and `accton` (C8). The `rm -f /tmp/*` line is a fifth, and it is now waiting on a
*kernel* task rather than on this file: `ed` has arrived and really is the first program that
writes to `/tmp`, but §7 step 4 gives a line in `/etc/rc` exactly one home for its assertion —
`kernel/test/console` — and that test is `DISABLED` (kernel task 35). An unasserted line in the
boot script three tests walk through is worse than an honest deferral, so `etc/rc` says so and
the line comes back with task 35.

**Task numbers carry a `C`** — `C2a`, `C4d`, … — because `kernel/TODO.md`'s 1–34 are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The
numbering is **left as it was** when a task is finished and dropped.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it
compiles.

| | task | what it buys | size |
|---|---|---|---|
| C4 | filesystem maintenance — ~~`df` `du` `quot`~~ `dd` `mkfs` `fsck` `icheck` `dcheck` `ncheck` `clri` `mount` `umount` | a system that maintains itself | large; C4a done |
| C5 | the text filters — `wc` `cmp` `sum` `tee` `split` `rev` `tr` `uniq` `comm` `tail` `od` `look` `col` `grep` `fgrep` `sort` `sed` `pr` `diff` `cal` `tsort` `join` `find` `file` | the corpus everything else is tested against | medium ×24 |
| C6 | multiuser userland — `passwd` `su` `newgrp` `stty` `who` `write` `wall` `mesg` `mail` | more than one person | medium; unblocked |
| C7 | `tar` | getting data on and off without `b6fsutil` | medium |
| C8 | inspection — `ps` `dmesg` `pstat` `iostat` `nice`, `ac` `sa` `accton` | seeing what the machine is doing | medium; needs `nlist(3)` |
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` | a system worth using | open-ended |

C4 is still the one that matters, and it has started: C4a gave the machine the three programs
that *measure* its store, and what is left of C4 is the half that *changes* it — `mkfs` and
`fsck` above all. C5 is cheap and pays for itself in test coverage. C7 is one program and can be
taken at any time; C6, C8 and C9 are each gated on something the task names.

---

## C4. Filesystem maintenance

**Why.** The system cannot repair or extend itself. `kernel/test/session` fscks the image *on the
host*, with `b6fsutil`; the machine itself cannot make a new filesystem and cannot mount anything.

**C4a is done and its writeup has been removed.** `df`, `du` and `quot` are on the image — the
first three programs here that can say anything about the store they live on — and
`kernel/test/fsinfo` (volume 3087) holds them there. Two things it settled are the property of
every task below, and **[df/README.md](df/README.md) is the account of both**:

* **A raw read through `/dev/rmd0` obeys four conditions**, three of which fail with `EFAULT` and
  the fourth of which is silent: byte #0 of a word, a whole number of `BSIZE`s, a buffer whose
  *word address* is a multiple of `MDTRACK` (= `BSIZEW`, `mdstrategy()`'s half-zone), and a
  block-aligned seek. `(int)ptr` on a word pointer is the alignment idiom, and obeying the rules
  cost nothing: it deleted `quot`'s 4,096-word `itab[256]`, one block already being `INOPB`
  inodes.
* **A number about the whole image is recomputed by the host, not checked in.** `run-fsinfo.sh`
  derives `df`'s answer from `b6fsutil -c -v` and `quot`'s from an `awk` over `b6fsutil -v -v`,
  so nothing needs updating when a program joins `/bin` — and both programs report to the
  *console*, because a program that measures a filesystem cannot write its answer into one.

`df` and `quot` also gave `b6sim` its first fixture **filesystem** ([df/test/](df/test/)): they
read a file, and a flat `b6fsutil` image is one. C4c and C4d should copy that rather than build a
second.

Everything below still encodes the on-disk layout, and there is a rule for that:
**`_Static_assert` against `<sys/param.h>` rather than re-deriving the constants**, which is what
[fsutil/params.cpp](fsutil/params.cpp) does on the host side and why a kernel that retunes `INOPB`
or `DIRSIZ` breaks the build instead of the images. (A guest program needs none of that file's
machinery: it includes the real headers and inherits their assertions. `params.cpp` is elaborate
only because `fsutil` is host C++ and cannot.) The raw devices are on the image — `/dev/rmd0` and
`/dev/rmb0`, `cdevsw[3]` and `[4]`.

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

**Size.** Large overall; C4b and C4f are each small, C4d is the whole weight.  C4a was the small
one, and what it cost was not its line count -- see df/README.md.

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

**The `CCL` bitmap lives here, and this task inherited the warning C3's brief carried by
mistake.** `grep.c` packs a character class into 128 bits — sixteen bytes, addressed
`1 << (c & 07)` at `c >> 3` — and so does `sed`. That is byte work inside a fat-pointer buffer,
it is exactly the shape §2 describes, and **it has to become 32 bytes** if a class is to hold a
byte above `0177`, which §11 says it must. `ed` was thought to do the same and does not: its
classes are an enumerated byte list with a count byte, so it was byte-capable already and needed
none of this. See [ed/README.md](ed/README.md), which says what that mistake cost.

### C5d. `sort`

`sort.c` (903). The heavyweight of the phase: `sbrk`, eight `signal` calls for temp-file cleanup,
its own merge over temp files, and **the worst concentration of §2 in the tree — fifteen `char *`
comparisons, every one of them inside `cmp()`, which is the routine that decides the program's
entire output.** The record arena around it is `char **` and is fine, which is exactly what makes
this one dangerous to skim. Its 28,672-word fit should be measured early. Do it after C5a–C5c, so
the harness is established when the hard one arrives.

### C5e. `sed`

`sed/` (1,690 lines: `sed0.c`, `sed1.c` and `sed.h`). The same regex family as `ed`, and **C3 is
done**, so [ed/README.md](ed/README.md) is the thing to read first: the three `char *`
comparisons in `sed1.c` are the same `genbuf` bound `ed` had, and the `QESC` prefix that replaced
bit `0200` in `ed`'s replacement text is the pattern for `sed`'s. But `sed` **does** have the
`CCL` bitmap `ed` turned out not to (see C5c), so budget for widening that here rather than
expecting the `ed` diff to have covered it.

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

**Unblocked, and half of it is already done.** Kernel 29a gave the image a second terminal
(`/dev/tty1`, `dev/sc.c` driving both Consuls) and kernel 29b put the userland on top of it:
[getty/](getty/) and [login/](login/) are ported and on the image, `/etc/ttys` is staged from
[../etc/](../etc/), `/etc/passwd` carries a real encrypted field, `init`'s `merge()`/`multiple()`
half runs at last, and `kernel/test/login` logs in and out over the console. **Do not duplicate
that**: read [getty/README.md](getty/README.md) and [login/README.md](login/README.md) first —
between them they cover the speed table this machine does not have, the privilege order that must
not be tidied, and the stdio change that makes a prompt with no newline invisible.

What remains is the rest of the manual pages that assume more than one person:

* `passwd.c` (172), `su.c` (52), `newgrp.c` (57) — the account trio, and the first two are where
  the setuid bit `login` deliberately does **not** carry has to go. `passwd` needs a writable
  `/etc/passwd`; both need the `crypt` libc already has and `login` has now exercised.
* `stty.c` (303) — reads and writes the `sgttyb` the kernel's `sc.c` implements. Its capability
  list must be cut down to what that driver actually honours rather than carried whole, which is
  the same cut [getty/README.md](getty/README.md) records making to the speed table, and for the
  same reasons: no baud rate, no parity, no delays, no LCASE.
* `who.c` (64), `write.c` (186), `wall.c` (70), `mesg.c` (57) — the social four, all `/etc/utmp`.
  That file now exists and is written: `init`'s `merge()` creates it and `login` writes a record,
  and `lib/test/ttyt` asserts the `ttyslot(3)` they all index it by. `who` is the cheapest of the
  four and the first one worth doing, being the only reader `/etc/utmp` still lacks.
* `mail.c` (556) — only if `/usr/spool/mail` is wanted. `login` already probes for it with
  `access()` and quietly finds nothing.

**Size.** Medium, and mostly mechanical now that the terminal, the accounts and the login path are
all proven.

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
`prf.c` does not do yet, so it carries a small kernel task with it; `nice.c` (28) — trivial,
independent of `nlist`, and takeable on its own at any time; `pstat.c` (385) and `iostat.c` (289)
— both deeply tied to kernel structures and both worth rewriting rather than porting; `ac.c`
(251), `sa.c` (489), `accton.c` (16) — process accounting, which the kernel's `acct()` supports,
and which nothing needs. `accton` is one of the five [../etc/rc](../etc/rc) still names, and the
only one of them this task owns.

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
| `at.c`, `atrun.c`, `cron.c`, `calendar.c` | scheduling | 307 + 110 + 254 + 54 | want a running multiuser system and a correct clock; after C6. `cron` is one of the five [../etc/rc](../etc/rc) still names |
| `update.c` | periodic `sync` | 38 | trivial, and [../etc/rc](../etc/rc) names it — but it is a **daemon**, and `/etc/rc` runs on every pass through `init`'s loop, so weigh a second copy per pass before adding the line |
| `strip`, `size`, `nm` | | | **not these** — see C9 |

---

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` alone is larger than everything in C1–C4 together, it drives a CAT phototypesetter that does not exist, and **there is no `nroff` in this source tree at all** — only `troff`. This repo's own manual pages are read with the *host* `nroff`, which is the right answer for the foreseeable future. `spell` additionally needs its whole word list. |
| `tp/`, `dump.c`, `restor.c`, `dumpdir.c` | 800 + 641 + 1,150 + 475 | Tape. **This kernel has no tape driver** and no `bdevsw`/`cdevsw` row for one, and all four are built around a tape's sequential access rather than merely willing to use it — `dump`/`restor` are a filesystem-level backup pair whose whole design is the reel. `tp` is the pre-`tar` archiver and is superseded by it in any case. If a magnetic-tape driver is ever written (a `kernel/TODO.md` item nobody has raised; [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) is the reference), reconsider `dump`/`restor` and not the other two. |
| `uucp/`, `cu.c` | 6,415 + 541 | Dial-out over a modem link nothing models. `cu` becomes conceivable only if the machine's serial multiplexor is ever driven and wired to something outside; no kernel task proposes that. |
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

C4b or C4c, or C5 if a week is what is free rather than a month.

C4a has landed, so the guest can now *examine* its own store — `df`, `du` and `quot`, with the
raw-device path proven and a fixture-filesystem harness under `b6sim` that C4c and C4d inherit.
What it still cannot do is *make* a filesystem or *repair* one: `kernel/test/fsinfo` measures the
image the guest is living on, but it is still `b6fsutil` on the host that fscks it. C4c before
C4d, because a `fsck` with nothing to fix is only half tested — and C4b (`dd`) before either, it
being how anything gets copied to or from a raw device now that the rules for reading one are
written down.

C5 stays the cheap one, and the harness for it is now **complete**: `b6_progtest()` needs no
boot, and C3 gave it the `<case>.in` that C2b's writeup said the filters would want, so a filter
can finally be fed. README.md §9 records what is left that it cannot do — two things now, not
three.
