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

31–36 are small, independent, and were deferred deliberately.

| | task | size |
|---|---|---|
| 31 | the kernel-stack depth check | small |
| 32 | `profil()`: implement `addupc()` or make it fail | small; the decision is the task |
| 33 | `ptrace` single-step | small now, blocked after |
| 34 | the `int` ↔ pointer audit | open-ended |
| 35 | the guest's timing is not reproducible — the dropped `send` character, and the disabled `console` and `edit` tests | small to measure, unknown to fix |
| 36 | the shifting copy: the half of the byte path task 28 could not reach | medium, high risk |
| 37 | `mdvol[]` is filled only by a READ, so a pack that is only ever written is stamped with another drive's label | small |

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
page is silently lost, because `uflush()` copies at most `USIZE` words — it clamps there, task 30
having left that threshold exactly where it was — and another process then runs deep on the same
physical page. There is no fault and no diagnostic — see README.md's consequences list.

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

Task 29c added a **second place to measure it, on the other side of the mux boundary**.
`send TTY:n,"…"` used to be a silent no-op on a BESM-6 line — `vt_getc()` (`besm6_tty.c`) tested
only `TMXR_VALID`, the tag `tmxr_getc_ln()` puts on a character that came off the *socket*, while
SCP tags an injected one `SCPE_KFLAG` and the byte was dequeued and dropped. With that fixed,
`test/multi` types at a RAW getty on **line 26**, whose characters arrive through
`tmxr_getc_ln()`/`consul_receive()` rather than through `sim_poll_kbd()`. Two paths into the same
`scintr()` is exactly the discriminator step 1 wants: if the drop is the driver's single input
register, both lines lose characters at the same rate; if it is the console's host-clock timing,
only line 25 does. Neither has been measured — `multi.ini` simply carries the same
`after=`/`delay=` as `login.ini` — but the experiment is now one file away.

Task C11 left the **first symptom that is not a lost character**, and it is the one that says the
mechanism is not merely cosmetic: `test/console` fails about **one run in three** — two in six,
measured on an otherwise untouched tree, with `cmd/sh` reverted to its previous commit, so it is
nothing the shell did. It is always the same failure. Every stage of the dialogue passes, and then
the **last** expect never fires: `Step expired` at PC `37037`, with `/etc/rc`'s motd printed and its
date line still to come. **`test/console` is DISABLED because of it** (`test/CMakeLists.txt`), which
is a real hole — nothing else in the tree covers the typed keystroke path, erase and kill, or the
motd and boot date — and re-enabling it is the deliverable this task now owes.

What makes it worth more than the earlier three is that **a step budget ought to be
deterministic**. The same image, the same script and the same instruction count should reach the
same instruction every time; that they do not means something in the run is timed against the host
rather than counted, which is precisely candidate mechanism 2 below, and this is a much cheaper
experiment than the character drop: no `send` is involved in the failing stage at all, only
`^D` → `init` → `/etc/rc`. **Start here rather than at step 1** if the aim is to tell the two
mechanisms apart, and note what it implies — if instruction counts are not reproducible, then every
`step N` budget in `kernel/test/` is a wall-clock timeout wearing a disguise.

**A second test is disabled for the same reason now**, and its signature goes the other way.
`test/edit` fails **two runs in six** on an otherwise idle tree — measured — always at the same
place: the scripted stage passes in full and prints `edit done`, and then the *first* typed
`send after=20000 "ed\r"` comes back echoed as **`e d`**, a byte standing between the two
characters of a two-character send, so the `expect "ed\r\n"` on the next line never matches and
the run spends its budget in the idle loop (`Step expired` at `01024`). Every case above **loses**
a character; this one has **gained** one, on a `send` that already carries `after=`. That is a
discriminator, and a cheap one: an input overrun in `scintr()` cannot manufacture a byte, while a
host-timed console echo interleaved with the guest's own output can. `test/CMakeLists.txt` carries
the transcript and the by-hand command, and note what disabling this one costs that disabling
`console` did not — `edit` is a *script*, so the three host-side oracles at the end of
`run-edit.sh` go with it, the fsck and the two `cmp`s against `/etc/motd`.

