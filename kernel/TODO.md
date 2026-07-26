# The BESM-6 Unix port: what is left

The work plan. **[README.md](README.md) is the reference** — where the port stands, the design it
settled on, the five hardware rules, the u-area invariant, what a standalone SIMH test costs to get
right, the gotchas, and the consequences deliberately accepted. Read it before starting any task
below; nothing here repeats it.

Each task leaves the tree building (`cd kernel && make`) and the suite passing (`ctest -L kernel`).
Verification is under SIMH via `test/*.ini` — `b6sim` runs a user `a.out` with no kernel underneath
and cannot exercise any of this. `test/mmutest` is the model to copy, and every MMU test runs with
`set mmu cache`; README.md's "Writing a standalone SIMH test" is the rest of that story, including
why `besm6.o` cannot go into one.

**Tasks 1 through 27 are done and their writeups have been removed**; the design they settled on is
README.md, and how each turned out is in the source comments and in [../doc/](../doc/). The
numbering is **left as it was** — task numbers are cited from the sources and from `doc/`.

Task 29 is the road ahead and is the size of everything before it. 28 and 30–34 are small,
independent, and were deferred deliberately.

| | task | size |
|---|---|---|
| 28 | a bulk copy path for unaligned `read`/`write` | medium, high risk |
| 29 | multiuser: the terminal driver, `getty`/`login`, a test | large |
| 30 | copy only the live part of the u-area | small, measurable win |
| 31 | the kernel-stack depth check | small |
| 32 | `profil()`: implement `addupc()` or make it fail | small; the decision is the task |
| 33 | `ptrace` single-step | small now, blocked after |
| 34 | the `int` ↔ pointer audit | open-ended |

---

## 28. A bulk copy path for unaligned `read`/`write`

**Where.** `iomove()` in [rdwri.c](rdwri.c); `copyin`/`copyout` in [usermem.S](usermem.S);
`useracc()` in [utab.c](utab.c).

**What is wrong.** `copyin`/`copyout` are **word-only**: they mask the pointer with `aax #077777`
and drop the byte offset entirely. `iomove()` therefore takes them only when `n % NBPW == 0` and
both pointers stand on byte #0 of their word, and otherwise moves the block **one byte at a time**
through `cpass`/`passc` — that is one `fubyte`/`subyte` per byte, each a full `useracc()` range check
plus a mode-toggle bracket. Every stdio buffer is byte-granular, so the slow path is the ordinary
one for user I/O; the fast path is taken essentially only by the kernel's own struct copies.

(The alignment *test* is no longer the bug it was. v7's `(n & (NBPW-1)) == 0 && (int)cp & (NBPW-1)`
was silent data corruption here — `NBPW` is 6, not a power of two, and a `caddr_t`'s byte offset
lives in bits 47–45, so the mask could not see it. The block comment at `iomove()` is the writeup.)

**What to do.**

1. **Measure first.** Add a counter on each arm of `iomove()`, boot, run `test/session` and
   `libtest`, and read the two numbers out with `ex`. The work below is only worth doing if the byte
   arm dominates, and the measurement belongs in the commit message either way.
2. Give `copyin`/`copyout` byte offsets. The tractable case is **equal phase** — both pointers at
   the same byte within their word — which is the common one, and needs only a partial leading word,
   the existing whole-word loop, and a partial trailing word, each edge a read-modify-write on the
   destination side. The general case is a shifting copy across word boundaries; leave it on the
   `cpass`/`passc` path until the measurement says otherwise.
3. `iomove()` then calls the bulk path whenever the phases match, whatever `n` is.

**How to verify.** Write the bite test first. A new standalone `test/umem` (crt0 plus `usermem`,
`utab` and `brz`, in the shape of `mmutest`) with a forged user map, covering all 6 × 6 start phases
× lengths 0–13 and one copy that crosses a page boundary, and asserting the bytes *outside* the
range are untouched — the last is the part a round trip would miss. Then `libtest` and `session`
must be byte-identical, and they are the ones that found the previous bug here.

**Size.** Medium, and higher risk than it looks: this routine has already produced one silent
data-corruption bug. Do not touch it without the bite test.

---

## 29. Multiuser

