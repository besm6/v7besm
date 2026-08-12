# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. The kernel's own plan is
empty; its reference is [../kernel/README.md](../kernel/README.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C10`, `C11`, … — because the kernel's task numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped: C1 through C11, C13 through C22 and C26
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
| C23 | `calendar` | | small |
| C24 | the eight hand-rolled directory readers, over `opendir(3)` | one reader instead of eight, and §5 stops being everybody's problem | medium |

**Where to start: C23.** It is unblocked — its database is in [`calendar/calendars/`](calendar/)
— and `cron`'s crontab has a commented line waiting for it.

**What the finished tasks settled has moved to [README.md](README.md)**, under "What the
finished tasks settled" at its foot: the lessons C17 through C22 left behind, and the two loose
ends about the terminal that belong to no task. This file is the plan and that one is the
reference, which is the split the header above already claims.


---

## C23. `calendar`

`calendar/calendar.c`, 54 lines. Database is available in cmd/calendar/calendars/. 

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
