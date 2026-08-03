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

**Tasks C1, C2, C3, C7 and the whole of C4, C5 and C6 are done and their writeups have been
removed**;
what each taught is README.md's sixteen closing sections. Seventy-four commands are on the image —
sixty-three entries in `/bin`, since `[` is `test` under a second name, plus `/usr/lib/diffh`,
`/etc/getty`, `/etc/quot`, `/etc/wall`,
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
([grep/README.md](grep/README.md), which is what C5e read before `sed`, and did). Three other
things are worth naming. **`grep -c` printed the two characters `%D`** — §3's trap, in the
fourteenth filter after thirteen had been grepped for it and come back clean, which is the
caution to take from a negative result. **`fgrep` did not fit the machine**: `struct words
w[6000]` is 24,000 words of the 28,672 an address space has, a state being four 48-bit words
where the PDP-11 packed it into eight bytes, so `MAXSIZ` is 3000 and two `_Static_assert`s hold
it — the number that looked generous was the one that did not fit, and the thing to multiply by
is `sizeof`. **And `fgrep` had two more defects that this task did not find**, both in `cfail()`
and both found afterwards by measuring rather than reading: a 400-entry queue in a stack frame
whose wrap arithmetic was bounded on one arm and not the other, which was a second undocumented
ceiling at 399 keywords *and* wrote past the frame; and a failure function that never took a
second hop along the fail chain, so `fgrep` silently missed matches for about one keyword list
in a hundred. And **`-b` is the fourth deliberate divergence**, after `touch`, `rev` and `col`: it
printed a block number, which `grep` computed with `BSIZE` and `fgrep` with a hard-coded 512, so
the same flag on the same manual page gave two answers for the same match. It is a byte offset
now — the division deleted rather than the divisor chosen, so the two cannot disagree again.
**And since C5d it can put its text in order**: `sort` takes the phase to sixteen and `/bin` to
forty-six entries, and it is the one program of the phase that manages its own storage. Three
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
rather than computed, which is the one form of it that cannot be short; **`make` inherits that
paragraph whole, and `find` turned out not to need it** — task C5f deleted its only `sbrk` with
`-cpio`.
**And since C5e it can edit that text without a human at the terminal**: `sed` takes the phase
to seventeen and `/bin` to forty-seven entries, and it is the first multi-file native port since
`/bin/sh`. Four things are worth naming and the first is why that matters. **v7's `sed.h`
*defines* the program's forty-odd globals**, in a header both `.c` files include, and left alone
that gives each half its own storage — the compiler compiling a script into a `ptrspace` the
executor never looks at, silently and completely. `cmd/sh/defs.h` had met it and `sedglob.c` is
`glob.c` again; five multi-file ports remain and README.md §1 now names the shape.
**`sed` had the `CCL` bitmap this file had been pointing at since task C3 and it was exactly
`grep`'s**, wild compile-side store included — but it had a *second* 128-entry table nobody had
flagged, the `y///` translation table, and that one is the finding: **its width is written as a
loop condition and a pointer bump and as a number nowhere at all**, so a grep for `128` or for
`[128]` finds neither, while the execution side indexes it with an unmasked byte and reads 128
bytes past it into the arena. `sort`'s `+128` bias was the shape a size-grep would miss; this is
the shape a *both* kinds of grep miss, and what found it was reading the one routine no brief
mentioned. **Three `genbuf` bounds were announced and not enforced** — `fprintf` with no `exit`,
no `break` and no `return` behind any of them — which is `sort`'s one-limit-two-paths finding
from a worse angle, the limit being stated on every path and acted on none. And **the recursion
ceiling had to be measured rather than copied from `grep`**: the two paths that reach `advance()`
start 170 words apart, and printing the diagnostic costs `_doprnt`'s 281-word frame, so a limit
set where the recursion just fits faults while saying it was reached
([sed/README.md](sed/README.md)).
**And since C5f it can do all of that to a whole tree, and say what it is looking at**: `pr`,
`cal`, `tsort`, `join`, `file`, `diff` (with `diffh`) and `find` close task C5 at twenty-four
filters, take `/bin` to fifty-four entries and give the image its first `/usr/lib`. Six things
are worth naming. **A hand-written replacement for a `<ctype.h>` macro has to evaluate its
argument once**, which `isspace()` does and a six-way `#define` does not — every call site in
`diff` passes `BLANK(c = getc(...))`, so the macro read six characters and `diff -b` called two
identical files wholly different while printing truncated text; it is the finding of the task and
`awk`, `m4` and `dc` all inherit it. **`find`'s frame was measured and not estimated, at the
second attempt**: `descend()` recurses with its directory block in the frame, this port shrank the
block, counted the locals, guessed eighty words and set a depth limit of 40 — and `b6disasm` said
`15 utm 0277`, 191 words, which is 7,640 of a 4,096-word stack. **§2's third hazard has found its
first victim** and it is not in an arena: `find`'s parse tree is a union built out of pointer
casts, and a fat `char *` through a `struct anode *` floors to the word, so `-name` would have
matched the wrong bytes. **`pr` had a bound that depends on what the user typed** — its 6,720-byte
look-ahead ring is in `bss` where `rootfs_pr_size` can weigh it, but its *capacity* is a function
of `-l` and the column count, and v7 checked nothing: `pr -l800 -2` over 24 KB silently began its
first column 840 lines late. **`diff` needed a fourth kind of oracle**, because a diff is not
unique — Hunt–Szymanski and Myers disagree textually 79 times in 600 runs and never about whether
the files differ — so what is checked is the invariant: the `-e` script, applied with the host's
`ed`, must produce the second file, and 150 out of 150 do. And **`file` is the sixth deliberate
divergence**, after `touch`, `rev`, `col`, `grep -b` and `sort -d`: v7 calls any byte above `0177`
`data`, which on this machine reports the system's own Russian prose as binary, so well-formed
UTF-8 is text and a malformed byte is still `data`
([file/README.md](file/README.md), [find/README.md](find/README.md),
[diff/README.md](diff/README.md), [pr/README.md](pr/README.md)).
**And since C6 there is more than one person on it.** `who`, `mesg`, `write`, `wall`, `stty`,
`passwd`, `su` and `newgrp` take `/bin` to sixty-two entries and are the first programs here whose
subject is *somebody else*. Four are worth naming. **`who` is the first reader `/etc/utmp` has
ever had** — `init` creates that file and blanks records in it and `login` writes them, and until
this task nothing had printed the table. **`write` is the first program to open a terminal it does
not own**, and it makes the `mesg` check *itself*, on a descriptor it has already opened, so a
`mesg n` refuses **root** as well; `/etc/wall` is the deliberate other half and makes no such
check, which is what its manual page means by the super-user overriding the protections a user has
invoked. **`stty`'s port is a cut** — eleven groups of capabilities out of two tables, measured
against `kernel/dev/tty.c` rather than assumed, and `LCASE` is the one the kernel *does* implement
and that came out anyway, because the Consul has no lower-case Latin and this machine's text is
UTF-8 ([stty/README.md](stty/README.md)). And **`passwd`, `su` and `newgrp` are the first setuid
bits here that are not about a directory**: what they borrow root for is an identity, `su` cannot
give it back because `setuid(2)` moves the real id too, and between them they make
`getxfile()`'s ISUID branch something a user reaches by accident rather than something
`lib/test/suidt` was written to reach on purpose ([passwd/README.md](passwd/README.md)).
`kernel/test/accounts`, at volume **3098**, is where a password is set, used to log in again, and
read back off the disk.
**And since C7 a whole tree can leave the machine.** `tar` takes `/bin` to sixty-three entries
and is the first program here that can move a *directory* rather than a file or a block — which
is the one thing `dd`, `mkfs`, `fsck` and `mount` between them still could not do, and the one
that stops a tree needing `b6fsutil` on the host to get on or off the image. Three things are
worth naming and not one of them was in the brief. **A tar header is a byte layout the archive
format fixes**, so the first work of the task was to measure whether a `struct` here can express
one — and the answer contradicted what this project had written down in two places, README.md §6
and [mount/README.md](mount/README.md) §2 both being wrong about how a `char` member packs; the
offsets are v7's exactly, and had they not been, every archive this machine wrote would have
been readable by nothing. **What is not safe is an ARRAY of a union**: `sizeof(union hblock)` is
516 and not 512, so v7's `tbuf[NBLOCK]` strode four bytes further per record than the transfers
across it — invisible at the default blocking factor of one, which is why nothing showed. And
**this file's own claim that `tar cf /dev/rmd0` worked already was wrong**: a raw archive breaks
three of `physio()`'s four conditions, so the blocking factor on a character special must be a
multiple of six and `r` is refused there outright ([tar/README.md](tar/README.md)).
`kernel/test/tar`, at volume **3099** with a scratch pack at **3100**, walks a tree, extracts it
through the setuid `/bin/mkdir`, proves with `find -newer` that the modification times came back,
and then writes the archive onto the bare `/dev/rmd1` for the host's own `tar` to read off the
pack. It also found a kernel bug: `tar` is the first program here to write a pack it never reads,
and [../kernel/TODO.md](../kernel/TODO.md) task 37 is the result.
[../etc/rc](../etc/rc) is a boot script that does something: it prints the motd and then the
date, which is a literal to the minute because the boot clock is the image's own `-T` stamp,
and `kernel/test/console` asserts both. What it
still wants is what the file itself now names — `cron` and `update`
(C10). `accton` was the third and **is not coming**: process accounting is in the *Not ported*
table below, so that line of the v7 `rc` is gone rather than pending. `mount` has arrived and
deliberately gets no line, a boot image having nothing to mount. An `fsck` line is a third, and it is C4d's one loose end: the
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
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` `mail` | a system worth using | open-ended |

**C4, C5 and C6 are gone from this table** — C4's twelve programs, C5's twenty-four and C6's
eight, and what each of them settled, being the opening paragraph's business now. C5 was called cheap in this table for four tasks running and the correction is
under *Where to start* below: twenty-four programs, 511 `ctest -L cmd` cases and the whole
label still under seven seconds is what the HARNESS cost, and it is the number to quote when
budgeting a test suite. What the ports cost is twenty-one findings, several of them a day
apiece, and not one of them is in a line count.
C9 is gated on something the task names. **C8's five inspection programs are done and the
section is gone**: `ps`, `dmesg`, `iostat`, `pstat` and `nice` are on the image, and what the
port taught is [README.md](README.md)'s business now. Its gate is worth one sentence of
history, because it was wrong twice over: the task opened by saying every one of these programs
needed `nlist(3)` to find a kernel variable by name in `/unix`, and **there is no `/unix`** —
[../root.manifest](../root.manifest) names no kernel image. `kctl(2)` was built in its place
([../include/sys/kctl.h](../include/sys/kctl.h), [../kernel/ksym.c](../kernel/ksym.c)) and
cannot drift from the image it is part of, every address in its table being a link-time
relocation of the real declaration.

**Two loose ends C6 leaves behind, both about the terminal and both one line each.**
`TANDEM` is honoured by the kernel — `ttyblock()` queues the stop character when the input queue
passes `TTYHOG/2` and `canon()` sends the start character back — and **no program on this image
can set it**: v7's `stty` had no word for it and the port added none, the cut in
[stty/README.md](stty/README.md) being deliberately subtractive. `TIOCEXCL`/`TIOCNXCL` and
`TIOCHPCL` are the other shape: `ttioccomm()` accepts all three and sets `XCLUDE` or `HUPCLS`,
and **nothing in the kernel ever tests either bit**, so an exclusive-open request and a
hang-up-on-last-close request are both silently ignored. Neither is worth a task of its own;
both are worth knowing before somebody reports them as bugs.

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

Everything else worth having, in no fixed order, once C1–C6 are in place.

**`mail` came here from C6, deliberately, and the reasons are worth keeping.** `mail.c` is 556
lines and would have been the ninth program of that task; what stopped it is not its size but its
surroundings. It wants a `/usr/spool/mail` directory that [../root.manifest](../root.manifest)
does not have and a mailbox **lock protocol** built out of the user execute bit (`lock()` sets
`st_mode & 01` and spins), it includes `<whoami.h>` for a `sysname` this tree pruned on purpose
([../include/README.md](../include/README.md) calls it one of the two identity placeholders), and
its `REMOTE`/`FORWARD` arms hand a letter to `uucp`, which is in the exclusion table below. So it
carries three decisions rather than a port: re-import `whoami.h` or hard-code the system name,
add the spool directory and its mode, and cut the remote arm or leave it failing. `cmd/login`
already probes for `/usr/spool/mail` with `access()` and quietly finds nothing, which is the
behaviour to keep until the day this is done.

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
| `awk/` | | 2,700 | yacc; also the most float-dependent program in the tree — read [../lib/libm/README.md](../lib/libm/README.md) on what overflow does here. `b.c` is the same Aho-Corasick shape `fgrep` has, and its tables *are* bounded on every path, but `cgotofn()`'s frame is ~900 words of them before its per-state `malloc` — §6's third and fourth ceilings both apply, and [grep/README.md](grep/README.md) is the worked example |
| `m4/` | macro processor | 995 | |
| `dc/`, `bc.y` | calculators | 1,943 + 600 | `dc` is the engine, `bc` the yacc front end |
| `expr.y` | shell arithmetic | 669 | yacc; wanted by scripts almost as much as `test` |
| `egrep.y` | | 594 | yacc; finishes C5c |
| `units.c` | | 466 | needs `/usr/lib/units` staged |
| `crypt.c`, `makekey.c` | | 93 + 21 | libc's `crypt` already exists |
| `at.c`, `atrun.c`, `cron.c`, `calendar.c` | scheduling | 307 + 110 + 254 + 54 | want a running multiuser system, which C6 delivered, and a correct clock, which this machine has not got -- iinit() seeds `time' from the root superblock. `cron` is one of the five [../etc/rc](../etc/rc) still names |
| `update.c` | periodic `sync` | 38 | trivial, and [../etc/rc](../etc/rc) names it — but it is a **daemon**, and `/etc/rc` runs on every pass through `init`'s loop, so weigh a second copy per pass before adding the line |
| `strip`, `size`, `nm` | | | **not these** — see C9 |

