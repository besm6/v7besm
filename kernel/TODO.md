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

**Tasks 1 through 28 are done and their writeups have been removed**; the design they settled on is
README.md, and how each turned out is in the source comments and in [../doc/](../doc/). The
numbering is **left as it was** — task numbers are cited from the sources and from `doc/`.

Task 29 is the road ahead. 30–36 are small, independent, and were deferred deliberately.

| | task | size |
|---|---|---|
| 29 | multiuser: the two-terminal test (the driver and the userland are done) | small |
| 30 | copy only the live part of the u-area | small, measurable win |
| 31 | the kernel-stack depth check | small |
| 32 | `profil()`: implement `addupc()` or make it fail | small; the decision is the task |
| 33 | `ptrace` single-step | small now, blocked after |
| 34 | the `int` ↔ pointer audit | open-ended |
| 35 | the character `send` drops — find it, and see what it costs | small to measure, unknown to fix |
| 36 | the shifting copy: the half of the byte path task 28 could not reach | medium, high risk |

---

## 29. Multiuser

The machine has **two** Consul-254 typewriters and, since 29a, [dev/sc.c](dev/sc.c) drives both:
`/dev/console` (also `/dev/tty0`) is Consul 1 and `/dev/tty1` is Consul 2. Since 29b the system
is genuinely multi-user: `/etc/ttys` is on the image, `merge()` forks an `/etc/getty` per line,
`/bin/login` checks a password through `crypt(3)` and hands the terminal over, and `test/login`
walks the whole of it — an unknown name asked for a password and refused, root logged in, the
shell it got exited and the getty **respawned** by `multiple()`, and then guest logged in with no
password to the first non-root shell this port has ever produced.
[../cmd/getty/README.md](../cmd/getty/README.md) and [../cmd/login/README.md](../cmd/login/README.md)
are the accounts, and the second of them is why `cmd/README.md` §8 no longer says every shell here
is root's.

**One piece left**, and it is a test rather than a program.

### 29a. The terminal driver — DONE

**The second terminal is the second Consul, not the serial multiplexor and not the telegraph
lines.** The multiplexor would have wanted new registers, a syllable encoder, one
character-in-flight round-robin across 24 lines and a receive register that reading does not free;
the telegraph channels would have wanted a software UART clocked per bit per line. Neither buys
anything a *second* terminal does not, and the machine already had one:
[../include/sys/besm6dev.h](../include/sys/besm6dev.h) has carried `EXT_CONS2`, `EXT_CONS2_RD`,
`PRP_CONS2_INPUT`, `PRP_CONS2_DONE` and `CONS2_READY` all along.

How it turned out is in the source — `dev/sc.c`'s header for the driver, `test/sctest.ini`'s for
the test — but three things are worth keeping here because the next driver meets them again.

* **The hardware numbering decided the shape.** `EXT_CONS1`/`EXT_CONS2` are adjacent and each ПРП
  pair is one bit apart, so a variable unit folds into the instruction's own address field
  (`../doc/Intrinsics.md` §8, the `dev/mb.c` pattern) and one driver serves both lines. `READY2`
  does not follow: Consul 2 is bit **6**, not bit 7, so the ready bits are a table.
* **ПРП is read live, so a masked bit is still seen.** The machine comes up with "printing
  finished" standing for *both* typewriters and `scinit()` dismisses only the line being opened, so
  Consul 2's stood from reset. The first console interrupt of the boot then "completed" a tty whose
  `t_oproc` was still zero — a jump to 0, `GRP_INSN_CHECK`, a kernel trap before the root was
  mounted. `scintr()` skips a line without `ISOPEN` for that reason.
* **`sctest` could not have caught it**, because it drives the hardware and does not link the
  driver; `test/boot` did, on the first run. The division is worth remembering when the next
  device test is written: a standalone test covers the registers, and only a booting one covers
  what the kernel does with them.

`dev/sr.c` — the multiplexor skeleton — is **gone**, and with it `cdevsw[3]`, so `rmd` and `rmb`
moved to majors 3 and 4.

### 29c. The multiuser test

`test/login` covers everything this task was scoped for **except the second terminal**: a getty on
each Consul, a password checked, and a logout bringing a fresh `login:` — which is `multiple()`,
the loop no other test reaches. What is left is to drive **Consul 2** as well, and prove that two
people can be logged in at once on the two typewriters.

**The connection question is answered.** `test/login.ini` already carries the configuration:

```
set tty26 raw
attach tty26 4199        ; listen for telnet connections on TCP port 4199
```

and a getty is already sitting on `/dev/tty1` waiting for a client. `telnet localhost 4199` during
a hand run reaches it and logs in for real. What is *not* answered is how to get a client there
**without a human** and synchronise it with SIMH's own `expect` halts — a host-side helper beside
the simulator, in `run-session.sh`'s mould, is the shape with a precedent. Notes that survive from
the survey and are still worth having:

