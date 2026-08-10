# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. The kernel's own plan is
empty; its reference is [../kernel/README.md](../kernel/README.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C10`, `C11`, … — because the kernel's task numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped: C1 through C9 are spent and their
sections are gone, and no number is ever re-used, because `root.manifest` stanzas and per-program
`README.md`s cite them.

**Three numbers are spent and have no row in the table below**, which is why they are written
down here rather than only in the places that cite them.

**C12 is `novi`** and **C27 is `more`**: the two programs under [`.`](.) that are not ports of v7
commands — v7 had neither a full-screen editor nor a pager — so neither ever had a row among the
v7 programs still to port, and each landed complete and left no section behind. `egrep` held C12
for a while by mistake and is C26 now.

**C25 is the manual**, and it is spent in three parts that are cited from about twenty files, so
they are named here: **C25a is [`manview/`](manview/)**, the renderer, which is
`/usr/bin/manview` and is what `man(1)` runs over a page; **C25b is [`man/`](man/)**, which finds
the page; and **C25c was the standing procedure** — [`man2umm/`](man2umm/) is *not* retired, every
program left in the table above arrives with a roff page, and `b6man2umm` plus
[../scripts/mancheck.py](../scripts/mancheck.py) is how one becomes a `.umm`. That last is a
habit and not a task, and it lives in `man2umm/README.md` and §10 of [README.md](README.md) now.
`manview` is also this tree's third program that is not a port of a v7 command, `nroff` being
refused below with the rest of the typesetting suite.

**None of 12, 25 or 27 may be re-used**, which is the rule directly above and is exactly the rule
a number recorded in twenty source files and nowhere else is apt to break.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it compiles.

**One task is one program**, and they are ordered so that each is unblocked by the one before it.
**C24 is the exception and says so**: eight mechanical conversions of programs that are already
here and already tested, whose value is in doing them together rather than one at a time.

| | task | what it buys | size |
|---|---|---|---|
| C10 | `yacc` and `lex`, host and native | the seven grammars below, and a machine that generates its own parsers | large |
| C11 | `expr` | shell arithmetic; the first thing C10 proves | small |
| C26 | `egrep` | finishes C5c | small |
| C13 | `m4` | macro processor | medium |
| C14 | `make` | the build tool — the highest-value item here | large |
| C15 | `dc` | the calculator engine | medium |
| C16 | `bc` | the calculator front end | small |
| C17 | `awk` | the one program that is a grammar *and* a scanner | large |
| C18 | `units` | | small |
| C19 | `crypt`, `makekey` | | small |
| C20 | `update` | one of the two [../etc/rc](../etc/rc) lines | trivial |
| C21 | `at`, `atrun` | | medium, blocked on a clock |
| C22 | `cron` | the other [../etc/rc](../etc/rc) line | medium, blocked on a clock |
| C23 | `calendar` | | small, blocked on a clock and on its data |
| C24 | the eight hand-rolled directory readers, over `opendir(3)` | one reader instead of eight, and §5 stops being everybody's problem | medium |

**Where to start: C11.** `b6yacc` and `b6lex` both exist now (C10a, C10b), so every grammar
below can be built — and `expr` is the smallest consumer and the one that proves them end to end.
C10d, which puts `lex` on the image as C10c did `yacc`, is a separate axis and blocks no grammar.

**Two loose ends about the terminal, one line each and neither worth a task of its own.** `TANDEM`
is honoured by the kernel — `ttyblock()` queues the stop character when the input queue passes
`TTYHOG/2` and `canon()` sends the start character back — and **no program on this image can set
it**, the cut in [stty/README.md](stty/README.md) being deliberately subtractive.
`TIOCEXCL`/`TIOCNXCL` and `TIOCHPCL` are the other shape: `ttioccomm()` accepts all three and sets
`XCLUDE` or `HUPCLS`, and **nothing in the kernel ever tests either bit**, so an exclusive-open
request and a hang-up-on-last-close request are both silently ignored. Both are worth knowing
before somebody reports them as bugs.

**[../etc/rc](../etc/rc) still wants four lines.** `update` waits on C20 and `cron` on C22. A
boot-time `fsck` and the `rm -f /tmp/*` line wait on something else: both programs exist, but §7
step 4 gave a line in that script exactly one home for its assertion, `kernel/test/console`,
and that test is now **deleted** along with the rest of the tests that booted. So the deferral
stands, and with nothing left that runs `/etc/rc` at all it is no longer a deferral but a gap.

---

## C10. `yacc` and `lex`

**The parser-generator decision, settled by porting them.** Six of the tasks below are yacc
grammars — `expr.y`, `egrep.y`, `m4y.y`, `gram.y`, `bc.y`, `awk.g.y` — and `awk` is a lex scanner
besides. The two alternatives that were on the table are both refused: **checking generated C into
the tree** puts a machine-written file under version control and hides the grammar's real
dependency, and **taking a host `bison`/`flex` dependency** spends a property this project has kept
from the start, that the build needs nothing but CMake and a C++17 compiler.

**The sources come from RetroBSD**, `/Users/vak/Project/BSD/retrobsd/src/cmd/yacc` and
`.../src/cmd/lex`, not from the v7 originals. RetroBSD's yacc is 4.2BSD's with the ANSI pass
already done — prototypes throughout, `<stdarg.h>` for `error()` — which is most of §1 gone before
the task starts. RetroBSD's lex is very nearly v7's, K&R and all, but it carries the 4.xBSD
`<paths.h>` fixes and a few corrections, and there is no better starting point in existence.

**This is the one task that started by fetching.** [README.md](README.md) says almost every
program named here is already in its directory as a verbatim upstream copy; [yacc/](yacc/) and
[lex/](lex/) are the exceptions, and both are now here.

**Neither program needs a support library.** v7 shipped `liby.a` (a `main()` and a `yyerror()`) and
`libl.a` (a `main()`, a `yywrap()`, `yyreject()` and `yyless()`); every consumer below defines the
first two itself and `ncform` carries the rest, so nothing here adds an archive to
[../lib/](../lib/) or to `B6_STAGE_LIB`. [lex/README.md](lex/README.md), "No support library".

### C10d. `lex` on the image — `/usr/bin/lex` and `/usr/lib/lex/ncform`

**C10c is the worked example and it is done**, so this is that shape again over a `lex/rootfs/`
that already exists (C10b's `scant` is there, and `cmd/lex` is added above the `libruntime.a`
guard as well as below it): a `b6_prog()` call and a staged skeleton in
[yacc/rootfs/CMakeLists.txt](yacc/rootfs/CMakeLists.txt)'s shape, a `rootfs/test/` on
[yacc/rootfs/test/](yacc/rootfs/test/)'s, [../root.manifest](../root.manifest) stanzas for the
binary, the skeleton and `/usr/man/man1/lex.1`, and `ROOTFS_FILES` in
[../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt). The manual page is written and
staged; only its manifest stanza is missing. `find_form()` already has its `#ifdef besm6` arm,
and `B6LEXFORM` is already on `ENV_WHITELIST` in [sim/session.cpp](sim/session.cpp) — C10c put it
there, since the agreement test is the only way a native tool reaches an image skeleton under
`b6sim`.

**Different binding ceiling from yacc's**, and C10b measured it rather than leaving it to be
guessed — [lex/README.md](lex/README.md), "What C10d still has to measure", has the arithmetic:

* **The heap is what binds, not `rootfs_lex_size`.** Nearly every table lex owns is `calloc`'d, so
  the size check sees almost none of it. At the shipped host sizes and `awk.lx.l`'s shape the peak
  concurrent heap is about **12,400 words** — the parse tree 4,167, phase 2 about 7,600, phase 1
  about 850. §6's uncheckable ceiling. A `besm6` size profile is **not optional**; `SMALL` is gone,
  so it is a new `#ifdef besm6` arm in `ldefs.h` beside the one block, and the six
  `_Static_assert`s there will refuse a set that does not hold.
* **Size it against `awk.lx.l`**, the only scanner this machine has to compile, from the numbers
  C10b pinned: **618** tree nodes of 1,000, **1,345** positions of 2,500, **202** states of 500,
  **64** packed classes of 1,000, **530** packed transitions of 2,000, **455** output slots of
  3,000. Every bound has better than 2x headroom, so the profile can come down a long way — and
  all six stay overridable per-`.l` with `%e %n %p %a %o %k`, which is the escape hatch a fixed
  profile usually lacks. Measure the **break**, not the request: `malloc` grows a page at a time.
* **The stack is already dealt with.** `cgoto()`'s `tch`/`tst`, `packtrans()`'s
  `go`/`temp`/`swork`/`cwork` and `acompute()`'s `temp`/`neg` are `static` as of C10b — some 1,700
  words moved off the stack, none of the three recursing. What is left is the
  `cfoll`/`first`/`follow` recursion, small frames at a depth the input chooses: §6's last bullet,
  and **the one ceiling still to give**.
* **What a generated scanner costs is measured**: `rootfs/scant` is **5,788 words** (80 const,
  3,741 text, 649 data, 1,318 bss) for eleven rules over stdio. `ncform` carries `yyreject()`,
  `yyracc()` and `yyless()` for every scanner whether or not it uses them, which `awk` does not;
  `rootfs_awk_size` is the first measurement of what that costs, and C10b's README records the
  fallback if it turns out to matter.
* **The agreement test is the one that matters**, as it was for yacc: host `b6lex` and native
  `/usr/bin/lex` over one scanner, `lex.yy.c` compared byte for byte, live.
  [yacc/rootfs/test/run-yacc-test.sh](yacc/rootfs/test/run-yacc-test.sh) is the script to copy,
  and it comes with two things C10c learned. **The `-v` statistics are not comparable** — like
  yacc's `y.output` they quote the profile's own bounds, so what `awk.lx.l`'s pinned line asserts
  is the *host* build and the cheap half of the check, not agreement. And a case must not name
  its input by an absolute path if the generator writes it into the output.

---

## C11. `expr`

`expr/expr.y`, 669 lines, one grammar and nothing else. **The smallest consumer of C10 and
therefore the one that proves it end to end** — do this before `make`, whatever the value ordering
says. Scripts want it almost as much as they want `test`, which is already on the image.

Its arithmetic is one word wide here (§3) and its `match` operator is a regular expression over
bytes (§11). A pure filter, so `b6_progtest` cases are the whole of the harness (§9).

## C26. `egrep`

`egrep/egrep.y`, 594 lines. **Finishes C5c**, which put `grep` and `fgrep` on the image and left
this one behind the yacc decision.

[grep/README.md](grep/README.md) is the worked example for everything it will hit: the `CCL`
bitmap that steals bit `0200`, a table whose size is written down nowhere, and a bound test that is
not on every path. `fgrep` is 20,019 words of which 15,000 are two arrays, which is the scale to
expect and the reason to measure early.

## C13. `m4`

`m4/m4.c` 901 and `m4/m4y.y` 94. The macro processor, and the smaller of the two things worth
having most.

## C14. `make`

`main.c` 366, `doname.c` 301, `files.c` 465, `misc.c` 339, `dosys.c` 172, `ident.c` 98 and
`gram.y` 306 — 2,047 lines — over a 128-line `defs` header. **The highest-value item in this
file.**

Two things to plan for. `defs` is §1's exact trap — a v7 multi-file program defining its globals
in a header the PDP-11 linker merged for it, which C11 and `b6ld` will not; `extern` in the header
and one definition file, checked with one line of `b6nm`. And `dosys.c` runs commands, so it is
`fork`/`exec`/`wait` — this system has **no `waitpid()`** and `<sys/wait.h>` has only the
argument-less `wait(2)`, as `cc.c`'s `run()` already works around in both its builds. Measure
against the word ceiling early: this is the largest thing here after `awk`.

## C15. `dc`

`dc/dc.c` 1,943 over `dc/dc.h` 119. The desk calculator, and the arbitrary-precision engine `bc`
drives. Its numbers are its own representation, not the machine's, so [../lib/libm/](../lib/libm/)
does not come into it — but it is a heap program and §6's uncheckable ceiling is the one to ask
about.

## C16. `bc`

`bc/bc.y`, 600 lines. **Blocked on C15**: `bc` is a front end that `exec`s `dc` and pipes to it, so
it is worth nothing until the engine is on the image. Small once C15 lands.

## C17. `awk`

2,428 lines of C across nine files, plus `awk.g.y` 272 and `awk.lx.l` 173 over the 131-line
`awk.def` header. **The only program in the tree that is a yacc grammar and a lex scanner both**,
which makes it the real test of C10 and the reason it comes last of the grammars rather than first.
Both halves generate today: C10a pinned `awk.g.y`'s 95 shift/reduce conflicts and C10b `awk.lx.l`'s
statistics line.

* `awk.def` is `make`'s problem again — §1, globals in a header.
* It is **the most float-dependent program here**; read [../lib/libm/README.md](../lib/libm/README.md)
  on what an overflow does on this machine before trusting a number it prints.
* `b.c` is the same Aho-Corasick shape `fgrep` has. Its tables *are* bounded on every path, but
  `cgotofn()`'s frame is some 900 words of them before its per-state `malloc`, so §6's stack and
  heap ceilings both apply and [grep/README.md](grep/README.md) is again the worked example.

## C18. `units`

`units/units.c`, 466 lines, and the `units/units` database staged as `/usr/lib/units` — a
[../root.manifest](../root.manifest) stanza of its own beside the binary's.

## C19. `crypt` and `makekey`

`crypt/crypt.c` 93 and `makekey/makekey.c` 21, the second as `/usr/lib/makekey`. libc's `crypt(3)`
already exists (it is what `login` and `passwd` use), so this is two small programs over an
implementation that is on the image already. §11 matters: `crypt` is a byte filter and must not
mask anything.

## C20. `update`

`update/update.c`, 38 lines — a `sync()` in a loop — and one of the two [../etc/rc](../etc/rc)
lines still missing. Trivial to port and **not trivial to add to the boot script**: it is a
**daemon**, and `/etc/rc` runs on every pass through `init`'s loop, so the line as v7 wrote it
leaves a second copy running per pass. Decide that before adding it, and remember §7 step 4 — the
assertion has one home and it is DISABLED.

## C21. `at` and `atrun`

`at/at.c` 307 and `atrun/atrun.c` 110 (documented inside `at.1`). **Blocked on a clock this machine
has not got**: `iinit()` seeds `time` from the root superblock's `s_time`, so every boot starts at
whatever `b6fsutil -T` stamped and nothing advances it but the interval timer. A program whose
whole subject is "later" is not worth putting on the image until that is answered, and the answer
is a kernel task nobody has written.

## C22. `cron`

`cron/cron.c` 254, plus the `cron/crontab` data file as `/usr/lib/crontab`. **The same blocker as
C21**, and the other [../etc/rc](../etc/rc) line — with the same §7 step 4 problem `update` has.

## C23. `calendar`

`calendar/calendar.c`, 54 lines. **Blocked twice**: on C21's clock, and on its data — what the
reference tree holds under `calendar`'s name is an x86 binary, not the database, so the file the
program reads would have to be written from scratch.

## C24. The eight hand-rolled directory readers, over `opendir(3)`

**The one task here that is not one program**, and it is together on purpose: eight mechanical
conversions whose value is not in any of them separately but in what stops being true afterwards.
§5 — *a name read out of a directory is not NUL-terminated* — becomes something the library knows
instead of something every future port has to be told, and the same for `d_ino == 0` and for
re-deriving `DIRENTSZ`.

`libc` grew the library with `cmd/ls`, which is 4.2BSD's now and is its first caller
([ls/README.md](ls/README.md), [../lib/libc/man/directory.3.umm](../lib/libc/man/directory.3.umm)):
`opendir`, `readdir`, `closedir`, `rewinddir`, `telldir`, `seekdir`, `dirfd`, about 230 words for
a caller that only walks. Nothing else uses it yet.

**The eight, and only the eight.** `du`, `find`, `rm`, `rmdir`, `mv`, `pwd`, `tar` and
`sh/expand.c` open a **pathname** and read entries out of it. That is the whole list.

**The other eight are not candidates and must not be converted.** `fsck`, `mkfs`, `ncheck`,
`dcheck`, `icheck`, `quot`, `df` and `pstat` also include `<sys/dir.h>`, and they read a
`struct direct` out of a block they fetched from `/dev/rmd*` themselves. `opendir(3)` has nothing
to offer a program doing filesystem archaeology — there is no descriptor on a directory to open,
only a block number — and `<sys/dir.h>` stays exactly the header they want. Naming both halves is
most of what this task is for.

Three things to weigh rather than assume:

* **`rm -r` is the one with a cost.** [rm/README.md](rm/README.md) records that it **holds the
  directory descriptor open across the recursion**, one per level. A bare descriptor is free; a
  `DIR` carries a read buffer sized from the directory, so a deep tree turns a handful of
  descriptors into a few hundred words apiece. Either `rm` keeps its raw reader, or the recursion
  closes before it descends. Measure it; do not decide in advance.
* **`sh/expand.c` has the other one.** Its read loop tests `trapnote & SIGSET` between entries, so
  a globbing shell stays interruptible, and a library `readdir()` hides that seam. It may be right
  to leave it alone for exactly that reason.
* **A conversion that changes no output is the point**, and most of the eight already have a
  harness that would have said so, had it not been deleted: `du` and `find` through `kernel/test/fsinfo`, `rm`/`rmdir`/`mv` through
  `files`, `tar` through `tar`, `pwd` through `console`, the shell through [sh/test/](sh/test/).

---

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `mail/`, `xsend/` | 556 + 414 | **Three decisions rather than a port.** `mail` wants a `/usr/spool/mail` directory [../root.manifest](../root.manifest) has not got and a mailbox **lock protocol** built out of the user execute bit (`lock()` sets `st_mode & 01` and spins); it includes `<whoami.h>` for a `sysname` this tree pruned on purpose ([../include/README.md](../include/README.md)); and its `REMOTE`/`FORWARD` arms hand the letter to `uucp`, which is refused below. There is one user on this machine and nowhere for a letter to go. `cmd/login` already probes for `/usr/spool/mail` with `access()` and quietly finds nothing, which is the behaviour to keep. `xsend` is secret mail and needs `mail` first. |
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` drives a CAT phototypesetter that does not exist, and there was never an `nroff` in this source tree to begin with. **The refusal is stronger than it was: there is nothing left here for either to typeset.** This repo's manual pages are in the dialect [../doc/Manual_Page_Format.md](../doc/Manual_Page_Format.md) describes, and [manview/](manview/) displays them. `spell` additionally needs its whole word list. |
| `tp/`, `dump.c`, `restor.c`, `dumpdir.c` | 800 + 641 + 1,150 + 475 | Tape. **This kernel has no tape driver** and no `bdevsw`/`cdevsw` row for one, and all four are built around a tape's sequential access rather than merely willing to use it — `dump`/`restor` are a filesystem-level backup pair whose whole design is the reel. `tp` is the pre-`tar` archiver and is superseded by it in any case. If a magnetic-tape driver is ever written (a kernel task nobody has raised; [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) is the reference), reconsider `dump`/`restor` and not the other two. |
| `uucp/`, `cu.c` | 6,415 + 541 | Dial-out over a modem link nothing models. `cu` becomes conceivable only if the machine's serial multiplexor is ever driven and wired to something outside; no kernel task proposes that. |
| `lpr/`, `vpr.c` | 1,315 + 334 | Printer spooling. **Worth revisiting:** SIMH *does* model the АЦПУ drum printer, so `lpr` becomes a small task the day a kernel printer driver exists — which is a kernel task nobody has written yet. |
| `graph.c`, `plot/`, `spline.c`, `tc.c`, `tk.c` | 695 + 608 + 335 + 638 + 250 | Plotters and Tektronix terminals; no hardware, and the output would go nowhere. |
| `learn/` | 1,066 | Needs the entire `/usr/lib/learn` lesson corpus, which is not in this tree. |
| `adb/` | 3,547 | PDP-11 instruction decoding, PDP-11 core files, PDP-11 `ptrace` semantics. A BESM-6 debugger is **new work**, not a port — and [disasm/](disasm/) plus `ptrace(2)` is where it would start. `ptrace`'s single-step, request 9, is **refused with `EIO`** on this machine: what it would take, and the breakpoint contract to settle before writing any of it, is the bullet in [../doc/Besm6_Kernel_Reference.md](../doc/Besm6_Kernel_Reference.md) under "Known consequences, accepted". |
| `lint/`, `mip/`, `struct/`, `ratfor/` | 1,164 + 7,615 + 4,721 + 1,200 | `lint` and `mip` are the PDP-11 C compiler's own internals; `struct`/`ratfor` are Fortran-to-Ratfor tooling with no Fortran here — which is also why C10b dropped lex's `nrform`. |
| `osh.c` | 846 | The pre-Bourne shell. [sh/](sh/) supersedes it. |
| `mknod.c` | 44 | **There is no `mknod(2)` in this kernel.** Every device node on the image is made by `b6fsutil` from [../root.manifest](../root.manifest), which is where a new one is added; a program that can only fail is worse than no program. Reconsider only if the gate is ever written. |
| `prof.c` | 310 | Reads a `mon.out` that nothing produces, and nothing will: the kernel decided against profiling, so `profil(2)` **refuses** with `EINVAL` (`../doc/Besm6_Kernel_Reference.md`, "Known consequences, accepted"), there is no `monitor`/`mcount` in libc, and `cc` has no `-p`. Reconsider only as the last step of porting all four; `b6sim` profiles a program today with no kernel help. |
| `cb.c`, `diff3.c`, `tabs.c` | 366 + 423 + 196 | Curiosities with a real cost and no caller. `cb` is a C beautifier superseded by this repo's own clang-format; `diff3` wants three files and a merge nobody does here; `tabs` sets hardware tab stops on terminals this machine does not have — the Consul typewriter is not one of them. |
| `cc.c`, `as/`, `ld.c`, `nm.c`, `ar.c`, `size.c`, `strip.c`, `ranlib.c`, `arcv.c` | | PDP-11 `a.out`, PDP-11 opcodes and PDP-11 registers; nothing in them survives retargeting. The BESM-6 tools were written for this repo instead, and task C9 built every one of them a second time for the machine itself — see each tool's "Building for the BESM-6". |
| `ac.c`, `sa.c`, `accton.c` | 251 + 489 + 16 | Process accounting. The kernel side EXISTS and works — `acct(2)` is a real gate ([../kernel/acct.c](../kernel/acct.c), `<sys/acct.h>`), which is what makes this a decision rather than a gap. Nothing needs it: the machine has one operator, there is nobody to bill, and `sa`'s whole subject is digesting a record nothing on this image writes. It would also want a `/usr/adm` that [../root.manifest](../root.manifest) does not have, and a boot-time `accton` line in [../etc/rc](../etc/rc) whose only assertion home was the deleted `kernel/test/console`. Reconsider if this machine ever has more than one user who matters. |
| `random.c`, `sp.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |
