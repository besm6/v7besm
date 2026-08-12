# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet. The kernel's own plan is
empty; its reference is [../kernel/README.md](../kernel/README.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C10`, `C11`, … — because the kernel's task numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped: C1 through C11, C13 through C24, C26
and C28 are spent and their sections are gone, and no number is ever re-used, because `root.manifest` stanzas
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
**The one task left does not obey that rule and says so**: C29 is not a port at all but a defect
in a program that is already here and already tested.

| | task | what it buys | size |
|---|---|---|---|
| C29 | `sh` corrupts itself on a `case` arm holding a `while` and a pipeline | the last v7 script this tree wants to run, and confidence in every one it already runs | small |

**C29 is the only task left**, and it is the one this file most wants done: it is small, it is
reproducible in one command, and C23 found it the hard way — by shipping a script that had to be
rewritten around it. Everything else that could be ported has been, or is refused with a reason
in the table at the foot.

**What the finished tasks settled has moved to [README.md](README.md)**, under "What the
finished tasks settled" at its foot: the lessons C17 through C22 left behind, and the two loose
ends about the terminal that belong to no task. This file is the plan and that one is the
reference, which is the split the header above already claims.


---

## C29. `sh` corrupts itself on a `case` arm holding a `while` and a pipeline

**The one task here that is not a port**, and it is a defect in a program this tree already
ships and already tests. C23 found it: v7's `calendar(1)` is a `case $# in 0) … *) … esac`, and
written that way it does not run on this machine at all.
[`calendar/README.md`](calendar/README.md) has the bisection; what is here is the part a fix
needs.

**The repro**, on the booted machine — nothing smaller has been found, and every piece of it
works on its own:

```sh
case $# in
*)      sed 's/:.*//' /etc/passwd |
        while read x
        do
                if test -r /etc/motd
                then    egrep a /etc/motd | cat
                fi
        done
        ;;
esac
```

`** SIGNAL 8 **`, the shell's message tables and variable list printed to the console as text,
then `cannot shift` — from a builtin the script does not call. Take the inner `if` out and it
**hangs** instead. Replace the `case` with `if`/`else`, changing nothing else, and it is
correct; that is what [`calendar/calendar.sh`](calendar/calendar.sh) ships and why. Moving `;;`
onto a line of its own changes nothing, so it is not a `fi;;` tokenising problem.

**Where to look.** A corrupted data segment rather than a syntax error, and a single nesting
level deciding it, both point at the **4,096-word stack** (§6) and the recursion `execute()`
does over the parse tree — `case` is one `TFND` frame, and it is the frame that tips it. Measure
before assuming: the stack cannot be checked by `rootfs_sh_size`, which sees the four segments
and not the depth.

**Why nothing caught it.** [`sh/test/`](sh/test/) runs under `b6sim`, where no external program
can be `exec`'d, so **not one case in it contains a pipeline** — the ingredient this needs.
`case` is covered, `while` is covered, and the combination cannot be. A fix wants an assertion
that lives where pipelines run, which means `kernel/test/multi` (§9) and not `sh/test/`.

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `xsend/` | 414 | Secret mail. **Its stated reason expired with task C28**, which ported `mail` — the row used to read "needs `mail` first" — so here is one of its own: **the source is not in this tree at all**, there being no `xsend/` directory to port, and what it adds to `mail` is a public-key scheme whose `enroll`/`xget` half is equally absent. It is new work rather than a port, and nothing asks for it: [`crypt/`](crypt/) already carries this image's encryption and `mail` now carries its delivery. |
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
