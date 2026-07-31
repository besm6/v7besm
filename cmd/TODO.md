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

**Tasks C1, C2, C3 and C4a–C4e are done and their writeups have been removed**; what each taught
is README.md's seven closing sections. Thirty-seven commands are on the image — thirty entries
in `/bin`, since `[` is `test` under a second name, plus `/etc/getty`, `/etc/quot`, `/etc/mkfs`,
`/etc/fsck`, `/etc/icheck`, `/etc/dcheck`, `/etc/ncheck` and `/etc/clri` beside them
— so the tree
can be built, rearranged and re-permissioned from the console, the machine can say what time it
is, wait and signal, a shell script can finally **branch**, and since kernel task 29b there is
a `login:` prompt on each Consul and a shell that need not be root's. **And since C3 the machine
can write its own text**: `ed` is on the image, so a shell script, an `/etc/ttys` or a C source
need no longer come from the build host — which is the precondition for C9 meaning anything.
**And since C4a it can measure its own store**: `df`, `du` and `quot` read the superblock, a
directory tree and the whole i-list, the first two off the raw device through `physio()` —
which turns out to have four alignment rules a v7 program knows nothing about
([df/README.md](df/README.md)). **And since C4b it can move that store about**: `dd` is on the
image, so bulk data reaches `/dev/rmd0` and comes back without `b6fsutil` on the host — and it
does so at the *default* record size, which had to become the 3072-byte block, v7's 512 not
being a whole number of words and so refused by `physio()` before the driver ever sees it.
**And since C4c it can make one**: `/etc/mkfs` writes a filesystem onto `/dev/rmd1` — this
system's first raw *write*, `kernel/dev/md.c`'s `mdwrite()` having been dead code until it,
and the first time the machine has had two drives at all ([mkfs/README.md](mkfs/README.md)).
**And since C4d it can repair one**: `/etc/fsck` takes a volume that is wrong, works out what
is wrong with it and puts it right over that same write path — and then reads the filesystem
the machine is *running on* and pronounces it sound, which is the one thing in C4 that no
host tool can do at all ([fsck/README.md](fsck/README.md)). **And since C4e it can do each
of those jobs on its own** — `icheck`, `dcheck`, `ncheck` and `clri` — of which the one
worth naming is `ncheck`: it puts a **name** to an i-number, which nothing else on this
image can do, and which is what `quot -n` has been waiting for since C4a
([icheck/README.md](icheck/README.md)).
[../etc/rc](../etc/rc) is a boot script that does something: it prints the motd and then the
date, which is a literal to the minute because the boot clock is the image's own `-T` stamp,
and `kernel/test/console` asserts both. What it
still wants is what the file itself now names — `mount` (C4), `cron` and `update`
(C10) and `accton` (C8). An `fsck` line is a fourth, and it is C4d's one loose end: the
program exists, and the line that would run it at boot has the same single home for an
assertion as the `rm -f /tmp/*` line below and the same reason for waiting. That `rm -f /tmp/*`
line is waiting on a
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
| C4 | filesystem maintenance — ~~`df` `du` `quot` `dd` `mkfs` `fsck` `icheck` `dcheck` `ncheck` `clri`~~ `mount` `umount` | a system that maintains itself | large; C4a–C4e done |
| C5 | the text filters — `wc` `cmp` `sum` `tee` `split` `rev` `tr` `uniq` `comm` `tail` `od` `look` `col` `grep` `fgrep` `sort` `sed` `pr` `diff` `cal` `tsort` `join` `find` `file` | the corpus everything else is tested against | medium ×24 |
| C6 | multiuser userland — `passwd` `su` `newgrp` `stty` `who` `write` `wall` `mesg` `mail` | more than one person | medium; unblocked |
| C7 | `tar` | getting data on and off without `b6fsutil` | medium |
| C8 | inspection — `ps` `dmesg` `pstat` `iostat` `nice`, `ac` `sa` `accton` | seeing what the machine is doing | medium; needs `nlist(3)` |
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` | a system worth using | open-ended |

C4 was the one that mattered and its weight is now behind it: C4a gave the machine the three
programs that *measure* its store, C4b the one that *moves* it, C4c the one that *makes* one,
C4d the one that *repairs* one and C4e the four that do each of those jobs on its own. What is
left of it is `mount`/`umount`. C5 is cheap and pays for itself in test coverage. C7 is one
program and can be taken at any time; C6, C8 and C9 are each gated on something the task names.

---

## C4. Filesystem maintenance

**Why.** The system could not maintain itself. It can now measure its store (C4a), move it
(C4b), make a filesystem (C4c), repair one (C4d) and do each of `fsck`'s jobs standalone
(C4e) — what is left is the pair that would let it mount anything.

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
read a file, and a flat `b6fsutil` image is one. `dd`, `mkfs` and `fsck` all inherited it, the
last building a fixture of its own — `fsck/test/fsckimg.manifest` says why df's could not be
extended, and it is the general answer: a manifest whose free count is asserted as a literal
somewhere is a manifest nothing may add a file to.

**C4b is done and its writeup has been removed too.** `/bin/dd` is on the image and
`kernel/test/dd` (volume 3088) holds it there, so bulk data can move to and from `/dev/rmd0`
without `b6fsutil` on the host. Three things it settled that the rest of C4 inherits:

* **A raw record is `BSIZE`, and a program's *default* has to say so.** `dd`'s `ibs`/`obs`
  default to `BSIZE` and its `b` suffix multiplies by `BSIZE`, where v7's were 512 — because
  512 is not a whole number of words, so `physio()` refuses it before the driver sees it and
  v7's defaults could not reach the disk at all. `w` became `NBPW` for free, `sizeof(int)`
  being 6 here, so `1b`, `3k` and `512w` now name the same thing. Any later program with a
  record size of its own faces the same choice; `dd.1` is the worked example of writing a
  divergence down twice.
* **A wrong size is refused two different ways, and the diagnostic distinguishes them.**
  `physio()` answers `EFAULT` for a count that is not a whole number of words; `mdstrategy()`
  answers `EIO` for one that is a whole number of words but not a whole half-zone. Both are
  condition 2 above, enforced in two places, and `kernel/test/dd` is the first test here to
  assert a **refused** transfer at all.
* **The oracle can be the disk itself.** `run-dd.sh` compares what the guest pulled off
  `/dev/rmd0` against the same offset in the container the boot was handed — nothing checked
  in, nothing recomputed — at the cost of one assumption, that the run does not rewrite those
  blocks, which it checks rather than assumes. `mkfs` had the stronger version of this
  available — `b6fsutil -n` builds the same layout on the host — and C4c took it, twice.

**C4c is done and its writeup has been removed too.** `/etc/mkfs` is on the image and
`kernel/test/mkfs` (volumes 3089 root, **3090 scratch**) holds it there, so the machine can
make a filesystem rather than only measure and move one. It is the task that built the
raw-**write** harness `dd` was waiting for: there is a second drive now, `/dev/rmd1` and
`/dev/md1` are on the image, `mdwrite()` is live, and one test attaches a scratch volume.
**[mkfs/README.md](mkfs/README.md) is the account**, and three things it settled belong to
everything below:

* **A raw write has a fifth condition, and it is about where the buffer lives.** All four of
  C4a's apply unchanged — `physio()` never tests `rw` — and it also refuses a `base` below
  `u_tsize`, so a raw write may never be sourced from the text segment. It cannot fire for an
  `FMAGIC` program, which is what `b6_prog()` produces, and goes live the day anything that
  writes a device is linked **pure**.
* **The sector header is per *controller*, so a two-drive machine mislabels a pack.** `md.c`
  maintains only the self-address in the service words it writes; the magic mark and the
  **volume number** come from the last read of *any* drive on the controller. `b6fsutil`
  reads the volume from zone 0 alone, so the rule is that **nothing may write block 0** of a
  pack this system did not label. The first bug here that needed a second drive to exist.
* **Committing last is a substitute for a size check.** `MDNBLK` has no guest header and must
  not be duplicated, so `mkfs` reads the *last* block before writing the first and lets
  `mdstrategy()` answer. Safe only because the superblock is written last: a run that dies
  partway leaves a volume with no magic rather than a plausible wreck.

And the oracle turned out to be available **twice** — byte for byte against `b6fsutil -n`
under `b6sim` in a tenth of a second, where the layout is debugged, and again over the device
under SIMH, where nothing else is. The only obstacle either time was the timestamp, which is
six bytes at a known offset.

**C4d is done and its writeup has been removed too.** `/etc/fsck` is on the image and
`kernel/test/fsck` (volumes 3091 root, **3092 scratch**) holds it there, so the machine can
repair a filesystem rather than only measure, move and make one — and `/lost+found` is on the
root for it to reconnect into. **[fsck/README.md](fsck/README.md) is the account**, and two
things it settled were what made C4e cheap and are C4f's to use too:

* **`b6fsutil` can break an image on purpose now**, which C4d's brief asked for and which did
  not exist: `-D sb.nfree=999`, `-D i5.nlink=7`, `-D e3.2=0`, `-D b12.0=…`. The targets are
  **symbolic**, resolved through the tool's own word-offset tables, so no test script
  re-derives `itod()` or `INOPB` — the rule below about `_Static_assert` applies to shell as
  much as to C. It writes the raw word without `to_word()`'s range check and never `sync()`s;
  `cmd/fsutil/damage.h` says why both.
* **A test that repairs must assert that there was something to repair.** Every case here
  requires `b6fsutil -c` to *fail* before the guest runs and to *succeed* after, and requires
  a second `fsck` to find nothing. Without the first, a damage spec that drifted out of step
  with its fixture would leave a test that fixes nothing and passes, and nothing else would
  notice.

It also found four bugs by holding the two implementations against each other, three of them
in `fsck` itself: a free-inode count one too high on every clean filesystem this system has
ever made (inode 1 exists and can never be allocated), a superblock magic number v7's `fsck`
never looked at, and a reconnected file that could have been left unreachable by `namei()`.

**C4e is done and its writeup has been removed too.** `/etc/icheck`, `/etc/dcheck`,
`/etc/ncheck` and `/etc/clri` are on the image, so each of `fsck`'s jobs can be done on its
own — and the machine can at last put a **name** to an i-number, which nothing here could do
before and which `quot -n` has been waiting for since C4a.
**[icheck/README.md](icheck/README.md) is the account**, and three of its findings are
general. **A field that is inert upstream can be load-bearing here**: v7's `icheck -s` writes
`s_tinode` as zero, harmlessly, because nothing in v7 maintains it — and since C4d the kernel
maintains it and `b6fsutil -c` faults an image on it, so a salvage carried over unchanged
would have broken every volume it touched. **A shared-and-re-fetched buffer and one buffer
per level are each wrong in the other's program**, and the question that decides it is whether
anything in the walk re-enters it: `fsck`'s does and `icheck`'s cannot, so copying `fsck` here
would have re-read the outer indirect block 512 times per double indirect. And **`clri` is the
first program here whose success is the host's checker *failing***, which inverts the polarity
of every assertion in `cmd/fsck/test`.

**C4e's one loose end is that it has no SIMH test**, and that is a deliberate deferral rather
than an oversight. Everything asserted about the four runs under `b6sim`, whose `read(2)` and
`write(2)` are the host's, so **none of the five conditions of the raw path is exercised for
any of them** — and `clri` and `icheck -s` are the first programs since `mkfs` and `fsck` to
*write* a device. What would close it is a test on `kernel/test/fsck`'s shape at volumes
**3093** (root) and **3094** (scratch), with the scratch pack attached *without* `-n`, `clri`
and `icheck -s` pointed at `/dev/rmd1`, and a read-only pass over the live root last. Two
things it must know before it is written are in
[icheck/README.md](icheck/README.md) §5: `icheck -s` and `clri` **stop the machine** on a hot
root by design, so pointing either at `/dev/rmd0` is a 1,800-second timeout with no
diagnostic; and anything that measures the mounted root goes below the `sync` and writes only
to `/dev/console`, which is `kernel/test/fsck.sh`'s rule.

Everything below still encodes the on-disk layout, and there is a rule for that:
**`_Static_assert` against `<sys/param.h>` rather than re-deriving the constants**, which is what
[fsutil/params.cpp](fsutil/params.cpp) does on the host side and why a kernel that retunes `INOPB`
or `DIRSIZ` breaks the build instead of the images. (A guest program needs none of that file's
machinery: it includes the real headers and inherits their assertions. `params.cpp` is elaborate
only because `fsutil` is host C++ and cannot.) The raw devices are on the image — `/dev/rmd0`,
`/dev/rmd1` and `/dev/rmb0`, `cdevsw[3]` and `[4]` — and so are the block nodes `/dev/md0`
and `/dev/md1`.

### C4f. `mount`, `umount`

`mount.c` (67), `umount.c` (56), plus `/etc/mtab`. Small, and **unblocked since C4c**: the point
of them is that there is a second thing to mount, and now there is one. `/dev/md1` is on the
image for exactly this, `/etc/mkfs` makes the filesystem to put behind it, and
`kernel/test/mkfs`'s `.ini` is the two-drive harness to extend — attach the same scratch pack,
mount it on a directory, write a file through the *buffer cache* rather than through
`physio()`, unmount, and let the host fsck what comes back. That last is the part this task
buys that nothing else does: everything C4 has written so far went to the raw device, and
`bdevsw[0]` minor 1 has still never carried a block.

**Size.** What is left is two programs.  C4d was the weight and it is done.  What none of
C4a–C4e cost was its line count -- see df/README.md, cmd/dd/dd.c's header, mkfs/README.md
and fsck/README.md, the last of which came out a third shorter than it went in, and
icheck/README.md, whose four programs came to between 5,901 and 8,265 words apiece.

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
  same reasons: no baud rate, no parity, no delays, no LCASE. One name it uses is gone rather
  than renamed: `('t' << 8) | 16` is `TIOCFLUSH` here and nothing else, v7's `<sgtty.h>` spelling
  `TIOCTSTP` having been dropped when the two tty headers were folded onto `<sys/ttyio.h>`.
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

C4f, which is small and unblocked; C5 if a week is what is free rather than a day. C4e's one
loose end — a SIMH test for the four programs it added, at volumes 3093 and 3094 — is worth
folding into C4f rather than doing on its own: that task has to extend the two-drive harness
anyway, and `icheck`, `dcheck` and `ncheck` pointed at a *mounted* filesystem is exactly what
`mount` gives something to check.

C4a through C4e have landed, so the guest can now *examine* its own store — `df`, `du` and
`quot`, with the raw-device path proven and a fixture-filesystem harness under `b6sim` — *move*
it with `dd`, *make* one with `mkfs` on a second drive through a raw write path that had never
run, *repair* one (`fsck` takes a volume that is wrong, works out what is wrong with it and
puts it right, over that same write path, and then reads the filesystem the machine is running
on and pronounces it sound), and take each of those jobs on its own with `icheck`, `dcheck`,
`ncheck` and `clri` — of which `ncheck` is the one that buys something new, a **name** for an
i-number. What is left of C4 is `mount`/`umount`, whose whole point is that there is now a
second thing to mount and a program to make it with.

C5 stays the cheap one, and the harness for it is now **complete**: `b6_progtest()` needs no
boot, and C3 gave it the `<case>.in` that C2b's writeup said the filters would want, so a filter
can finally be fed. README.md §9 records what is left that it cannot do — two things now, not
three.