Today the machine has one terminal, the operator's Consul ([dev/sc.c](dev/sc.c)), and `/etc/init`
runs the single-user loop only: with no `/etc/ttys` on the image `merge()` returns as soon as the
open fails and `multiple()` falls straight through. [dev/sr.c](dev/sr.c) is a skeleton — the tty
scaffolding and the `cdevsw` surface exist, and nothing behind them talks to the multiplexer.

Three pieces, in order. 29a is kernel work and is the interesting one; 29b is userland ports in the
[../cmd/sh/README.md](../cmd/sh/README.md) mould; 29c is the test that makes the other two mean
something.

### 29a. The terminal driver

**Pick the interface: the serial multiplexor, not the telegraph channels.** The `TTY` device offers
both, and they are not comparable in cost:

* The **24 telegraph lines** (`033 0140` write, `033 4100` read) are bit-serial and polled — one bit
  per line per bit time, with the start / 8 data / stop framing run in software off `GRP_SERIAL`
  (ГРП bit 19, raised only if already enabled in МГРП) at `set tty rate` Hz. That is an interrupt
  per bit per line and a software UART in the kernel.
* The **serial multiplexor** (`033 0143` write, `033 4143` read, `033 0153` clear) is
  character-at-a-time and interrupt-driven, reporting in ПРП — which is exactly the shape of the
  Consul, and exactly the path [intr.c](intr.c)'s `prpintr()` already dispatches. `dev/sr.c` becomes
  `dev/sc.c` with a line number in the syllable.

**The work.**

1. **[../include/sys/besm6dev.h](../include/sys/besm6dev.h)** gains the registers and bits, in the
   existing naming: `EXT_MUX 0143` (write a syllable), `EXT_MUX_RD 04143` (read the syllable back),
   `EXT_MUXCLR 0153` (clear the interface), `PRP_MUX_INPUT 0100` (bit 7) and `PRP_MUX_DONE 040`
   (bit 6).
2. **Syllable format**, 16 bits: line number in bits 9–16, character in bits 1–7. Bit 15 (`040000`)
   marks a **control** syllable; with bit 8 (`0200`) also set it is a line-status request, answered
   in the syllable register with the receive state in bit 4 and raising `PRP_MUX_INPUT`; without it,
   bit 4 set **disables** reception on the line and clear enables it. An input syllable carries the
   line in bits 9–16 and the character in 1–7 with **odd parity in bit 8**, so the driver masks
   `0177`.
3. **`srintr()` beside `scintr()`** in `prpintr()`, and `mprpon(PRP_MUX_INPUT | PRP_MUX_DONE)`.
   `GRP_SLAVE` is already unmasked for the Consul, and the ordering rule is the same one `intr.c`
   states: clear the ПРП bit *before* dismissing `GRP_SLAVE`, or the handler storms.
4. **Output is one engine for all 24 lines.** `PRP_MUX_DONE` does not say which line finished. So
   keep **one character in flight** across the whole device, remember whose it was, and on each DONE
   complete that line and round-robin to the next line with a non-empty `t_outq`. `t_addr` already
   holds the line number. Per-line `BUSY` in `t_state` is not enough by itself.
5. **Input is one register for all 24 lines, and reading it does not free it.** `033 4143` returns
   the syllable but leaves the simulator's busy flag set; only `033 0153` clears it — and that also
   zeroes the syllable, clears `PRP_MUX_INPUT` and *raises* `PRP_MUX_DONE`. So the receive path is
   read → clear → absorb the DONE the clear itself raised. While the register is busy the simulator
   collects from no line at all; characters are not lost (they wait in the telnet buffer) but the
   handler must not defer.
6. **No polling clock.** `vt_clk()` reschedules itself at the line rate and calls `mux_receive()`
   whether or not `GRP_SERIAL` is enabled in МГРП. Do **not** unmask `GRP_SERIAL`; there is nothing
   for the kernel to do at bit rate. Nor `GRP_SLOW_CLK` (ГРП bit 10, 62.5 Hz), which is what the
   historical OS used to prod terminal I/O — the mux is interrupt-driven through ПРП here, so there
   is nothing to prod. README.md's gotchas say why that bit is not the Unix tick either.
7. **Raise `NSR`** from 2 to the number of lines wanted and add the nodes to
   [../root.manifest](../root.manifest) — `cdev /dev/tty01`… against `cdevsw[3]`, minor = line
   number.