---

## C12. What `novi` left open

The editor itself is done and on the image — [novi/README.md](novi/README.md) is the account,
and [README.md](README.md)'s C12 section what it taught. **Not a v7 command**, which is why it
is not in C10's table. Three things outstanding:

**The interactive test, and it is blocked on something else.** A typed console dialogue driving
the screen is not written, on purpose: `kernel/test/edit` and `kernel/test/console` are both
DISABLED for the `send` wobble of [../kernel/TODO.md](../kernel/TODO.md) task 35, and a `novi`
dialogue would be strictly worse than `edit`'s — its output is escape sequences with embedded
cursor coordinates on an alternate screen, not readable text. **It goes in with the re-enabling
of those two, not before.** What is asserted meanwhile is the two halves that need no terminal,
which is where the risk of the port was: the disk-backed gap buffer (`cmd_novibuft_buffer`) and
the escape decoder (`cmd_novikeyt_keys`).

**No `termcap`.** `novi` writes hard-coded ANSI, so a terminal that is not ANSI cannot use it.
The blocker is `b6_prog()`'s missing `LIBS` keyword, which
[../lib/libcurses/README.md](../lib/libcurses/README.md) has predicted since the library landed
and which nothing has needed yet — `novi` is the first program that *would* want it and the
first that gets by without it.

