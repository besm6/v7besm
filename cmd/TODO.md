# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. The kernel's own plan is
empty; its reference is [../kernel/README.md](../kernel/README.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C10`, `C11`, … — because the kernel's task numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped: C1 through C11, C13 through C20 and C26
are spent and their sections are gone, and no number is ever re-used, because `root.manifest` stanzas
and per-program `README.md`s cite them.

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
| C21 | `at`, `atrun` | | medium, blocked on a clock |
| C22 | `cron` | the other [../etc/rc](../etc/rc) line | medium, blocked on a clock |
| C23 | `calendar` | | small, blocked on a clock and on its data |
| C24 | the eight hand-rolled directory readers, over `opendir(3)` | one reader instead of eight, and §5 stops being everybody's problem | medium |

**Where to start: C21**, and it starts by answering the clock — C21, C22 and C23 are three tasks
behind one kernel question, and nothing below is unblocked until somebody answers it.

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

**[../etc/rc](../etc/rc) still wants three lines**, and only one of them waits on a program:
`cron`, which is C22 and is blocked with C21 on the clock. A boot-time `fsck` and the
`rm -f /tmp/*` line wait on nothing but an assertion, and **that excuse expired with C20** —
`kernel/test/multi` runs the script and asserts two of its lines already, so a third and a fourth
are a stage each in `multi.ini`. Whoever writes them should read what C20 learned about an expect
string an echo can satisfy, and note that a `fsck` which repairs the mounted root does not return.

---

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
| `ac.c`, `sa.c`, `accton.c` | 251 + 489 + 16 | Process accounting. The kernel side EXISTS and works — `acct(2)` is a real gate ([../kernel/acct.c](../kernel/acct.c), `<sys/acct.h>`), which is what makes this a decision rather than a gap. Nothing needs it: the machine has one operator, there is nobody to bill, and `sa`'s whole subject is digesting a record nothing on this image writes. It would also want a `/usr/adm` that [../root.manifest](../root.manifest) does not have, and a boot-time `accton` line in [../etc/rc](../etc/rc), which `kernel/test/multi` could assert since C20 but has nothing to assert about. Reconsider if this machine ever has more than one user who matters. |
| `random.c`, `sp.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |
