# The BESM-6 Unix userland: what is left

The work plan for **`cmd/`** — the v7 commands this port has not got yet, and the companion of
[../kernel/TODO.md](../kernel/TODO.md).

**[README.md](README.md) beside it is the reference** — what is already here, the porting recipe,
the hazards a v7 source walks into on this machine, how a program gets onto the image and which
harness tests it. Read it before starting any task below; **nothing here repeats it**, and a bare
`§N` is a section of that recipe.

**Task numbers carry a `C`** — `C10`, `C11`, … — because `kernel/TODO.md`'s numbers are cited from
source comments and from `doc/`, and a bare number would be ambiguous forever after. The numbering
is **left as it was** when a task is finished and dropped: C1 through C9 are spent and their
sections are gone, and no number is ever re-used, because `root.manifest` stanzas and per-program
`README.md`s cite them.

**The contract per task**, as in the kernel file: it leaves `make` building and `ctest` passing,
and it leaves the program **on the image** — staged into `build/rootfs/`, named in
[../root.manifest](../root.manifest), and asserted by a test. A port is not done when it compiles.

**One task is one program**, and they are ordered so that each is unblocked by the one before it.

| | task | what it buys | size |
|---|---|---|---|
| C10 | `yacc` and `lex`, host and native | the seven grammars below, and a machine that generates its own parsers | large |
| C11 | `expr` | shell arithmetic; the first thing C10 proves | small |
| C12 | `egrep` | finishes C5c | small |
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

**Where to start: C10a.** Six of the tasks below are yacc grammars and one of those is a lex
scanner besides, so nothing after C10 can begin until `b6yacc` exists.

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
step 4 gives a line in that script exactly one home for its assertion, `kernel/test/console`, and
that test is DISABLED ([../kernel/TODO.md](../kernel/TODO.md) task 35). An unasserted line in the
boot script three tests walk through is worse than an honest deferral.

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

**This is the one task that starts by fetching.** [README.md](README.md) says every program named
here is already in its directory as a verbatim upstream copy; `cmd/yacc/` and `cmd/lex/` will be
the exceptions, and that sentence needs amending when C10a lands. Import verbatim all the same, so
that the first diff is the porting diff.

**Neither program needs a support library.** v7 shipped `liby.a` (a `main()` and a `yyerror()`) and
`libl.a` (a `main()` and a `yywrap()`); every consumer below defines its own, so nothing here adds
an archive to [../lib/](../lib/) or to `B6_STAGE_LIB`.

### C10a. `yacc`, the host tool `b6yacc`

`y1.c` 797, `y2.c` 989, `y3.c` 488, `y4.c` 368, `dextern.h` 292, `files.h` 21, and `yaccpar.c` 163
— 2,642 lines of C, 313 of header, and a 163-line data file that is not compiled. The v7 `yacc.1`
is not in the reference tree's `src/cmd/yacc/`; take it from `usr/man` or write one (§10).

* `cmd/yacc/CMakeLists.txt` on the [cpp/CMakeLists.txt](cpp/CMakeLists.txt) model —
  `add_executable(b6yacc y1.c y2.c y3.c y4.c)`, `install(TARGETS b6yacc RUNTIME DESTINATION bin)`
  — plus a line in the host-tool block at the top of [../CMakeLists.txt](../CMakeLists.txt).
* **`PARSER` becomes a path profile**, exactly as `cc.c`'s three paths are. `files.h` hardcodes
  `/usr/share/misc/yaccpar.c` and **RetroBSD's makefile installs the template nowhere at all**, so
  the tool as shipped cannot run. The host build installs it to `<prefix>/share/besm6/yaccpar.c`
  beside the header tree; a `B6YACCPAR` environment override, on the `B6CPP` precedent, is how a
  test points the tool off its search path before an install.
* **`yaccpar.c` has to be C11 before anything downstream compiles.** It is K&R — `yyparse()` with
  no return type, no prototype for the `yylex()` and `yyerror()` it calls — and it is copied
  **verbatim into the user's `y.tab.c`**, where `b6parse` will refuse it. Nothing in C11–C17 works
  until this file does.
* The mechanical part of §1 is about twenty zero-argument definitions written `void others()`
  rather than `(void)`, and `register` throughout.
* **Fix the latent overrun while porting.** `dextern.h` states in a comment that
  `TEMPSIZE >= NSTATES` must hold, and the selected `TINY` profile has `TEMPSIZE 200` against
  `NSTATES 750`; `y4.c` writes `yypact[++nstate]` into `temp1[TEMPSIZE]`. Any grammar over 200
  states corrupts memory in silence. Which end to move is C10c's measurement, but the assert
  belongs here — `_Static_assert(TEMPSIZE >= NSTATES, …)`, §12's habit applied to a program's own
  invariant.
* **A `b6_yacc()` helper** in [../scripts/BesmCross.cmake](../scripts/BesmCross.cmake), beside
  `b6_prog()`: run the in-tree `b6yacc` over a `.y` at build time and hand the `y.tab.c` to
  `b6_obj()`, so a rebuilt `b6yacc` regenerates every parser with no `make install`. **It needs a
  working directory per invocation**: yacc writes `y.tab.c`, `y.tab.h` and `y.output`, and its two
  temp files `yacc.tmp` and `yacc.acts`, under fixed names in the current directory, and a shared
  one races under `make -j` for the same reason `kernel/test/`'s per-program object dirs do.
* **Test.** A GoogleTest suite under `cmd/yacc/test/` on the [cpp/test/](cpp/test/) model for the
  diagnostics, and the real oracle beside it: `b6yacc` over each of the seven grammars C11–C17
  will feed it, compared against the host `yacc`'s output for what the two dialects share.

### C10b. `lex`, the host tool `b6lex`

`sub2.c` 952, `parser.y` 714, `sub1.c` 692, `lmain.c` 230, `ldefs.c` 166, `once.c` 131,
`header.c` 112, and the `ncform` skeleton 181 — 2,283 lines of C and a 714-line grammar.
**`parser.y` is built by `b6yacc`**, which is why this comes second and is C10a's first consumer.

**Drop the Ratfor half on the way in** — the `nrform` skeleton, the `ratfor` flag, `ratname`,
`rhd1` and `rtail`. There is no Fortran on this machine and `struct`/`ratfor` are already refused
below. Drop the dead `/share/lex/ebcform` reference in `lmain.c` with it.

**The C11 pass is the task, and it is nothing like yacc's.** Every function in all five files is
old-style, implicit-`int` returns are everywhere, and three things are worse than mechanical:

* **`error(s, p, d)` and `warning(s, p, d)` are varargs by abuse** — three untyped parameters,
  called with one, two or three arguments, handed straight to `fprintf`. Undefined behaviour that
  a PDP-11 calling convention happened to serve. `<stdarg.h>` and `vfprintf`, as RetroBSD's yacc
  already does.
* **`# ifdef unix` gates `NCH`, `CMASK`, `CWIDTH` and `ASCII`.** `b6cpp` does not predefine `unix`,
  so `ldefs.c` will not compile at all until this is unwound — the first thing to fix, and the
  thing that decides the next bullet.
* `ldefs.c` re-declares `calloc` and `myalloc` in the v7 manner, and `sub1.c` defines an
  `index(c, s)` with the arguments the other way round from the BSD one. §1's "names C11 reserved
  that v7 used freely", both of them.

**§11 has to be decided rather than inherited.** `NCH` is 128 and `CMASK` is `0177`, so this lex
masks bit 7 off every input byte; `nchar[]`, `symbol[]`, `match[]`, `cindex[]`, `extra[]`, `tch[]`
and `cwork[]` are all `char` holding values up to `NCH+105`. Either make it 8-bit clean or say in
its manual page that it is not — `awk` (C17) is the program that would notice, and a scanner that
silently turns `привет` into junk is §11's worst shape.

**`ncform` needs the same C11 treatment `yaccpar.c` does, and one hazard besides.** It is emitted
verbatim into every generated scanner, so `yylook()` and `yyback(p, m)` must gain prototypes — and
it compares `(int)yyt` against `(int)yycrank` and then forms `yycrank + (yycrank - yyt)`, a pointer
below the base of its own array. Both are §2: a cast to a thin pointer floors a fat one, and a
pointer formed below an array need not compare. Plain pointer relationals and `ptrdiff_t` indices.

Two smaller ones: `lmain.c` tests an allocation against `(char *)-1`, §2's fabricated pointer that
can never equal a real one; and `parser.y` allocates one combined block purely to ask whether there
is enough core and frees it immediately. Delete both.

`once.c`'s `long rcount` is the only `long` in the program and is printed with `%ld` — §3, make it
`int`. The skeleton path is a profile like C10a's (`<prefix>/share/besm6/lex/ncform`, `B6LEXFORM`
to override), and `b6_lex()` sits beside `b6_yacc()` with the same per-invocation working
directory, `lex.yy.c` being another fixed name.

**`ldefs.c` and `once.c` are headers with the wrong extension** — every other file `#include`s
them. Rename them, and remember §1: a header of the program's own is a build blind spot.

### C10c. `yacc` on the image — `/usr/bin/yacc` and `/usr/lib/yaccpar`

The C9 shape exactly: a `cmd/yacc/rootfs/` subdirectory (`cmd/yacc` is added above the
`libruntime.a` guard, where `b6_prog()` does not yet exist), the same sources built a second time,
`add_subdirectory(cmd/yacc/rootfs)` beside the eleven that are there, [../root.manifest](../root.manifest)
stanzas for the binary and the template, and `ROOTFS_FILES` in
[../kernel/test/CMakeLists.txt](../kernel/test/CMakeLists.txt).