**Two simulator conditions that will look like driver bugs.**

* A line is served only if it is **attached** (`attach tty3 4203`) *and* carries the **`mux` unit
  flag** (`set tty3 mux`). Without both, `mux_receive()` skips it and nothing says why.
* On the mux path the **simulator echoes** the character back to the line itself, and folds DEL to
  BS, before handing it to the kernel — unlike the Consul path. An `sr` line must therefore run with
  the kernel's `ECHO` **off** or every character doubles. Decide this before writing `srparam()`,
  because it changes what the `t_flags` default is.

**Also fix while there.** [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) says `033 0153`
"sets bit 6 of МПРП". It sets bit 6 of **ПРП** (`PRP_MUX_DONE`) — `mux_clear()` in `besm6_tty.c`
touches no mask.

**How to verify.** `test/srtest`, in the shape of `sctest`: link `sr` + `tty` + `prim` + `partab`
against a crt0, attach two mux lines, and drive the *device* (not a booted kernel) — send a
character out on line 2 and one on line 3 and assert both arrive on the right lines, which a
single-line test cannot distinguish. That is the same "leave two different patterns and read the
region back whole" argument `mbtest` needed.

**Size.** Medium. The driver is small; the register semantics above are the whole of the risk.

### 29b. `getty`, `login`, and `/etc/ttys`

Two more native programs in the `b6_prog()` mould, `cmd/getty` and `cmd/login`, ported from v7 —
strict C11, and the `int`-is-not-a-`char *` hazards that
[../cmd/sh/README.md](../cmd/sh/README.md) and [../cmd/ls/README.md](../cmd/ls/README.md) enumerate.
The libc side is already there: `crypt`, `getpwnam`/`getpwent`, `ttyname`, `getlogin` and
`<utmp.h>` are all in [../lib/libc/](../lib/libc/), which `init.c` already uses.

* **`/etc/ttys`** joins the static files in [../etc/](../etc/) and gets a `file` line in
  `root.manifest`. One line per enabled terminal: `'1'` to run a getty, then the character handed to
  getty as its speed selector, then the device name — the format `rline()` in
  [../cmd/init/init.c](../cmd/init/init.c) already parses.
* **`/etc/passwd`** gets a real encrypted field. Start with an empty password so a failure is in one
  place at a time, then add one and keep the plaintext in the test script, not in the image.
* **`init`'s multiuser half needs no work** — `merge()`, `multiple()` and `dfork()` are the v7 ones
  and are already ported; they simply have nothing to read today. Expect the first bug to be in what
  `merge()` does on a *second* pass, which is the half no single-user boot has ever run.
* Both programs are subject to the `rootfs_<name>_size` ceilings (28,672 words of address space, no
  relocatable symbol above word 32,767), registered automatically by `b6_prog()`.

**Size.** Medium, and mostly mechanical — but do it *after* 29a, so that a login that never prompts
is a userland bug and not a driver one.

### 29c. The multiuser test

`test/multiuser`: boot the full kernel with two mux lines attached, log in on both, and assert that
a logout brings a fresh `login:` — which is what proves `init`'s `multiple()` loop, the piece no
existing test reaches.

SIMH's `expect`/`send` **do work per line** — the `<device>:<line>,"string"` form, e.g.
`expect TTY:3,"login: "` and `send TTY:3,"root\r"` (`_tmxr_locate_line_send_expect()` in
`sim_tmxr.c`). The open question to settle first is how to get the line **connected** without a human
telnet client, since `mux_receive()` skips a line whose `conn` is 0: the candidates are a host-side
helper alongside the simulator (`run-session.sh` already establishes that pattern), TMXR's loopback
mode if the BESM-6 `TTY` device exposes a `set` modifier for it, and `attach ttyN Connect=…`.
Resolve that before writing anything else, because it decides the shape of the whole test.

Note README.md's caveat about `send` dropping a character under parallel ctest: a `send`-driven test
that fails once and passes when re-run alone has said nothing.

**Size.** Small once the connection question is answered; blocked on it until then.

---

## 30. Copy only the live part of the u-area

