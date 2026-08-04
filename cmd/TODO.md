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
| C9 | self-hosting — the driver (C9e); `cpp`, `as`, `ld`, the read-only binutils and the archive pair are done | building the system on itself | large |
| C10 | the rest of the manual — `make` `m4` `awk` `bc` `dc` `expr` `egrep` `units` `crypt` `at` `cron` `calendar` `update` `mail` | a system worth using | open-ended |

**Where to start: C9e, the `cc` driver — and it is the last of C9.** C9a through C9d are done:
`/usr/bin/cpp`, `as`, `ld`, `nm`, `size`, `strip`, `disasm`, `lorder`, `ar` and `ranlib` are on
the image, each built from the same sources as its host tool, and **the machine now assembles,
links, archives and indexes its own programs and reads back what it built** — the native `as`
and `ld` reproduce the host tools' objects and images byte for byte for the whole kernel and
for the toolchain itself, and the native `ar` and `ranlib` build `libc.a`, archive and index
both, byte for byte too. What that cost is the lesson for what is left, and the "Building for
the BESM-6" sections are where it is written down —
[cpp/README.md](cpp/README.md) for the size profile and the stack, [as/README.md](as/README.md)
for the 48-bit word in a machine with no `int64_t`, [ld/README.md](ld/README.md) for a
50,000-word struct and twelve stdio buffers, [nm/README.md](nm/README.md) for a heap step that
`rootfs_<name>_size` cannot see, [strip/README.md](strip/README.md) for an 8-kilobyte buffer on
a 4,096-word stack, [ar/README.md](ar/README.md) for the `mkstemp()` libc gained and a program
the ceilings did not touch at all, [ranlib/README.md](ranlib/README.md) for an agreement test
whose output cannot be made deterministic. **Three of the nine needed no target change at all**
([size/](size/), [disasm/](disasm/) and [ar/](ar/)), which is the measure of how much of the
difficulty was the three big ones.
C10 is still behind the `yacc` decision its own section names; `expr.y` is the smallest thing in
front of that decision, and scripts want it almost as much as they want `test`.

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

**C9a through C9d are done**; `/usr/bin/cpp`, `as`, `ld`, `nm`, `size`, `strip`, `disasm`,
`lorder`, `ar` and `ranlib` are on the image, and the "Building for the BESM-6" sections are
what they left behind. The numbering below is left as it was.

**What they established, and C9e inherits.** The `b6_prog()` recipe needed no
change for any of the nine. What did:

* **No struct above 4,096 words**, every time. `struct cppstate` was ~38,600, `struct assembler`
  ~28,300, `struct linker` ~50,400 — the last of those half again the whole address space. The
  answer is always to lift the big arrays to file scope, never to shrink them in place.
* **A size profile keyed on `besm6`, measured rather than guessed.** Instrument the host tool,
  run it over this repo's own sources, and size from the high-water mark.
* **The heap, which `rootfs_<name>_size` cannot see.** `ld` holds twelve stdio streams open and
  at the default `BUFSIZ` that alone is 6,144 words; it sets its own buffer size.
* **Three compiler and libc facts, all silent.** `b6lower` ignores designated initializers and
  initializes *positionally*; a string literal cannot initialize a `char *` inside a struct
  initializer at all; and there is no `int64_t` — only what really holds a 48-bit word becomes
  `uword_t`, everything narrower stays `word_t`. An **automatic** aggregate initialised from
  runtime values (`unsigned char b[6] = { i >> 40, … }`, `char *av[] = { "ar", … }`) had no
  precedent in this tree before C9d and turns out to be correct; the check cost two `b6cc` runs
  and would have caught a wrong `ARMAG` at the head of every archive.
* **The agreement test is the one that matters.** Host tool and native program over one fixture,
  the outputs compared live; a checked-in expectation cannot express "these two builds agree".
  **When one field of the output cannot be deterministic, mask that field and assert the rest,
  and find a second comparison with nothing masked at all.** `ranlib` stamps `__.SYMDEF` with
  the time of day: `rootfs_ranlib_*` compares the archive either side of those six bytes and
  `rootfs_ranlib_symdef` compares the index *member*, which carries no timestamp. Anything C9e
  timestamps will want the same shape.