* **A `besm6` size profile, measured rather than guessed.** `dextern.h` already has the mechanism —
  `HUGE`, `MEDIUM`, `TINY` — so a fourth profile keyed on the macro `b6cpp` predefines is the whole
  of the change. For scale, `TINY` costs about **10,540 words** of int and pointer arrays:
  `lkst[250]` 2,000, `mem0[1300]` 1,300, `amem[1000]` 1,000, and 3,052 in the four `NSTATES`-sized
  arrays, the rest scattered. Against 28,672 that leaves some 18,000 words for text, so **the
  address-space ceiling is not what binds here** — the tuning to make is `NSTATES` against
  `TEMPSIZE` (C10a's overrun), sized to the seven grammars this yacc will actually be given. None
  of them has more than thirty nonterminals.
* **`WORD32` stays undefined.** The alternative bit-packing macros shift by at most 15 and are safe
  on a 41-bit `int`; the `WORD32` set shifts by 31 and buys four words per lookahead set.
* **`y4.c` aliases three unrelated global tables as flat `int` arrays** — `lkst[0].lset`,
  `wsets[0].ws.lset` and `&nontrst[0].tvalue` — and walks the last with a stride of
  `sizeof(struct ntsymb) / sizeof(int)`, which is to say it assumes a pointer and an `int` are the
  same width. They are, here, so it works; §2 says to grep for exactly this shape, and the answer
  is to give `y4` arrays of its own rather than leave a correct-by-accident aliasing in the tree.
* The rest is easy: the only automatic array in the program is a `char actname[8]`, so §6's stack
  ceiling does not come near, and the two temp files are text-only and want `tmpfile()`, as `as`,
  `ld` and `strip` all use.
* **The agreement test is the one that matters.** Host `b6yacc` and native `/usr/bin/yacc` over one
  grammar, `y.tab.c` compared byte for byte, live — a checked-in expectation cannot say "these two
  builds agree". It joins `kernel/test/toolchain`, which already boots for exactly this claim,
  rather than taking volume 3103.

### C10d. `lex` on the image — `/usr/bin/lex` and `/usr/lib/lex/ncform`

Same shape, **different binding ceiling**. Nearly every table lex owns is `calloc`'d — the parse
tree, the state tables, the position sets — so `rootfs_lex_size` sees almost none of it and §6's
uncheckable heap is what decides. Measured, the peak concurrent heap at the shipped sizes is about
**11,840 words**: the parse tree 4,000 (`name`, `left`, `right`, `parent` at `TREESIZE` 1,000
each), the phase-2 state tables 6,500, `foll` up to 1,000, and the phase-1 buffers 336. Static data
is only about 700. **With the text that does not fit**, so a profile is not optional here:

* `SMALL` — already in `ldefs.c` — drops the peak to roughly 7,240 words.
* A `besm6` profile of about `TREESIZE 400, NSTATES 200, MAXPOS 1000, NTRANS 1000, NOUTPUT 1000`
  drops it to roughly 4,600.

**Measure against `awk.lx.l`** — the only scanner this machine has to compile — and size from the
high-water mark, §6's rule. Note that all five bounds are overridable per-`.l` with `%e %n %p %a
%o`, so a scanner that needs more can ask, which is the escape hatch a fixed profile usually lacks.
And **measure the break, not the request**: `malloc` grows a page at a time, so several of these
arrays round up.

The stack is fine but not trivial — `acompute()` carries `int temp[300], neg[300]`, 600 words, and
`cfoll()` recurses over the parse tree with small frames at a depth the input chooses. §6's last
bullet applies: give it a ceiling.

---

## C11. `expr`

`expr/expr.y`, 669 lines, one grammar and nothing else. **The smallest consumer of C10 and
therefore the one that proves it end to end** — do this before `make`, whatever the value ordering
says. Scripts want it almost as much as they want `test`, which is already on the image.

Its arithmetic is one word wide here (§3) and its `match` operator is a regular expression over
bytes (§11). A pure filter, so `b6_progtest` cases are the whole of the harness (§9).

## C12. `egrep`

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
is a `kernel/TODO.md` item nobody has written.

## C22. `cron`

`cron/cron.c` 254, plus the `cron/crontab` data file as `/usr/lib/crontab`. **The same blocker as
C21**, and the other [../etc/rc](../etc/rc) line — with the same §7 step 4 problem `update` has.

## C23. `calendar`

`calendar/calendar.c`, 54 lines. **Blocked twice**: on C21's clock, and on its data — what the
reference tree holds under `calendar`'s name is an x86 binary, not the database, so the file the
program reads would have to be written from scratch.

---

## Not ported, and why

Each row is a decision that can be re-examined; the line count is there so it can be.

| | lines | why not |
|---|---|---|
| `mail/`, `xsend/` | 556 + 414 | **Three decisions rather than a port.** `mail` wants a `/usr/spool/mail` directory [../root.manifest](../root.manifest) has not got and a mailbox **lock protocol** built out of the user execute bit (`lock()` sets `st_mode & 01` and spins); it includes `<whoami.h>` for a `sysname` this tree pruned on purpose ([../include/README.md](../include/README.md)); and its `REMOTE`/`FORWARD` arms hand the letter to `uucp`, which is refused below. There is one user on this machine and nowhere for a letter to go. `cmd/login` already probes for `/usr/spool/mail` with `access()` and quietly finds nothing, which is the behaviour to keep. `xsend` is secret mail and needs `mail` first. |
| `troff/`, `eqn/`, `neqn/`, `tbl/`, `refer/`, `deroff.c`, `prep/`, `checkeq.c`, `ptx.c`, `spell/` | 8,266 + 1,726 + 1,677 + 2,434 + 4,874 + 496 + 589 + 101 + 553 + 625 | The typesetting suite. `troff` drives a CAT phototypesetter that does not exist, and **there is no `nroff` in this source tree at all** — only `troff`. This repo's own manual pages are read with the *host* `nroff`, which is the right answer for the foreseeable future. `spell` additionally needs its whole word list. |
| `tp/`, `dump.c`, `restor.c`, `dumpdir.c` | 800 + 641 + 1,150 + 475 | Tape. **This kernel has no tape driver** and no `bdevsw`/`cdevsw` row for one, and all four are built around a tape's sequential access rather than merely willing to use it — `dump`/`restor` are a filesystem-level backup pair whose whole design is the reel. `tp` is the pre-`tar` archiver and is superseded by it in any case. If a magnetic-tape driver is ever written (a `kernel/TODO.md` item nobody has raised; [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) is the reference), reconsider `dump`/`restor` and not the other two. |
| `uucp/`, `cu.c` | 6,415 + 541 | Dial-out over a modem link nothing models. `cu` becomes conceivable only if the machine's serial multiplexor is ever driven and wired to something outside; no kernel task proposes that. |
| `lpr/`, `vpr.c` | 1,315 + 334 | Printer spooling. **Worth revisiting:** SIMH *does* model the АЦПУ drum printer, so `lpr` becomes a small task the day a kernel printer driver exists — which is a `kernel/TODO.md` item nobody has written yet. |
| `graph.c`, `plot/`, `spline.c`, `tc.c`, `tk.c` | 695 + 608 + 335 + 638 + 250 | Plotters and Tektronix terminals; no hardware, and the output would go nowhere. |
| `learn/` | 1,066 | Needs the entire `/usr/lib/learn` lesson corpus, which is not in this tree. |
| `adb/` | 3,547 | PDP-11 instruction decoding, PDP-11 core files, PDP-11 `ptrace` semantics. A BESM-6 debugger is **new work**, not a port — and [disasm/](disasm/) plus `ptrace` (kernel task 33) is where it would start. |
| `lint/`, `mip/`, `struct/`, `ratfor/` | 1,164 + 7,615 + 4,721 + 1,200 | `lint` and `mip` are the PDP-11 C compiler's own internals; `struct`/`ratfor` are Fortran-to-Ratfor tooling with no Fortran here — which is also why C10b drops lex's `nrform`. |
| `osh.c` | 846 | The pre-Bourne shell. [sh/](sh/) supersedes it. |
| `mknod.c` | 44 | **There is no `mknod(2)` in this kernel.** Every device node on the image is made by `b6fsutil` from [../root.manifest](../root.manifest), which is where a new one is added; a program that can only fail is worse than no program. Reconsider only if the gate is ever written. |
| `prof.c` | 310 | Reads a `mon.out` that nothing produces: `profil(2)` is `kernel/TODO.md` task 32, still undecided, and `cc` has no `-p`. |
| `cb.c`, `diff3.c`, `tabs.c` | 366 + 423 + 196 | Curiosities with a real cost and no caller. `cb` is a C beautifier superseded by this repo's own clang-format; `diff3` wants three files and a merge nobody does here; `tabs` sets hardware tab stops on terminals this machine does not have — the Consul typewriter is not one of them. |
| `cc.c`, `as/`, `ld.c`, `nm.c`, `ar.c`, `size.c`, `strip.c`, `ranlib.c`, `arcv.c` | | PDP-11 `a.out`, PDP-11 opcodes and PDP-11 registers; nothing in them survives retargeting. The BESM-6 tools were written for this repo instead, and task C9 built every one of them a second time for the machine itself — see each tool's "Building for the BESM-6". |
| `ac.c`, `sa.c`, `accton.c` | 251 + 489 + 16 | Process accounting. The kernel side EXISTS and works — `acct(2)` is a real gate ([../kernel/acct.c](../kernel/acct.c), `<sys/acct.h>`), which is what makes this a decision rather than a gap. Nothing needs it: the machine has one operator, there is nobody to bill, and `sa`'s whole subject is digesting a record nothing on this image writes. It would also want a `/usr/adm` that [../root.manifest](../root.manifest) does not have, and a boot-time `accton` line in [../etc/rc](../etc/rc) whose only assertion home is the DISABLED `kernel/test/console`. Reconsider if this machine ever has more than one user who matters. |
| `random.c`, `sp.c` … | | Curiosities. Port one if it is ever wanted; none is on a path to anything. |