`uflush`/`uload` ([uarea.S](uarea.S)) copy the whole `USIZE` = 1024-word page, twice per context
switch. The live content is `struct user` (~140 words) plus the stack **below** `r15` — the stack
grows up, so everything above `r15` is dead. Measured: the deepest `resume()` in a boot →
`/etc/rc` → shell → `ls /bin` run had `r15 = 075302`, i.e. 706 words live; a typical one is ~300.

**What to do.** Pass a word count. `uflush` knows it — the current `r15`. `uload` does not, and
cannot be told by `resume()`, which has not yet read the incoming process's state: it must store the
count *into* the saved page at a fixed offset (a new `u_stkdepth` in `struct user`, or a word next to
the resume label) and read it back through the `WHOME` window before the copy. Round the count up to
a whole number of words and leave slack.

**Where it bites.** The count must cover the deepest frame that can ever be *resumed*, not the one
current when `uflush` runs. Those are the same thing while interrupts are off — which `uload` already
requires and `uflush` should assert — but they stop being the same the moment anything calls `uflush`
with delivery open. `DIRSIZ` moving `u_upt` is the other trap: `UPT` is hardcoded in `uarea.S`,
`seg.S` and `mmutest.c`, and any new field in `struct user` moves it.

**How to verify.** `uswtch` and `mmutest` unchanged; add a leg that writes a sentinel just below
`r15` and another well above it, switches away and back, and asserts the first survives — and that
the second does *not* have to. Then `boot` and `swap`, which are where a lost frame shows up as a
hang rather than a failure.

**Size.** Small, and the payoff is on every context switch. It is the only item on this list with a
measurable win in the ordinary path.

---

## 31. The kernel-stack depth check

There is **no guard page and none is possible.** `r15` grows up from ≈ `074214` to `0100000`, and
past that a 15-bit address wraps to 0 — into the interrupt vectors. The kernel runs unmapped, so
neither РП nor РЗ applies to it; the only mechanisms are a software depth check or the simulator.
Task 25a made the wrap unreachable by any measured path, but two one-line checks were left unwritten:

```c
if ((int)&local >= UBASE + USIZE)   panic("kstack");   /* in sleep() — the one the geometry needs */
if ((int)&local >= 0100000 - MARGIN) panic("kstack");  /* the wrap */
```

The first is the important one: `sleep()` (and `swtch()`) is exactly where a frame in the overflow
page is silently lost, because `uflush()` copies only `USIZE` words and another process then runs deep
on the same physical page. There is no fault and no diagnostic — see README.md's consequences list.

**Decide first whether it earns its keep**, because SIMH already observes both without kernel code: a
write watchpoint on the overflow page (`break -w 0176000-0177777`) fires only on an *unmapped* store,
i.e. only on a kernel stack frame, because `mmu_store()` ORs `0100000` into the address before
`sim_brk_test`; and `break <resume>; ex M17` samples the depth at every switch. The argument for the
runtime check is that it fires on a real workload nobody is watching, and it costs one comparison per
sleep.

**How to verify.** A standalone test that forges a deep frame and sleeps in it must panic; without
that, the check is decorative. A test that cannot fail proves nothing about the geometry.

**Size.** Small.

---

## 32. `profil()`: implement `addupc()` or make it fail

`addupc()` is a stub ([besm6.S](besm6.S):773), so `profil(2)` is accepted, records nothing, and says
nothing. Its three callers ([clock.c](clock.c), [syscall.c](syscall.c), [trap.c](trap.c)) are all
guarded by `u.u_prof.pr_scale`, so today the stub is never reached with work to do.

**Decide the direction first.** Nothing in userland profiles: there is no `monitor()` or `mcount()`
in [../lib/libc/](../lib/libc/), `b6cc` has no `-p`, and no `prof(1)` is ported. So the cheap honest
move is to make `profil()` fail — `EINVAL` for a non-zero scale — and delete the stub, leaving one
line saying what would have to exist first. Implementing it is only worth doing behind a libc
`monitor()` and a host-side `prof`.

**If it is implemented:** v7's `addupc` indexes an array of 16-bit shorts by a byte offset, scaling
`pc` by a 16-bit binary fraction. Neither survives: the profile buffer here is an array of **words**,
and the bucket index is a word index, so `pr_scale`'s definition and `<sys/user.h>`'s `struct uprof`
have to be restated before any code is written. The update also lands in the *user's* buffer while
the kernel is unmapped, so it goes through `fuword`/`suword`, not a store — and it happens at clock
interrupt, so it must not fault.