* Consul 1 is line **tty25** and Consul 2 is line **tty26** (`besm6_tty.c`, `consul_print()`:
  `line_num = dev_num + TTY_MAX + 1`).
* Every test today drives Consul 1 with **bare** `expect`/`send`, because `attach tty25 console`
  binds it to the simulator's own stdin/stdout and **bypasses `tmxr_attach` entirely**. A
  consequence to know before trying the obvious: `expect TTY:25,…` does *not* work in that
  configuration — the per-line `SEND`/`EXPECT` structures are only allocated when the mux has been
  through `tmxr_attach`. On line 26, which *is* attached, `expect TTY:26,"…"` / `send TTY:26,"…"`
  do work (`_tmxr_locate_line_send_expect()` in `sim_tmxr.c`) once a client has connected.
* **An unattached line is not a second terminal.** It still raises its interrupts —
  `consul_print()` lowers `READY2` and `consul_receive()` raises "printing finished" whether or not
  the line is connected, which is what lets `test/sctest` exercise both typewriters with one console
  attached — but `vt_puts()` returns early on `!t->conn`, so nothing it prints goes anywhere and
  nothing typed can reach it. A half-configured line therefore fails **silently** rather than
  loudly. TMXR loopback is out for a different reason: it feeds the guest's own output back as
  input.
* Two traps in the connect path have been **fixed in the simulator** rather than worked around, and
  the fix is what makes a raw Consul 2 usable at all: a `set tty26 raw` issued before the client
  connected used to be discarded by `reset_line()`, and every new connection injected a `'\3'`
  into the receive buffer, which on a Consul line arrives at the guest as a typed ^C. Both are
  conditional on the line being raw now. Anything written here must be run against a simulator
  new enough to have that.
* `test/login.ini`'s fixed port is safe only because every booting test holds
  `RESOURCE_LOCK simh_boot`. Two of them at once would fail to bind it.

**Read `test/login.ini`'s header before writing the dialogue**, and task 35 below with it. Both of
this suite's known `send` hazards bite here: the `after=` every booting test carries, and the
`delay=` that `login.ini` had to add on top of it because a getty reads the name RAW, one character
per round trip through user mode. Nothing in that file may end on a rule a bare prompt satisfies,
and with two terminals in one stream that rule gets harder rather than easier.

**Size.** Small, and no longer blocked.

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

---

## 35. The character `send` drops

**What is known, and it is more than it was.** Every test that types at the guest —
`console`, `session`, `files`, `libtest`, `utils`, `swap` — loses a character out of a `send`
sometimes. That much was already written down twice, in README.md's SIMH notes and in the
`RESOURCE_LOCK simh_boot` comment in [test/CMakeLists.txt](test/CMakeLists.txt), and both attribute
it to **host load**: `session` failing one run in four under `CTEST_PARALLEL_LEVEL=8` with
`s: not found` for a sent `sh /etc/session`.

Task C2c found a case that is **not** load-dependent and reproduces every time. In `console.ini`,
`send "rmdir /tmp/d\r"` arrived as `mdir /tmp/d` on every run of the file, on an idle machine — the
**first** character of the send and no other. Two things establish the shape of it:

* prefixing a throwaway character (`send "Zrmdir …"`) makes `rmdir` arrive intact and the `Z` is the
  one eaten, so it is positional, not a corruption of that byte;
* `send after=20000 "…"` — 20,000 cycles before the first character — makes it deliver reliably,
  five runs for five.

`console.ini` carries `after=20000` on every `send` now and says so at length in its header. **The
other five `.ini` files do not**, and they are the ones the lock was measured against.

Task 29b found a **third** case, and it is the first one that separates the two candidate
mechanisms rather than merely restating the symptom. `test/login.ini` needed `delay=20000` — the
gap between the characters *after* the first — on top of the `after=`: without it `nosuch` arrived
as `nsuch`, every run, on an idle machine, and it is the **second** character that goes, not the
first. What is different about that dialogue is the guest, not the simulator: `/etc/getty` reads
the login name in **RAW mode**, one `read(2)` and one `write(2)` per character from user mode,
where every other test types at a shell in canonical mode and the kernel accumulates a whole line
in a clist before waking anybody. So the same simulator, at the same default rate, feeds a shell
without loss and a getty with it — which is what the input-overrun hypothesis below predicts and
the timing-artifact one does not. **Start step 1 from there**, and note that 20000 is a delay that
works, not a measured minimum.

**Why it went unnoticed for so long is worth its own sentence**, because it is the more useful
finding: `console.ini`'s last rule was a bare `expect "# "`. All SIMH rules are armed at once, so a
stalled stage simply fell through to it at the next prompt and the test printed PASS — it had been
passing without running its last four stages. That rule is unique now. **Check every
`expect`/`send` test for a final rule a bare prompt can satisfy** before trusting a green run here.

**What to do.**

