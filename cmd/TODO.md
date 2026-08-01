# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. It is the companion of
[../kernel/TODO.md](../kernel/TODO.md), which carries the kernel's own list, and it starts where
that one's task 24 left off: the machine boots, mounts a root filesystem, execs `/etc/init` and
gives a shell prompt.

**[README.md](README.md) beside it is the reference** — what is already in this directory, the
porting recipe, the hazards a v7 source walks into on this machine, how a program gets onto the
image and which harness tests it. Read it before starting any task below; **nothing here repeats
it**, and a task names only what is unusual about itself. **A bare `§N` below is a section of
that file's porting recipe** — §2 the `char *` ordering hazard, which the compiler has since
fixed and which §2 now records as history, §4 the 3072-byte block and the
1024-byte one reported in its place, §6 the
address-space ceilings, and so on.

**Tasks C1, C2, C3, the whole of C4 and C5a are done and their writeups have been removed**;
what each taught is README.md's eleven closing sections. Fifty-five commands are on the image —
forty-six entries in `/bin`, since `[` is `test` under a second name, plus `/etc/getty`, `/etc/quot`,
`/etc/mkfs`, `/etc/fsck`, `/etc/icheck`, `/etc/dcheck`, `/etc/ncheck`, `/etc/clri`,
`/etc/mount` and `/etc/umount` beside them
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
([icheck/README.md](icheck/README.md)). **And since C4f it can *reach* one**: `/etc/mount`
puts a second filesystem behind a directory, which is the first thing in this tree ever to
call `mount(2)` — `smount()` had been compiled into every kernel this port built and had no
caller — and the first block `bdevsw[0]` minor 1 has ever carried. Everything C4 wrote
before it went to the **raw** device through `physio()`; a mounted volume goes through the
**buffer cache** ([mount/README.md](mount/README.md)). **And since C5a it can read its own
text**: `wc`, `cmp`, `sum`, `tee`, `split` and `rev` are the first programs here whose subject
is bytes rather than the machine, and the phase they open is the one that makes a userland
*testable* — thirty-nine cases in a tenth of a second each, with no boot at all. Two of them
are worth naming. `wc` is where §11 stopped being a property of the *system* and became a
property of a **program**: v7's word test makes every byte of a Cyrillic letter a delimiter, so
until this task the machine could type, store and glob `привет мир` and then count it as zero
words. And `rev` is the first **deliberate divergence** since `touch` — it reverses UTF-8
sequences where v7 reversed bytes, because on a machine whose text is UTF-8 end to end the
faithful port is the one that returns mojibake ([rev/README.md](rev/README.md)).
**And since C5b it can read it in every way the manual offers**: `tr`, `uniq`, `comm`, `tail`,
`od`, `look` and `col` take the phase to thirteen and `/bin` to forty-three entries. Three are
worth naming. **`od` is the one program in this tree that had to be *designed* rather than
ported** — its whole subject is the machine word, and a machine word here is 48 bits, so `-o`,
`-d` and `-x` go on meaning exactly what they meant on a PDP-11 while the columns they print
become 16, 15 and 12 digits wide; `-w` is the name this file asked for and is a **synonym** for
`-o` rather than a sixth format, because making `-o` byte-sized would have been the one change
that really did redefine a v7 flag ([od/README.md](od/README.md)). **`col` is the third
deliberate divergence**, after `touch` and `rev`: v7's steals bit `0200` of every stored
character for a Model 37 Teletype's Greek half-shift and masks its input with `0177`, which
between them turn `привет` into `P?QP8P2P5Q` — ten bytes of plausible ASCII, two characters
short, which is a worse failure than losing the text would be — so the shift is deleted rather
than carried, on `getty`'s precedent of cutting what this hardware has not got
([col/README.md](col/README.md)). And **`look` brought a data file with it**: `/usr/dict/words`
is on the image, which is what makes the bare `look word` form mean anything, and which is the
sharpest instance of §9's absolute-path rule there has been — under `b6sim` that path is the
*build machine's*, so every case beside the source names its dictionary explicitly and the
default is asserted under the booted kernel or nowhere. **And C5a's deferral is paid**:
`kernel/test/filters` puts all thirteen through one boot, at volume **3097**.
**And since C5c it can search it**: `grep` and `fgrep` take the phase to fifteen and `/bin` to
forty-five entries, and they are the two that carried the **`CCL` bitmap** this file had been
pointing at since task C3. It was 128 bits and is 256 — but the warning was right about the
constant and wrong about the failure. The `0177` mask everyone knew about is the *match* side;
the **compile** side masked nothing at all, so a Cyrillic byte in a class stored past the end of
its own sixteen-byte table and corrupted the compiled expression while it was being built.
Widening the table closes both, `c >> 3` landing in `[0,31]` by construction
([grep/README.md](grep/README.md), which is what C5e should read before `sed`). Three other
things are worth naming. **`grep -c` printed the two characters `%D`** — §3's trap, in the
fourteenth filter after thirteen had been grepped for it and come back clean, which is the
caution to take from a negative result. **`fgrep` did not fit the machine**: `struct words
w[6000]` is 24,000 words of the 28,672 an address space has, a state being four 48-bit words
where the PDP-11 packed it into eight bytes, so `MAXSIZ` is 3000 and two `_Static_assert`s hold
it — the number that looked generous was the one that did not fit, and the thing to multiply by
is `sizeof`. And **`-b` is the fourth deliberate divergence**, after `touch`, `rev` and `col`: it
printed a block number, which `grep` computed with `BSIZE` and `fgrep` with a hard-coded 512, so
the same flag on the same manual page gave two answers for the same match. It is a byte offset
now — the division deleted rather than the divisor chosen, so the two cannot disagree again.
**And since C5d it can put its text in order**: `sort` takes the phase to sixteen and `/bin` to
forty-six entries, and it is the one program of the phase that manages its own storage. Four
things are worth naming and not one of them was in a table. **Its four character tables were
rotated by 128** — v7's way of letting a *signed* `char` index them — so on a machine with
unsigned chars every subscript landed up to 128 bytes past the end of its own array, which is
`grep`'s `CCL` finding again except that this one is a wild **read** rather than a wild store,
and a read returns a plausible number and carries on. Un-rotating them asked the question the
rotation was hiding, and the answer is **the fifth deliberate divergence** after `touch`, `rev`,
`col` and `grep -b`: v7's tables ignore every byte of `0200`–`0377`, so a faithful `sort -d`
deletes a Cyrillic word before comparing it and makes `привет` and `мир` compare *equal*.
**An arena that takes the whole heap starves stdio in silence** — a stream whose `malloc` fails
does not fail, it becomes one syscall per byte — so the reservation is `malloc`'d and `free`'d
rather than computed, which is the one form of it that cannot be short; `find` and `make`
inherit that paragraph whole. And **the compiler had a wrong-code bug that sixteen ports had
walked past**: a bare truth test on an additive result compiles as a *sign* test, so `if (x - y)`
is false whenever `x > y`, which threw away half of every `sort -n` on a key and said nothing
([../cmd/tmp/BUG.md](tmp/BUG.md), and nothing else on the image hits it).
[../etc/rc](../etc/rc) is a boot script that does something: it prints the motd and then the
date, which is a literal to the minute because the boot clock is the image's own `-T` stamp,
and `kernel/test/console` asserts both. What it
still wants is what the file itself now names — `cron` and `update`
(C10) and `accton` (C8); `mount` has arrived and deliberately gets no line, a boot image
having nothing to mount. An `fsck` line is a third, and it is C4d's one loose end: the
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