**Size.** Small either way; the decision is the whole task.

---

## 33. `ptrace` single-step

Marked `TODO 33` at [sig.c](sig.c) (cases 6 and 9), [trap.c](trap.c) (the `GRP_BREAKPOINT` arm) and
`GRP_BREAKPOINT` in [../include/sys/besm6dev.h](../include/sys/besm6dev.h).

**Why it is not a flag bit.** v7's `PT_STEP` sets the PDP-11 T-bit and the hardware traps after one
instruction. This machine has no such bit. What it has is a pair of debug registers
([../doc/Memory_Mapping.md](../doc/Memory_Mapping.md) §13): **ИБП** (`M[034]`, КРА) is an *execute
breakpoint* — one address — and **ДВП** (`M[035]`, ЗПСЧ) is a data watchpoint with `PSW_WRITE_WATCH`
selecting write- or read-match. They raise ГРП bits 12, 16 and 17, and they match the *tagged*
address, so they follow the current mapping mode.

A breakpoint register is not a single-step: stepping with it means decoding the instruction at the
resume PC, computing the successor and arming `M[034]` on it — and a conditional branch has two
successors against one register. So `PT_STEP` needs either an instruction decoder in the kernel or a
different contract with the debugger.

**Today it is worse than unimplemented: it lies.** `procxmt()` case 9 falls through to case 7 and
resumes the process with no trap armed, so a debugger waits forever for a stop that will never come.

**Do this much now** (small, and independent of everything above): make case 9 return `EIO` rather
than fall through, and say so in one comment at each of the three sites.

**The rest is blocked on a user.** No debugger is ported — no `adb`, no `sdb` — so there is nothing
to hold an implementation to. When one arrives, the design question to settle first is whether to
offer hardware breakpoints as their own `ptrace` request (`M[034]` armed at an address, re-armed by
`procxmt()` after each match, since there is no flag to clear) or to keep v7's ABI and pay for the
decoder.

**Size.** Small now; the full version is a task of its own and should be re-scoped when a debugger
exists.

---

## 34. The `int` ↔ pointer audit

Both bugs `libtest` found on its first run were one pattern in two disguises — **v7 packs flags into
spare low bits of addresses, and this machine has none** — and both sat in code every other test
walked straight past. The rule and the two worked examples are in README.md's gotchas. What is *not*
done is the sweep.

**What to look for.** Two distinct mistakes:

* **A mask on `(int)ptr`.** A `caddr_t` is a fat pointer — marker in bit 48, byte offset in bits
  47–45 — so `(int)cp & MASK` reaches bits of the *word* address and cannot see the byte offset at
  all. Grep for `& 1`, `& ~1`, `& (NBPW - 1)` and `& CROUND`-shaped masks. `prim.c`'s clists and
  `sh`'s parse nodes are the two already fixed.
* **A pointer fabricated from a small integer.** `u.u_dirp = (caddr_t)u.u_arg[0]`
  ([syscall.c](syscall.c)) is fine — that cast is a silent `COPY`, so the caller's marker and byte
  offset survive and `namei()`'s `fubyte(u.u_dirp++)` is right. Everywhere *else* has to be checked
  one site at a time: `(caddr_t)ipc.ip_addr` in [sig.c](sig.c) (four sites) is a `ptrace` client's
  address and is only right if a word address is what the ABI promises, which nothing states today.
  An `int` and a pointer are the same word here and `b6cc` converts between them without a word.

**Deliverable.** A pass over `kernel/*.c` and `kernel/dev/*.c` recording, per site, which of the two
things the integer is; the fixes; and the ABI sentence for `ptrace`'s `ip_addr` written down in
[../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md).

**How to verify.** Neither of the two known bugs reproduced under `b6sim`, where the host serves the
system call and the kernel's copy never runs, and neither was found by review — both were found by a
*user program* doing something ordinary. So the check on this task is a user-level program that
exercises the site, run under `libtest`, not an inspection.

**Size.** Open-ended; do it in one sitting per file and stop when the grep is clean.