**A third test is now waiting on this one without ever having been written.** Task C12 put a
full-screen editor on the image (`/bin/novi`, [../cmd/novi/README.md](../cmd/novi/README.md)),
and its interactive half is deliberately untested: a typed dialogue driving it would be
strictly worse than `edit`'s, because `novi`'s output is escape sequences with embedded cursor
coordinates on an alternate screen rather than readable text, and its input is multi-byte
arrow keys — so a stray or missing byte would be neither diagnosable nor, in a transcript,
even visible. **A third test born disabled asserts nothing**, so it was not written, and it
goes in with the re-enabling of `console` and `edit` rather than beside them. What that test
would cover and nothing else does: `refresh()`'s screen model, the key bindings, and RAW mode
driven from user code for the second time on this machine after `getty`. Until then `novi`'s
gap buffer and escape decoder are asserted under `b6sim` and the screen itself is not.

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
3. **Re-enable `test/console` and `test/edit`.** Both are disabled, not deleted, and each property
   is one line in `test/CMakeLists.txt` with its measurement written beside it. Nothing else asserts
   the typed keystroke path, erase and kill, `/dev/tty` from a forked child, or `/etc/rc`'s motd and
   boot date; and nothing else runs `ed` under a real kernel or fscks what it wrote. The suite is
   thinner than it looks until both come back.
4. Whatever the answer, correct README.md's "`send` DROPS A CHARACTER now and then, and it is not
   the kernel" — the second half of that sentence is exactly what has not been established.

**How to verify.** A fix is only a fix if the fault can be *made to happen first*. For the
step-budget half that is free: take the `DISABLED` property off `test/console` and run
`ctest -R console` six times, which is how the one-in-three was measured, and the same off
`test/edit` for the two-in-six. For the drop, remove the `after=` from `console.ini` and confirm
`rmdir` still arrives as `mdir`, which takes one run. Then the five parallel-`ctest` runs for the
load half.

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


---

## 37. `mdvol[]` is filled only by a read

**Where.** `mdvol[]` in [dev/md.c](dev/md.c) — filled at `md.c:552` inside
`if (bp->b_flags & B_READ)`, stamped into the sector header at `md.c:385` and `md.c:391`
inside `if ((bp->b_flags & B_READ) == 0)`.

**What is wrong.** The service-word buffer is the **controller's**, so the volume mark a write
puts on the platter has to come from somewhere the driver keeps **per drive** — which is what
`mdvol[]` is for, and the comment above it says so. But it is filled from a completed *read*
and from nothing else, and `0` is treated as "not seen yet" and left alone:

```c
if (mdvol[dev] != 0)
    sys[track * MDSYSHALF + 1] = mdvol[dev];
```

Leaving it alone does not leave the header blank. It leaves whatever the **last read of any
drive on that controller** put in the controller's buffer. So a pack that is written without
ever being read is stamped with **another drive's volume number**.

**How it was found, and why not before.** Task C7. `tar cf /dev/rmd1` is the first program in
this tree that writes a pack it never reads: `mkfs` reads the last block before writing the
first (its own end-of-volume probe), `fsck` reads everything it repairs, and `dd` is pointed at
a device the caller has usually just read. `kernel/test/mkfs`'s oracle 1 was written for exactly
this class of defect and passes only because of that probe. `kernel/test/tar` reproduces it in
one line:

```
attach -n md01 scratch3100.disk      # SIMH formats it, volume 3100 throughout
tar cfb /dev/rmd1 6 tree             # writes from block 0, never reads
b6fsutil -S scratch3100.disk out     # "scratch3100.disk: volume 3099 -> out (flat)"
```

3099 is the **root** pack's number, off drive 0.

**What it costs today.** Nothing that has been noticed: `b6fsutil`'s `from_simh()` validates the
magic *mark* and the per-half-zone self-address, and both are right — only the volume *number*
beside the mark is another pack's. A container written this way converts, fscks and boots. What
it breaks is the one thing the number is for: telling two packs apart.

**How to fix it.** Either read the label before the first write to a drive (`mdopen()` is the
obvious place, and it currently does nothing but bound the minor), or carry the number the way
SIMH does and take it from the drive rather than the controller. The first is smaller and needs
no new state; it costs one exchange per open of a drive that has not been read.

**What is asserted meanwhile.** `kernel/test/run-tar.sh` requires the **wrong** number and says
why, in `kernel/test/fsck.sh`'s `hostblind` style: the day this is fixed that check fails and
has to be tightened to 3100, which is what stops the deferral being forgotten.

**Size.** Small.
