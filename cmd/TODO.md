# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet, and the companion of
[../kernel/TODO.md](../kernel/TODO.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C9a`, `C10`, … — because `kernel/TODO.md`'s numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it compiles.

| | task | what it buys | size |
|---|---|---|---|
| C9 | self-hosting — native `cpp`, `as`, `ld`, the binutils, `cc` | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` `mail` | a system worth using | open-ended |

**Where to start: C9a's `cpp`.** It waited on a foreign repository until the front-end bug that
gated it was fixed upstream; the libc gap beside it is closed too, so what is left in
[cpp/TODO.md](cpp/TODO.md) is one codegen bug with a local workaround and two size limits this
repo owns — a plan that can now be executed end to end. C10 is still behind the `yacc` decision
its own section names; `expr.y` is the smallest thing in front of that decision, and scripts want
it almost as much as they want `test`.

**Two loose ends about the terminal, one line each and neither worth a task of its own.** `TANDEM`
is honoured by the kernel — `ttyblock()` queues the stop character when the input queue passes
`TTYHOG/2` and `canon()` sends the start character back — and **no program on this image can set
it**, the cut in [stty/README.md](stty/README.md) being deliberately subtractive.
`TIOCEXCL`/`TIOCNXCL` and `TIOCHPCL` are the other shape: `ttioccomm()` accepts all three and sets
`XCLUDE` or `HUPCLS`, and **nothing in the kernel ever tests either bit**, so an exclusive-open
request and a hang-up-on-last-close request are both silently ignored. Both are worth knowing
before somebody reports them as bugs.

**[../etc/rc](../etc/rc) still wants four lines.** `cron` and `update` wait on C10. A boot-time
`fsck` and the `rm -f /tmp/*` line wait on something else: both programs exist, but §7 step 4 gives
a line in that script exactly one home for its assertion, `kernel/test/console`, and that test is
DISABLED ([../kernel/TODO.md](../kernel/TODO.md) task 35). An unasserted line in the boot script
three tests walk through is worse than an honest deferral.

---

## C9. Self-hosting: the toolchain on the machine itself

**State the exclusion first, because it is the whole shape of this task.** v7's `cc.c` (387),
`as/` (4,095), `ld.c` (1,257), `nm.c` (229), `ar.c` (707), `size.c` (48), `strip.c` (113),
`ranlib.c` (160) and `adb/` (3,547) **are not ports.** They speak PDP-11 `a.out`, PDP-11 opcodes
and PDP-11 registers; nothing in them survives retargeting. The BESM-6 versions already exist, in
this directory, and — with the exception of `cmd/sim` and `cmd/fsutil`, which are C++ and therefore
out of reach until there is a C++ compiler — **they are all plain C**. So the task is not to port
anything: it is to build what is already here a *second* time, for the target.

### C9a. `cpp`

**See [cpp/TODO.md](cpp/TODO.md)**, which is a complete plan already: one codegen bug in the
external compiler (B1) and two address-space limits (L1, L2), each with a minimal repro. Do not
restate any of it here. All eight sources now reach `b6as`, so what stops `cpp` is size and how
one large struct is addressed — and B1 reaches far beyond `cpp`, since every tool below keeps
tables of the same shape.

### C9b. `as`, `ld`

[as/](as/) (12 sources) and [ld/](ld/) (9). Both plain C, both already reading and writing this
machine's `a.out` through [libaout/](libaout/), which builds natively too. Expect the same limits
`cpp` hit: a symbol table that must live inside 32,767 words, frames that must fit 4,096, and
B1 — a member of a global struct past word 4,095 is unreachable, which is a table of any size.
Both are already designed around fixed tables rather than unbounded growth, which helps.

### C9c. The binutils and the driver

`ar`, `nm`, `size`, `strip`, `ranlib`, `lorder` (a shell script, so free), and `cc` — the driver,
which needs nothing but `fork`/`exec`/`wait` and a path search. Once `as` and `ld` run on the
machine, this is the short tail that makes them usable.

**What it does not include.** `b6sim` and `b6fsutil` are C++ and stay host-only. `b6disasm` is C
and could come along for free.

**Size.** Large, but no longer blocked on a foreign repository: the one upstream bug left
([cpp/TODO.md](cpp/TODO.md) B1) has a local workaround, and everything else is size. And it is
the task that changes what this port *is*.

---

## C10. The rest of the manual

Everything else worth having, in no fixed order.

**Settle the `yacc` decision before starting anything, not during, because six of these are yacc
grammars** — `expr.y` (669), `egrep.y` (594), `bc.y` (600), `make/gram.y`, `m4/m4y.y` and
`awk/awk.g.y` — and `awk` is a **lex** scanner besides (`awk.lx.l`). There is no native `yacc` and
no native `lex`; v7's own are 2,249 and 2,980 lines and would each have to be ported first, which
is a worse deal than any program they would generate. Two ways out: check the generated C into the
tree beside the `.y`, or add a host `yacc`/`bison`/`flex` dependency to the build.
**Recommend checking in the generated parser**, with the grammar beside it and a note in the
program's README saying which host `yacc` produced it — the build stays dependency-free, which is
a property this project has kept so far and should not spend lightly. This catches `make` and `m4`,
the two most valuable items in the table.

**`mail` carries three decisions rather than a port.** It wants a `/usr/spool/mail` directory that
[../root.manifest](../root.manifest) does not have and a mailbox **lock protocol** built out of the
user execute bit (`lock()` sets `st_mode & 01` and spins); it includes `<whoami.h>` for a `sysname`
this tree pruned on purpose ([../include/README.md](../include/README.md)); and its
`REMOTE`/`FORWARD` arms hand a letter to `uucp`, which is in the exclusion table below. So:
re-import `whoami.h` or hard-code the system name, add the spool directory and its mode, and cut
the remote arm or leave it failing. `cmd/login` already probes for `/usr/spool/mail` with
`access()` and quietly finds nothing, which is the behaviour to keep until the day this is done.

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
| `at.c`, `atrun.c`, `cron.c`, `calendar.c` | scheduling | 307 + 110 + 254 + 54 | want a correct clock, which this machine has not got — `iinit()` seeds `time` from the root superblock. `cron` is one of the two [../etc/rc](../etc/rc) still names |
| `update.c` | periodic `sync` | 38 | trivial, and [../etc/rc](../etc/rc) names it — but it is a **daemon**, and `/etc/rc` runs on every pass through `init`'s loop, so weigh a second copy per pass before adding the line |
| `strip`, `size`, `nm` | | | **not these** — see C9 |

---

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` drives a CAT phototypesetter that does not exist, and **there is no `nroff` in this source tree at all** — only `troff`. This repo's own manual pages are read with the *host* `nroff`, which is the right answer for the foreseeable future. `spell` additionally needs its whole word list. |
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