**One rule of task C4's survives it and is cited from a dozen sources**, so it lives here
rather than in a section that has been deleted. Anything that encodes the **on-disk layout**
— a block number computed from an i-number, an entry count per block, a name length —
**`_Static_assert`s against `<sys/param.h>` rather than re-deriving the constants**. That is
what [fsutil/params.cpp](fsutil/params.cpp) does on the host side, and why a kernel that
retunes `INOPB` or `DIRSIZ` breaks the build instead of the images. (A guest program needs
none of that file's machinery: it includes the real headers and inherits their assertions.
`params.cpp` is elaborate only because `fsutil` is host C++ and cannot.) It applies to test
scripts as much as to C, which is why `b6fsutil -D`'s damage targets are **symbolic**. The
devices those programs are pointed at are all on the image: `/dev/rmd0`, `/dev/rmd1` and
`/dev/rmb0` (`cdevsw[3]` and `[4]`), and the block nodes `/dev/md0`, `/dev/md1` and
`/dev/md2`.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it
compiles.

| | task | what it buys | size |
|---|---|---|---|
| C5 | the text filters — ~~`wc` `cmp` `sum` `tee` `split` `rev` `tr` `uniq` `comm` `tail` `od` `look` `col` `grep` `fgrep` `sort`~~ `sed` `pr` `diff` `cal` `tsort` `join` `find` `file` | the corpus everything else is tested against | medium ×8; sixteen done |
| C6 | multiuser userland — `passwd` `su` `newgrp` `stty` `who` `write` `wall` `mesg` `mail` | more than one person | medium; unblocked |
| C7 | `tar` | getting data on and off without `b6fsutil` | medium |
| C8 | inspection — `ps` `dmesg` `pstat` `iostat` `nice`, `ac` `sa` `accton` | seeing what the machine is doing | medium; needs `nlist(3)` |
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` | a system worth using | open-ended |

**C4 was the one that mattered and it is gone from this table**, its twelve programs and what
each of them settled being the opening paragraph's business now. C5 is cheap and pays for
itself in test coverage, and C5a through C5d have now shown what that is worth in numbers:
sixteen programs, 327 `ctest -L cmd` cases, and the whole label still finishing in under seven
seconds. **"Cheap" is about the harness and not about the port**, which is C5c's correction to
this paragraph and C5d's again: two of C5c's programs were three quarters of a day's work each,
C5d's one program was more than that, and the reason was never the line count. C5d's day went
on four things no line count contains, one of them a bug in the compiler.
C7 is one program and can be taken at any time; C6, C8 and C9 are each gated on something the
task names.

---

## C5. The text filters

**Why.** Two reasons, and the second is the real one. They are the commands that make a Unix feel
like Unix — but more importantly **almost all of them run under `b6sim`**, so this is the phase
that builds a userland regression corpus cheaply, in the harness that does not need a two-minute
boot.

**C5a, C5b, C5c and C5d are done and their writeups have been removed**; what they taught is
README.md's *What task C5a taught* through *What task C5d taught*. Sixteen filters are on the
image with 327 `ctest -L cmd` cases between them and a booted pass over all sixteen at once.
Four things they leave to the tasks below.

**The filter test pattern is established**, which is what C5a existed for: `<case>.in` feeds the
filter, fixtures are **copied into the build directory** rather than named through `@srcdir@` (a
program that prints the name it was given would otherwise put a build path into a checked-in
`.expected`), and a filter whose output is a **file** rather than a stream gets a custom
`add_test` that lets the host look at the directory afterwards — `cmd/tee/test/run-tee-test.sh`,
`cmd/split/test/run-split-test.sh` and now `cmd/uniq/test/run-uniq-test.sh` are the three shapes.
`sed -n w` and `sort -o` will both want the last. (**This file used to name `col` among them and
was wrong**: `col` opens no file at all, `getchar()` in and `putchar()` out, and takes the
ordinary shape. That is the second time a warning here was written from a survey rather than from
the code — see what C3 says about `ed`'s `CCL` bitmap — and it is worth re-reading the source
before budgeting for a harness.)

**The booted pass exists and later tasks join it rather than repeating it — and C5c and C5d
did.** `kernel/test/filters` at volume **3097** runs all **sixteen** in one boot, and
`cmd/README.md` §9 lists the seven things it asserts that `b6sim` cannot. `grep`, `fgrep` and
`sort` added a section to `kernel/test/filters.sh` rather than a boot, exactly as this file
asked; C5e should do the same. **The next free volume number is still 3098.**

**One of those six is new and it constrains how a case is written.** `run-prog-test.sh` splits a
`.args` line on whitespace with no quoting, so **a pattern containing a space cannot be a `b6sim`
case at all** — which is much of what anybody greps or `sed`s for. C5c also found that the line
was being *globbed*, harmlessly for thirteen filters and not for a regular expression; the
harness runs `set -f` now, and `sed`'s cases inherit both the fix and the remaining limit.

**An oracle for a program whose output is unreadable should be a second implementation — and
C5d found the edge of that rule.** `od` prints 48-bit words in sixteen octal digits, which no
reviewer can check by eye, so every one of its expectations came from a Python implementation
written from the manual page. This file used to name `sort` beside it and was **half wrong**:
`sort`'s output is ordinary readable text, and a nine-line fixture with a designed answer turned
out to be the better oracle, because it says *which rule* broke. A second implementation earned
its keep in exactly one of `sort`'s 41 cases — a 4,000-line generated input, sized so that one
pass is impossible by arithmetic, checked against `LC_ALL=C sort`. **The question is what a
reviewer could not check by eye, not what the program is.** `pr` is the one left that really
does have `od`'s property.

**`sed`'s `CCL` bitmap is still 128 bits, and `grep`'s is not** — task C5c widened it, so C5e
has a worked example rather than a warning. **Read `sort`'s account beside it**: its tables were
the right size already and were indexed with a `+128` bias, which is a second shape of the same
hazard and the one a grep for the table's *size* would not have found. Read [grep/README.md](grep/README.md) before starting
`sed`: the five places the width is written down, why the *compile* side turned out to be a wild
store and not merely a narrow table, and why the assertion that proves the fix is a **negative**
case. `sed0.c:732` and `sed1.c:243`/`296` are the same three lines.

### C5e. `sed`

`sed/` (1,690 lines: `sed0.c`, `sed1.c` and `sed.h`). The same regex family as `ed`, and **C3 is
done**, so [ed/README.md](ed/README.md) is the thing to read first: the three `char *` bounds
in `sed1.c` are the same `genbuf` bound `ed` had, and the `QESC` prefix that replaced
bit `0200` in `ed`'s replacement text is the pattern for `sed`'s. But `sed` **does** have the
`CCL` bitmap `ed` turned out not to, and task C5c has now done that widening in `grep` —
[grep/README.md](grep/README.md) is the recipe and the hazard it uncovered, and this is the
half of `sed` not to budget for from scratch.

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

**C5e**, `sed`, and [grep/README.md](grep/README.md) is what to read before starting it — the
`CCL` widening is a worked example now rather than a warning, and [sort/README.md](sort/README.md)
is the one to read before `find` in C5f, which has the same `sbrk` shape. The phase's economics
are measured rather than argued: sixteen programs, 327 cases, the whole of `ctest -L cmd` in
under seven seconds, and one booted pass that covers all sixteen at once.

**C5c and C5d were both cheap in line count and neither was cheap.** C5c's four findings were
two bugs a user meets on the first Cyrillic pattern, a program that did not fit the address
space, and a recursion that ran off the four-page stack returning a *wrong answer* for a dozen
levels before it faulted. C5d's were a wild read past four tables, a divergence the rotation had
been hiding, an arena that starved stdio without saying so, and a **compiler** bug that had been
one `if (a - b)` away since the first port. None of the eight is in a line count. The opening
paragraph is the account; [grep/README.md](grep/README.md) and [sort/README.md](sort/README.md)
are the long forms.

**The whole of C4 has landed**, so the guest can now *examine* its own store — `df`, `du` and
`quot`, with the raw-device path proven and a fixture-filesystem harness under `b6sim` — *move*
it with `dd`, *make* one with `mkfs` on a second drive through a raw write path that had never
run, *repair* one (`fsck` takes a volume that is wrong, works out what is wrong with it and
puts it right, over that same write path, and then reads the filesystem the machine is running
on and pronounces it sound), take each of those jobs on its own with `icheck`, `dcheck`,
`ncheck` and `clri` — of which `ncheck` is the one that buys something new, a **name** for an
i-number — and, since C4f, *reach* one: `mount` puts a second filesystem behind a directory,
which is the first thing here that has ever gone to a disk through the **buffer cache** rather
than through `physio()`. C4e's one loose end went with it, folded into that task's harness as
this section used to recommend.

C5 stays the cheap one, and the harness for it is now **proven** rather than merely complete:
`b6_progtest()` needs no boot, C3 gave it the `<case>.in` that C2b's writeup said the filters
would want, and thirteen filters have been fed with it. Two things C5a added to that harness and
every later filter inherits: a **fixture is copied into the build directory** rather than named
through `@srcdir@`, because a program that prints the name it was given would otherwise put a
build path into a checked-in `.expected`; and a filter whose output is a **file** gets a custom
`add_test` beside its cases, since `b6_progtest()` diffs standard output and can do no more.
README.md §9 records what is left that the harness cannot do — and C5b added a fourth item to
that list, `argv[0]` being the staged build path, which is what sends `col`'s two diagnostics to
the booted test.

**And C5b closed the phase's other half**: `kernel/test/filters` exists, at volume 3097, so a
later filter that wants a booted assertion adds a section to `filters.sh` rather than a boot.
Five things it says that `b6sim` cannot are listed in that script's header; the shortest of them
is that `b6sim`'s standard input is always a **file**, so any program that branches on `ESPIPE` —
`tail` does — has a whole path that only a real pipe reaches.