**The search is literal.** `ed`'s regex engine is next door and is 700 lines of it; whether
`novi` should carry a second copy or the two should share one is the question to settle before
starting, not during.

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
| `ac.c`, `sa.c`, `accton.c` | 251 + 489 + 16 | Process accounting. The kernel side EXISTS and works — `acct(2)` is a real gate ([../kernel/acct.c](../kernel/acct.c), `<sys/acct.h>`), which is what makes this a decision rather than a gap. Nothing needs it: the machine has one operator, there is nobody to bill, and `sa`'s whole subject is digesting a record nothing on this image writes. It would also want a `/usr/adm` that [../root.manifest](../root.manifest) does not have, and a boot-time `accton` line in [../etc/rc](../etc/rc) whose only assertion home is the DISABLED `kernel/test/console`. Reconsider if this machine ever has more than one user who matters. |
| `random.c`, `sp.c`, `tk.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |

---

## Where to start

**C10's `expr`.** Neither task in the table is unblocked whole: C9's first three fifths wait on
a foreign repository, and C10 on the `yacc` decision its own section names. `expr.y` is inside
that decision but is the smallest thing behind it, and scripts want it almost as much as they
want `test`.

**Task C7 is closed and cost two false claims, both of them in briefs rather than in code.**
The section that stood here named two things to settle and both were right and small; what the
task actually paid for was a **layout rule this project had written down wrong in two places**
and a **claim in this very file that a path already worked**. [README.md](README.md)'s C7
section is the long form and [tar/README.md](tar/README.md) the detail. Three things to carry:

* **A claim about struct layout is worth a `printf` of four `sizeof`s before it is worth an
  hour of design.** §6 said a `char` member of a struct takes a word of its own and
  [mount/README.md](mount/README.md) §2 said `sizeof{char f[32]; char s[32];}` is 72. Both are
  wrong — it is 66, chars pack six to a word inside a struct — and the first came from reading
  `b6nm`'s **octal** addresses as decimal. A tar header is a byte layout the format fixes, so
  either claim being true would have made every archive this machine writes readable by
  nothing, silently.
* **A false all-clear is worse than a false alarm.** C3 recorded that a warning which is false
  is the expensive kind, because it sends the work at a problem that is not there; C7 paid that
  (`tar` was called a multi-file port here and in README.md §1, and is one source) and paid the
  other kind too. This file said `tar cf /dev/rmd0` worked already. It broke three of
  `physio()`'s four conditions. A false alarm wastes a morning; a false all-clear ships.
* **Ask what a new program does that no existing one does, and point an oracle at it.**
  `tar cf /dev/rmd1` is the first thing here to write a pack it never reads, which is how it
  found [../kernel/TODO.md](../kernel/TODO.md) task 37 — `mdvol[]` filled from a read alone, so
  a written pack is stamped with another drive's label. The oracle that caught it already
  existed next door in `run-mkfs.sh`.

**Task C12 is closed and is the one task in this file that was not a port**, and the reason it
is worth reading before starting another is that it says which half of the recipe does the work.
Five of the eleven points came back empty on a source that had never seen v7; what was left was
**four kernel primitives that do not exist**, and not one of them wanted a substitute — the thing
that wanted them was wrong, or was better off without them. `creat(2)` preserves for free the
inode, owner and mode that a temp-file-and-rename save exists to fake. Also: **§11 has an input
side** — a byte above `0177` given a meaning of the program's own is a hazard whichever way it is
travelling, and the input side is harder to spot, the symptom being a lost keystroke rather than
mangled output. And **a syscall count can be a correctness-shaped problem**: `novi`'s gap moved a
byte at a time, four traps each, so one PgDn was six thousand of them. Work that out from the
source before deciding a port is mechanical. [README.md](README.md)'s C12 section is the long form
and [novi/README.md](novi/README.md) the detail.

**Task C6 is closed and cost four findings, none of them a line count** — the eight ports took
an afternoon and everything around them took the rest. A declaration four files carried is a
header's job and the threshold is how many callers there are (`crypt`, `getpass` and `ttyslot` are
in `<unistd.h>` now, and the definitions include it, which is the part that buys anything). A
capability the kernel honours is **not** automatically one to expose — `LCASE` is implemented and
came out anyway. A `step` budget is a ceiling on **guest time**, and `wall` is the first thing
tested here that waits on the wall clock rather than the CPU: one second is most of a 50,000,000
step budget and the failure looks exactly like a hung program. And `exit` does not leave an
interactive shell, which is `cmd/sh/error.c`'s `exitsh()` and is why every dialogue in
`kernel/test/` types `^D`. [README.md](README.md)'s closing section is the long form.

**Task C5 is closed and it is worth stating what it cost, because the estimate was wrong every
single time.** Twenty-four programs, 511 `ctest -L cmd` cases, the whole of that label still
under seven seconds, and one booted pass over all twenty-four at volume 3097. The table used to
call the phase "cheap"; C5c corrected that, C5d corrected it again, and C5f is the fourth
correction in a row. **"Cheap" was always about the harness and never about the port.**
Twenty-one findings across the six sub-tasks and not one of them is in a line count:

* **C5c** — two bugs a user meets on the first Cyrillic pattern, a program that did not fit the
  address space, and a recursion that ran off the four-page stack returning a *wrong answer* for
  a dozen levels before it faulted.
* **C5d** — a wild read past four tables, a divergence the rotation had been hiding, an arena
  that starved stdio without saying so, and a **compiler** bug one `if (a - b)` away since the
  first port.
* **C5e** — a header that would have given the two halves of one program separate storage, a
  character table whose width is written nowhere as a number, three bounds announced and enforced
  nowhere, and a recursion ceiling whose two entry points start 170 words apart.
* **C5f** — a `<ctype.h>` replacement that evaluated its argument six times, a stack frame
  estimated at 80 words that measured 191, §2's flooring cast finding its first real victim in a
  parse tree built out of pointer casts, a look-ahead buffer whose capacity depends on the
  command line, and a program whose oracle had to be an invariant because its answer is not
  unique.

[grep/README.md](grep/README.md), [sort/README.md](sort/README.md), [sed/README.md](sed/README.md),
[pr/README.md](pr/README.md), [file/README.md](file/README.md), [diff/README.md](diff/README.md)
and [find/README.md](find/README.md) are the long forms; the opening paragraph is the account.

**Four things C5 leaves to whatever comes next**, and the last is the one to plan around.

**The `b6_obj` blind spot.** Its header dependency is the *system* header tree, so a program
whose own constants live in a header of its own has no dependency on that header and editing it
rebuilds nothing. `cmd/sed` and `cmd/sh` name their own headers; `tar`, `make`, `m4`, `awk` and
`dc` will each have to.

**The four shapes a table indexed by a character goes wrong in**, all now on the record and all
worth carrying into C10: **`grep`'s**, right-sized and stored into unmasked; **`sort`'s**, 256
entries reached through a `+128` bias so a size grep finds nothing; **`sed`'s `y` table**, whose
width is a loop condition and a pointer bump and a number nowhere at all; and **`file`'s
`english()`**, which is a v7 wild write that this machine's unsigned `char` repairs by itself and
must not be "fixed" back. In every case the assertion that proves the fix is a **negative** one,
because a masked byte does not vanish — it becomes a different plausible letter.

**The four shapes an oracle can take**, which is the phase's most transferable result. A
**designed fixture** where a reviewer can check the answer (`sort`); a **second implementation**
where nobody can (`od`, `pr`); the **host's own program** replayed over the whole suite as a
cheap third opinion (C5e's rule, and it found `join` and `diff` agreeing byte for byte with
BSD's on everything the two dialects share); and — C5f's addition — an **invariant** where the
answer is not unique at all, which is the only thing that works for `diff`: its `-e` script,
applied with the host's `ed`, must produce the second file. Ask which of the four the program
admits before writing a case.

**And what the two harnesses can and cannot say**, which is now six limits long
([README.md](README.md) §9) and has one whole program on the wrong side of it. `find` has **no
`b6sim` half at all** — it reads directory descriptors, `popen`s `pwd` and `fork`/`exec`s — so
`cmd/find/` has no `test/` directory and `kernel/test/filters` §18 is the only word on it.
`mount`/`umount` were the first such program and this is the second. **`tar` was checked for it
before its cases were designed, as this paragraph asked, and came out on the good side**: it
reads directories too, but only on the `c` path, so `t`, `x`, `r` and every bound and diagnostic
have a `b6sim` half and only the tree walk needed a boot. **`ps` came out on the good side too,
and by a route worth recording**: `b6sim` now answers `kctl(2)` and both memory devices out of
an imitation kernel, so twelve of task C8's fifteen cases run under it. What has no `b6sim`
half is not a program but a *branch* — a u-area at a `p_addr` that is not the caller's, which
the simulator's single process can never produce. `kernel/test/inspect` is the only word on it.

**The booted pass is where a later program joins rather than boots.** `kernel/test/filters`
runs twenty-four programs at volume **3097**, masks nothing, and takes about ten seconds. Adding
a section to `filters.sh` costs nothing; a boot of its own costs two minutes and a volume number
— which task C7 spent, `tar` needing a **second drive** that `filters.ini` does not attach.
`accounts` took 3098, `tar` 3099 with a scratch pack at 3100, and task C8's `inspect` 3101 —
which did take a boot of its own rather than joining `filters`, because what it needs is
PLURALITY: a second process, and one of them asleep. So **3102 is the next free volume.**