* **The build lives in `cmd/<x>/rootfs/`**, a subdirectory and not more lines in
  `cmd/<x>/CMakeLists.txt`, because the host tool is added above the `B6RUNTIME_LIB` guard where
  `b6_prog()` does not yet exist. [ld/rootfs/CMakeLists.txt](ld/rootfs/CMakeLists.txt) is the
  commented model; copy it, not the older `as` one.
* **Two lists must grow with the program**, and only a failing test catches either:
  [../root.manifest](../root.manifest) and `ROOTFS_FILES` in
  [../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt). The `ls /bin` expectations
  `../README.md` §7 also names do not apply to anything in `/usr/bin`.
* **A small program is mostly `stdio`.** C9c's four came to 5,068–6,439 words against a 28,672
  ceiling, C9d's two to 8,504 and 11,333, and `_doprnt` alone is a 281-word stack frame — half
  of `nm`'s deepest path. The ceilings bind the big three and nothing else; measure anyway, but
  expect the answer.
* **A libc gap is worth filling in libc.** C9d wanted `mkstemp()`, which is not v7's and not
  C11's, and `ar` could not take `tmpfile()` the way `as`, `ld` and `strip` had — it wants the
  temp file's *name* and a descriptor it can read back. Putting it in
  [../lib/libc/gen/mkstemp.c](../lib/libc/gen/mkstemp.c) rather than behind an `#if besm6` in
  the program left `cmd/ar`'s sources character-identical in both builds, which is the property
  the whole of C9 is arranged around. Note what this kernel can and cannot promise: there is no
  `O_CREAT` and no `O_EXCL`, so the creation is not atomic and the man page says so.

### C9e. The driver: `cc`

`cc` (791 lines) — argument parsing, a path search, and `fork`/`exec`/`wait`. Two changes and one
honest limitation:

* **`posix_spawn()` and `waitpid()` do not exist on this machine.** `run()` uses both today;
  [../include/sys/wait.h](../include/sys/wait.h) records that this kernel has only `wait(2)` — no
  `wait3()`, no `waitpid()`, no `WNOHANG`. The native driver is `fork` + `execv` + `wait`, which is
  what v7's own `cc.c` did.
* **The prefix search is host-shaped and must be keyed on `besm6`,** exactly as the size profiles
  are. `find_tool()` walks `$HOME/.local/bin` then `/usr/local/bin`; `besm6_include_dir()` and the
  default `-L` walk `share/besm6/` under either. On the image those are `/usr/bin`, `/usr/include`
  and `/lib`, and the `B6CPP`-style environment overrides should survive unchanged.
* **It cannot compile C, and its README must say so.** `cc -E` works on the machine today, and
  so do `-S`, `-c` on a `.s` file and a link, C9d having put `ar` and `ranlib` there; `cc` on a
  `.c` file will not,
  because `b6parse`, `b6lower` and `b6codegen` are not here — they live in the external
  [c-compiler](https://github.com/besm6/c-compiler/) repo, which this one cannot add a
  `b6_prog()` to, and no task above proposes bringing them over. A driver that assembles and
  links is still worth having — it is what `make` will call — but shipping it without that
  sentence would be shipping a program that fails on its most obvious input.

**Size.** Small once the decision above is written down.

### When C9 is done

The closing test is the bootstrap: **the machine builds its own toolchain from its own sources, and
the result is byte for byte what the host build produced.** That is already true for objects and
images — the native `as` and `ld` reproduce the host tools' output for the whole kernel and for the
toolchain itself, and since C9c the machine can read back what it built, `nm`, `size` and `disasm`
agreeing with their host tools over the same files -- and since C9d for the archive those objects
go into, the native `ar` and `ranlib` building `libc.a`, index and all, byte for byte too. What is
left is one link, C9e, the driver that runs the chain. **The last link is not here**: the
compiler proper is the external repo's, so `cc` on a `.c` file is the one step of the chain this
task cannot close.

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
| `strip`, `size`, `nm`, `ar`, `ranlib` | | | **not these** — the BESM-6 ones have been on the image since C9c and C9d |

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