1. **Find where the character goes.** Not established, and the two candidate mechanisms sit on
   opposite sides of the boundary. One is an input overrun in the guest: `scintr()`
   ([dev/sc.c](dev/sc.c)) takes one character per ПРП interrupt out of a single register, so a
   second arriving before the first is read has nowhere to wait — and if that is it, then it is a
   **driver** bug that a real operator typing fast would also hit, not a test artifact. The other is
   the simulator: `send` delivers at an instruction-count rate while the console's timing is
   calibrated against the host clock, which is the guess `test/CMakeLists.txt` already records.
   `sctest` is the place to tell them apart — it drives the Consul with no kernel underneath, so
   feeding it two characters closer together than one interrupt service answers the first question
   on its own. `test/login.ini`'s `delay=` above is the first evidence that points one way rather
   than the other, and `test/login` is the cheap way to make the drop happen: remove that `delay=`
   and `nosuch` comes back as `nsuch` on the next run.
2. **Then decide what the delay is worth.** If `after=` is enough, put it on the other five files
   and **re-run the RESOURCE_LOCK measurement**: five full `ctest` runs with and without
   `simh_boot`, which is exactly how the lock earned its place. It costs about fifteen seconds a
   suite, and it was bought to treat this symptom. If the delay makes the lock unnecessary, both
   the lock and its comment come out.
3. Whatever the answer, correct README.md's "`send` DROPS A CHARACTER now and then, and it is not
   the kernel" — the second half of that sentence is exactly what has not been established.

**How to verify.** A fix is only a fix if the drop can be *made to happen first*: remove the
`after=` from `console.ini` and confirm `rmdir` still arrives as `mdir`, which takes one run. Then
the five parallel-`ctest` runs for the load half.

**Size.** Small to measure, and the measurement is most of the value. The fix is unknown until
step 1 answers which side of the boundary it is on.

---

## 36. The shifting copy

**Where.** `copyinb`/`copyoutb` in [ucopy.c](ucopy.c), and whatever machine assist they end up
calling.

**What is left.** Task 28 gave `iomove()` a bulk path, and it reaches the **in-phase** case only —
both pointers standing on the same byte of their respective words, so that after a partial leading
word the middle is whole words on both sides. Out of phase, every word of the transfer straddles
two on the other side, and the copy is still one `fubyte`/`subyte` per byte: a `useracc()` range
walk and a mode-toggle bracket for six bits of payload.

**How much it is.** `nioshift` (systm.h; `libtest.ini.in` prints it on every run) said **94,805
bytes** of `libtest`'s 1,253,598, against 871 left on the in-phase arm and 1,157,922 through the
bulk path. So this is now the *whole* of what `iomove()` still moves a byte at a time, and it is
7.6% of the traffic and a much larger share of the time. `session` is 2,894 bytes, unchanged by
task 28 and unchanged by anything since.

**Where it comes from**, which is worth knowing before optimising it: the kernel-side pointer is
`(caddr_t)bp->b_addr + on` with `on = u_offset % BSIZE`, so its phase walks with the file offset,
while the user's buffer stays put. `read(fd, buf, 100)` in a loop lands here on every call after
the first. A program whose transfers are multiples of six never does.

**What to do.** Two candidates, and the first is much the cheaper:

1. **Word-at-a-time in C, through `fuword`/`suword`.** For `copyinb`, read whole user words with
   `fuword` and split them in C — one `useracc()` per six bytes instead of six. For `copyoutb`,
   assemble each user word from the kernel bytes and `suword` it, reading the old word back with
   `fuword` only for the two partial end words. Roughly 3–6× on the same arm, no assembly, and the
   masks are the ones `ucopy.c` already reasons about.
2. **A funnel shift in `usermem.S`.** `asx`/`asn` shift `[A, Y]` as one 96-bit quantity and `yta`
   reads `Y` back, so a two-instruction shift-and-carry per word is available; it is used nowhere
   in this kernel today. Faster than (1) and the only way to get the mode-toggle bracket down to
   one per word — but it is assembly, in the file whose header explains why it stays word-only, and
   `copyinb`/`copyoutb` would have to hand it the two phases explicitly rather than mask them away.

Do (1) first and re-read `nioshift`; (2) is only worth it if the counter says so afterwards.

**How to verify.** [test/umem](test/umem.c) **already covers this**, and covering it is why its
matrix is 6 × 6 and not 6: every unequal `(ku, kk)` pair is an out-of-phase transfer, at lengths
0–13 and at 200, with the whole destination window compared against an independent oracle so that
a shift that lands one byte out is caught wherever it lands. The `.ini` header lists the mutations
that made it bite. So this task's bite test is written; what a new implementation must do is fail
`umem` when it is wrong, which the existing mutation list already demonstrates it does.

**Size.** Medium for (1), and higher risk than it looks — this is the same routine that produced
one silent data-corruption bug already, and the reason `umem` exists.
