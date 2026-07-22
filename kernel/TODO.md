# Retargeting the kernel's memory management to the BESM-6 MMU

A work plan. The memory model, the mapped brackets, boot, all three trap doors, the timer, the
context switch and the user memory layout are **done**: two processes alternate under the real
scheduler on SIMH, each seeing its own `u`, and the user stack grows on demand. What remains is
I/O — task 18 below.

Where the port stands: `cd kernel && make` links an image that boots under SIMH and reaches
`panic: iinit`. The hang after that is `panic()`'s own `for(;;) idle()`, which is now a real spin;
both drivers are now written and their failures classified (tasks 18b.3, 18b.4 and 18b.5), but no
root filesystem image exists for `iinit()` to mount, which is task 18b.6.

Read [../doc/Memory_Mapping.md](../doc/Memory_Mapping.md) before starting, and
[../doc/Intrinsics.md](../doc/Intrinsics.md) for how C reaches `002 «рег»`.

---

## The design

**The kernel runs unmapped.** БлП = 1 and БлЗ = 1 the whole time the kernel is in control, so a
kernel address *is* a physical address. The kernel image occupies the **first 32 physical pages** —
everything below `0100000`, which is exactly the reach of an unmapped data access.

**РП always holds the current process's map.** The kernel never programs a map of its own, so a trap
costs nothing: the hardware forces БлП/БлЗ on at the vector, which is already the kernel's mode.
Entry and exit touch no mapping register.

**The u-area is the last page of the kernel space** — physical `076000`–`077777`, holding
`struct user` and, above it, the per-process kernel stack growing up to `0100000`. `struct user` is
therefore at a fixed *physical* page, and is **copied in and out on a context switch**. That is the
price of an unmapped kernel, and it is the one we pay.

**Mapping is enabled only inside a few short assembly brackets** — to touch a user page
(`copyin`/`copyout`/`fubyte`/…), and to reach a physical page above `0100000` (`copyseg`/`clearseg`,
and the u-area save/restore itself).

```text
PHYSICAL, pages 0..31 — the kernel, addressed with БлП = 1 (no translation)

   0        const   (interrupt vector 0500/0501, extracodes 0550-0577, literal pool)
            text    (fetched unmapped: РП is irrelevant to it, always)
            data + bss
   ...      must all end below 064000 = KEND  (today `end` = 047310, 20168 words)
   064000   BUFFERS ------ buffers[NBUF][BSIZE], NBUF*BSIZEW = 5120 words -----
              a fixed PHYSICAL area, not bss: the drum/disk controllers transfer
              to a physical address.  `buffers = BUFBASE', absolute, in besm6.S;
              main.c declares it `extern'.  Raising NBUF lowers KEND with it.
   076000   U AREA  ------ the last page of the kernel space -----------------
              struct user     (~140 words)   `u = 076000`, an absolute symbol
              kernel stack    (~880 words, grows UP to 0100000)
   0100000  end of the unmapped reach; everything above is the page pool

РП — the current process's map, 32 pages, loaded by sureg()

   page  0..     user text (physical page != 0), data, bss, break growing up
   page 28..31   user stack, base 070000, grows UP to the 0100000 ceiling
   unallocated pages: РП = 0 (non-executable) and РЗ bit set (no data access)

   The user gets all 32 pages. The u-area is not in this map — it is physical.
```

### The click is dead

v7's "click" (a 4 KB page as a unit of counting) has no place on a word-addressed machine, and is
gone. Every size and every address in the kernel is counted in **48-bit words**: `p_addr`, `p_size`,
`x_caddr`, `x_size`, `u_tsize`/`u_dsize`/`u_ssize`, the coremap, `USIZE`. Where the hardware needs a
page, the value is a word address that is a multiple of `PGSZ` (1024), and the map builder shifts by
`PGSH` (10). `ctob`/`btoc`/`ctod` are replaced by `btow`/`wtob`/`pground`/`wtodb`; the swapmap still
counts disk blocks (512 words), only the coremap changed unit.

A page is no longer the unit of counting, so `copyseg`/`clearseg` — which still move exactly one
page — are stepped by `PGSZ` by every loop that calls them.

### The mapped brackets

Each is a short assembly routine that runs **entirely out of index registers**: while mapping is on,
the kernel's own data — including its stack — is not addressable, because virtual `076000` then
names the *user's* page 31.

| bracket | why | what it maps |
|---|---|---|
| `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword` | reach a user page | nothing — the user's map is already loaded. The loop toggles БлП per word: read the user word mapped, store it to the kernel buffer unmapped. |
| `copyseg`/`clearseg` | reach a physical page above `0100000` | steals virtual pages 1–2 as windows (one `mod 020`), restores the quartet from `u.u_upt[]` afterwards |
| `uflush()`/`uload()` | save/restore the u-area across a context switch | steals virtual page 1 for the process's u home and virtual page 2 for the live u-area (physical page 31); both live in quartet 0, so one `mod 020` steals them and one puts them back |

**Never virtual page 0.** A store to virtual address 0 is dropped and a load returns 0:
`mmu_store()`/`mmu_load()` test `addr == 0` *before* translation and before the "already physical"
tag, so the black hole is in the **virtual** address, whatever page 0 is mapped to. A window there
silently loses word 0 of whatever it copies. Pages 1 and 2 are also the cheap choice: they share
quartet 0, and their addresses (`02000`–`05777`) fit the 12-bit short address field, so the copy
loop needs no `utc`.

An interrupt taken inside a bracket is harmless *for addressing*: the hardware forces БлП = 1 at the
vector, so the handler sees the kernel's normal unmapped world and its stack at `076000` resolves
physically; `выпр` restores БлП from SPSW and the bracket resumes mapped.

It is **not** harmless for `uload`, which is overwriting the page the handler's stack frame is in —
so that bracket holds БлПр. And note that **`vtm N,0` writes БлПр along with БлП and БлЗ**, all three
from the address field: the bare `vtm 2`/`vtm 3` of `test/mmuhelp.s` *enables* interrupts as a side
effect. A bracket that wants them off must say `02002`/`02003`, and restore PSW afterwards
(`ita 021`/`ati 021` — supervisor takes a 5-bit register number, so `M[021]` is reachable).

### DONE: the doors open the interrupt level, and `intret` shuts it

The hardware forces БлПр on at every vector, and for a long time nothing cleared it: `syscall()`,
`badextr()` and `trap()` ran from the vector to the closing `выпр` with the clock stopped and every
device completion held off. v7 does the opposite — the PDP-11 trap path drops the priority to 0
before dispatching — and this kernel was already written for that: `clock()` takes its frame from
`intrframe` *specifically* so a tick can nest inside a syscall.

Worse, it was not merely missing but inconsistent, and the inconsistency was a live bug. No gate
touches `curipl` (`intr.c`), so it still read **0** after a gate had landed in C with БлПр **set**.
The first `spl` bracket in a syscall therefore opened the door by accident: `sleep()` does
`s = spl6()` → `s == 0`, and its closing `splx(s)` calls `sti()`. Any syscall that slept reached
`uj intret` unblocked — and `intret` cannot survive that. Below its first instructions it reloads
SPSW and IRET, single registers the hardware overwrites the instant an interrupt is taken, and
re-stashes into the five shared temp cells. An interrupt in that ~20-instruction window returns the
user to the wrong mode word.

How it turned out:

* **One instruction in each synchronous gate**, after the frame fill and before the `13 vjm` —
  `vtm 3`, which is `psw.s:sti()`. Not one instruction earlier: until the frame is complete,
  SPSW/ERET and the cells are the only copy of the interrupted context.
* **`trapgate` opens it only for a fault from user**, reusing `intrgate`'s `SPSW & 014` test. It
  was unconditional first, on the argument that a supervisor fault panics anyway. That was wrong on
  its own terms: the register dump is *polled* output (`scputc()` spins, `putchar()` holds spl7), so
  that path never needed interrupts; the one thing after it that does — `panic()`'s `update()` —
  opens them itself through `sleep()`; and opening them lets `clock()` and the drivers run on top of
  the corrupt state before the dump prints, which risks the only thing that path is for. `curipl`
  is not honest there either (a fault inside an `spl6` bracket leaves it at 6, so the first
  `putchar`'s `splx(s)` re-blocked anyway). The rule is now sayable: **a fault from supervisor
  changes nothing about the machine's interrupt state.** Revisit if a kernel-mode fault ever becomes
  recoverable.
* **The discriminator picks a ROUTINE, not just a level.** The supervisor arm is `u1a ktrap` — a
  branch, not a call, `ktrap()` (trap.c) being a kernel bug's dump-and-panic that never returns, so
  there is no return address to plant and no tail behind it. Each side then says only what is true
  of it: `trap()` drops v7's `USER` bit and the `+ USER` on every case label, since everything
  reaching it is from user; `ktrap()` drops the signal machinery, the `grow()` retry, the
  `issig`/`psig` tail, `intret`, and the `u.u_ar0` assignment (that name means the USER's registers,
  and publishing a kernel frame under it would destroy the frame an interrupted syscall still writes
  through). The dump itself is shared as `dumpregs()`, which also owns the rule that ГРП's bits 5–9
  mean a page only for a data violation.
* **The non-obvious part: `ktrap()` must dismiss EVERY fault bit in ГРП**, not just the one that
  fired. `panic()` does not stop the machine — it calls `update()`, which sleeps, so `swtch()` runs
  the other processes — and a fault bit left standing would be read live by the next process's
  `trap()` and shadow its real cause, the decode being a priority-ordered if/else. The old shape got
  this for free, the decode having run before the `default` arm.
* **One more at the top of `intret`**, the matching `vtm 02003`. Enforcing the level in the shared
  epilogue covers all four doors and any future C tail, where a `cli()` per C exit path would have
  to be got right once per path forever.
* **`vtm` with register field 0 is the mode write** — БлП, БлЗ and БлПр together, straight from the
  address field, nothing else in PSW touched, accumulator and ω untouched. It went in as a
  three-instruction `ita`/`aax`/`ati` first, because this file and three documents said `vtm` would
  clobber ПоП/ПоК. It does not: the hardware masks the write to those three bits. `psw.s` now says
  so, and `doc/Besm6_Instruction_Set.md` §024/§025 documents the feature, which it had omitted.
* **Inlined, not called.** One instruction beats a call outright, and `13 vjm sti` inside a block
  that uses `sti N` as a *machine instruction* a dozen times over would be unreadable.
* **`cli`/`sti` now assert БлП = БлЗ = 1** rather than preserving them, so they may only be called
  from unmapped kernel context — every caller is. The mapped brackets (`uarea.S`, `seg.S`,
  `usermem.S`) do their own `vtm` and bank PSW, because they must preserve a БлПр they do not know.
* **`curipl` is not touched by any of it.** It already reads 0 for an entry from user, and `выпр`
  restores БлПр from SPSW on the way out — the same asymmetry `extintr()`'s closing repair relies on.
* **`intrgate` is exempt**: a handler runs at raised level and the return drops it, as `rtt` does.
* Checked on the machine, on both doors: `usys` reads PSW back inside every dispatched handler and
  `utrap` inside its stub `trap()` (`getpsw()`, new in `psw.s`), each failing with `F_IPL` if БлПр
  is still set; and `utrap`/`ugrow` carry a `ktrap()` stub raising `F_KTRAP`, since both forge user
  mode and must never reach the supervisor arm. Each was made to fail on purpose first — deleting
  `sysgate`'s enable, deleting `trapgate`'s, and forcing its discriminator to the wrong arm — so the
  checks are known to bite rather than assumed to, and the three give three distinct signatures.

### Five hardware rules everything below obeys

1. **РП/РЗ cannot be read back.** The map is a shadow table in memory: `u.u_upt[8]`, eight words,
   each carrying four РП descriptors *and* (in bits 21–28 of the even words) the matching РЗ byte —
   so `sureg()` is 8 × `рег 020+i` plus 4 × `рег 030+j` with no shifting.
2. **The kernel keeps БлЗ set (protection off).** РЗ is consulted even when mapping is off, against
   `addr >> 10`, so a kernel running unmapped with the previous process's РЗ loaded would fault on
   its own bss. The hardware sets БлЗ at the vector — never clear it, not even in a bracket.
3. **Drain the БРЗ write cache before every РП write** — nine consecutive stores to physical 1–7
   with mapping off. (Stores made *unmapped* are tagged physical and survive a map change; stores
   made *mapped* — by the user, or inside a bracket — are tagged virtual and do not.) The hazard is
   invisible under default SIMH and fatal under `set mmu cache`. `drainbrz()` is the one routine in
   the kernel that **has to be assembly** — see [brz.s](brz.s) for why C cannot express it — and
   `test/mmutest` is what proves the drain is load-bearing.
4. **There is nothing to invalidate** — writing РП refills the TLB in the same instruction, so a stale
   translation is not a state the machine can be in. v7's `invd()` is deleted, not stubbed.
5. **A fault reports the faulting *page* (ГРП bits 5–9), and the saved PC points *past* the faulting
   instruction.** Anything that means to retry — stack growth — must back the PC up using
   `SPSW_NEXT_RK` and `SPSW_RIGHT_INSTR`.

### The u-area invariant

The live u-area is at `076000`; the copy in the process's image at `p_addr` is stale between
switches. A kernel global `uhome` records whose home the live u-area belongs to, and `NOUHOME` (0)
says it has no home at all — the state `exit()` and a freeing `xswap()` leave behind, without which
the next `resume()` would flush 1024 words into core `malloc()` may already have handed out.

`resume()` (`switch.s`): if `paddr != uhome`, `uflush(uhome)`, then `uload(paddr)`, then
`uhome = paddr`. Only then restore r1–r7, r13, r15 from the label — which, being at `076000+n` in
*every* process, now names the incoming process's saved state. That constant is the whole trick.

**Anything else that reads or frees the current process's image must flush first.** This is the
sharpest edge in the whole design; it has already bitten twice, and both times the site was one the
list did not have. The complete rule — all six sites, and why the test belongs inside `xswap()`
rather than at its call sites — is written up **once**, in the block comment at `xswap()` in
[text.c](text.c). Add to it there.

---

## Tasks

Each task leaves the tree building (`cd kernel && make`). Verification is under **SIMH**
(`../doc/Simh_Simulator.md`) via `kernel/test/*.ini` — `b6sim` runs a user `a.out` with no kernel
underneath and cannot exercise any of this. `test/mmutest` is the model to copy: it links the kernel's
own objects against a hand-built process, checks itself from C, and lets the `.ini` assert on the
machine state afterwards. Run every MMU test with **`set mmu cache`**.

**`besm6.o` cannot go into a standalone test** — its `0500` vector reaches into the C kernel and its
`_start` seeds no stack. That is why every routine a test has to link lives in its own file
(`brz.s`, `uarea.S`, `seg.S`, `usermem.S`, `switch.s`, `psw.s`, `syscall.c`) and why the gates are
duplicated in the tests' own crt0s.

**Tasks 1–16 are done and their writeups have been removed**; the design they settled on is the
section above, and how each turned out is in the source comments and in `../doc/`. The numbering
below is **left as it was** — task numbers are cited from the source (`seg.S`, `dev/bio.c`,
`dev/mem.c`, `clock.c`, `trap.c`, `sig.c`, `test/crt0*.S`) and from `doc/`.

### Stage 4 — process memory and I/O

**Tasks 17, 18a and 18b.1–18b.5 are done and their writeups have been removed**, on the same
terms as 1–16: how each turned out is in the source comments and in `../doc/`, and the numbering is
left as it was. What they built, one line each:

* **17** — `grow()` takes a virtual page and appends a stack page on a data fault (`sig.c`).
* **18a** — `struct buf` gained the physical word address `b_paddr`, read through `bufpaddr()`
  (`include/sys/buf.h`), so a transfer can name a page above 32767.
* **18b.1** — `dev/hd.c` retired into **`dev/mb.c`** (drums, major 1 — swap) and **`dev/md.c`**
  (disks, major 0 — root, pipedev, filesystems); the `033` map and the ГРП bits into
  `sys/besm6dev.h`, the exchange control word into `sys/besm6disk.h`. Two drivers rather than one
  split by minor, because the two are independent channels and only the control word is shared.
* **18b.2** — МГРП became dynamic: the four mass-storage completion bits are **wired**, so each is
  armed only around a live exchange (`mgrpon`/`mgrpoff`, `kernel/intr.c`).
* **18b.3**, **18b.4** — the drum and the disk actually transfer: page mode and chained 256-word
  sectors on the drum, a four-command two-step protocol and half-zone blocks on the disk.
* **18b.5** — the disk's status register and its retries: which failures are refused before the
  completion is armed, and which still interrupt and may be re-issued.

`test/` gained `ugrp`, `mbtest` and `mdtest` along the way; what they cost to get right is under
*Notes for the next standalone SIMH test* below.

**18b.6. Wiring up and bring-up.** The step that closes 18b.

* `IRQ_ON`, `conf.c`, and the raw devices through `physio()` (`mdread`/`mdwrite` are already the
  right shape — `physio(mdstrategy, &rmdbuf, dev, B_READ)`). `nswap`/`swplo` are done: 18b.3 set
  them to the drums' real 1024 blocks. Note the drum refuses a raw request that is not
  256-word-aligned at both ends, which is the finest granularity its control word can express —
  the disk's `DISK_HALFPAGE` makes its own limit 512.
* Build a root filesystem image, attach it, and boot far enough that `iinit()` mounts it.
* **Done when** `iinit()` reads the super block *and* `swap()` round-trips a page — the original
  18b criterion, now split across 18b.3 and 18b.4 and re-asserted together here.
* This is also what unblocks the BESM-6 `icode[]` deferred by task 17.

### DONE: the exec argument vector is built out of fat pointers

Not one of the numbered tasks — it came out of phase 0 of [`../lib/README.md`](../lib/README.md),
the user-level libc work plan, and is recorded here because it is a kernel change. A `crt0` cannot
be written against a block whose pointers a user program cannot dereference, so this had to be
settled before anything under `lib/` could be.

`exece()` (`sys1.c`) lays the block at the fixed base `USTKPAGE * PGSZ = 070000` — `argc`, the
`argv[]`/`envp[]` pointers, the two NULLs, then the byte-packed strings, with `r15` seeded just
above. That shape was right. What it put *in* the vector was not: three separate places treated a
`char *` as a plain word address.

* **`argv[i]`/`envp[i]` were bare word addresses.** A `char *` is a fat pointer — marker in bit 48,
  byte offset in bits 47–45 as a right-shift distance — and the compiler dereferences one with
  `asx`, whose shift comes from the operand's exponent field. A plain address asks for a shift of
  −64, so the user's very first `argv[0][0]` would have read zero.
* **The string cursor ran backwards.** It was an explicit `(word, offset)` pair that started at
  offset 0 and *incremented*, but offset 0 is byte #5 — the word's **last**. Every word went down
  LSB-first, the reverse of how six chars pack into a word. Fixing only the marker would not have
  helped: `argv[0][1]` would have come back as character 11. And no fixed offset could have worked
  at all, since only the *first* string starts on a word boundary — the rest begin wherever the
  previous NUL left off.
* **The staging loop read one byte in six.** `ap = fuword(uap->argp)` is the *caller's* own `char *`,
  already fat, and `fubyte((caddr_t)ap++)` stepped it as an `int` — the word address, not the byte
  offset. Worse, that `++` is a signed add on a word whose bits 48–42 are non-zero, which the
  additive unit reads as an exponent.

All three are now one thing: a real `char *` cursor, `up`, walked with `up++` and stored into the
vector as-is. The hand-built `(word, offset)` pair is gone, and so is the `1U << 44` shift that
spelled it. What makes that legal is what `b6cc` does with pointer casts — read out of the external
compiler (`translator/translate.c: emit_cast`, `backend/besm6/instr.c`), not guessed:

| cast | lowering |
| --- | --- |
| `int *` → `char *` | `aox` of marker + offset 5 — byte #0, the word's first |
| `char *` → `int *` | `aax` low 41 bits — strip marker and offset |
| `int` ↔ `char *` | silent `COPY` — the fat bits survive verbatim |
| `char *` ± 1 | call `b$pinc` / `b$pdec` — walk the offset field, carry into the word |

So `(caddr_t)(int *)w` *is* the fat pointer to byte #0 of word `w`, `(char *)ap` keeps a caller's
pointer intact, and `(int)up` hands the whole fat word to `suword()`. `b$pinc` was already linked
(`nami.c`'s `u.u_dirp++` pulls it). The end-of-block round-up, which used to test the offset
variable, now reads the cursor apart with `ptrword()`/`ptrbyte()` from `sys/param.h`.

`exece()` still cannot be *run* — that waits on 18b.6 and a root filesystem — so the contract is
asserted instead by **`mmutest` check 25**, which replays it against the real `usermem.o`: `suword`
a walking `char *` as an `argv[0]` slot, lay a ten-byte string down with `subyte(up++, …)` so it
crosses a word boundary, then `fuword` the pointer back, confirm it decodes to word address + offset
5, and read the string back byte by byte. Point the cursor at a plain word address instead and the
check returns 25. `mmutest.ini`'s expected halt PC moved `00604` → `00606` with the constant pool,
the same way `biotest.ini`'s did.

---

## Deferred, owned by no task

Loose ends the finished work left behind. None blocks 18.

* **/dev/mem and /dev/kmem (`dev/mem.c`).** Split out of 18a, which it shares no code with: the
  `b_paddr` work is about `struct buf`, and `mmread`/`mmwrite` never touch one. Minors 0 and 1 are
  stubbed to `ENXIO` today; minor 2 (/dev/null) works. /dev/mem must reach a physical page above
  `0100000`, which needs a `copyseg`-style mapped bracket (`kernel/seg.S` is the worked example) —
  or, simpler and with no new assembly, a bounce through a page-aligned kernel buffer using
  `copyseg()` itself, whose БРЗ drains are already in the right places. /dev/kmem is direct below
  the unmapped reach. Nothing opens either yet: `iinit()` still panics, so there are no device
  nodes, and nothing on the path to a booting kernel needs them.
* **`iomove()` tests alignment with `n & (NBPW - 1)`** (`rdwri.c`), and `NBPW` is 6 — not a power of
  two, so the mask is meaningless and the fast `copyin`/`copyout` path is taken more or less at
  random. Marked `/* XXX even addresses */` in the v7 original. Correctness is unaffected (the
  byte-at-a-time path is right); it is a performance bug, and it wants the same `(word, offset)`
  treatment as the `u_dirp` item below.
* **Single-step / the address-break registers М034/М035.** `ptrace()`'s "set signal and continue,
  one version causing a trace-trap" has no flag bit to set on this machine — there is no EFL/TBIT —
  so arming it means writing М034/М035, and `procxmt()` has to re-arm after each break match. The
  sites still carry the old `TODO 17` markers: `sig.c` (cases 6 and 9), `trap.c` (the
  `GRP_BREAKPOINT` arm) and `GRP_BREAKPOINT` in `include/sys/besm6dev.h`. A `ptrace` feature, not a layout one.
* **`sendsig()` pushes one word and no more.** The direction and the units are right now, but there
  is no signal frame proper — no saved accumulator or R, and no `sigreturn` path back through it.
  Nothing exercises signal delivery yet; build it when something does.
* **`uflush`/`uload` copy the whole 1024-word page.** Copying only up to the saved r15 —
  `struct user` plus the live stack, typically ~300 words — was planned and never done. It is a
  performance change, not a correctness one.
* **`u.u_dirp = (caddr_t)u.u_arg[0]` is an `int`→`char *` conversion.** Pre-existing, affects every
  path-taking syscall, and belongs with `iomove()`'s byte handling rather than with syscall
  marshalling. Re-read it when doing so: the exec work above establishes that this particular
  conversion is a silent `COPY`, so the caller's marker and byte offset *do* survive it and
  `namei()`'s `fubyte(u.u_dirp++)` — a genuine `char *` step — is right. What is worth checking is
  every *other* place an `int` becomes a `char *`, and there the pattern to copy is the one exec now
  uses: a real `char *` walked by the compiler, never a hand-built `(word, offset)` pair.
* **`addupc()` is a stub**, so `profil()` is inert. Was nominally task 17; left there.
* **`time` is never seeded from a wall clock.** This machine has no clock-calendar a program can
  read, so the epoch starts at 0.
* **`sy_nrarg` is read nowhere** and is documented as vestigial: exactly one argument arrives in a
  register on this machine, for any `narg >= 1`.

## Notes for the next standalone SIMH test

Task 18b wanted three — one per step 18b.2, 18b.3 and 18b.4 — and all three are written: `ugrp`,
`mbtest` and `mdtest`, the last since extended for 18b.5. What they cost to get right, for whoever
writes the fourth:

* **A round trip proves nothing about addressing.** Write a pattern, read it back, compare — and
  a driver that put the data in the wrong place passes, having been consistently wrong twice.
  `mbtest`'s first version passed with page mode forced on and with `ctlr` nailed to 0. What
  works is leaving *two different* patterns on the device from two different requests and then
  reading the region back whole, so the check is about where the boundary between them fell.
* **A C pointer cannot name anything above word 32767** (`ptrword()`, 15 bits). A test buffer at
  physical page `040` wrapped silently to address 0 and overwrote low memory. This binds the
  test, not the kernel — `b_paddr` reaches all 512 Kwords — but any test that inspects what a
  device deposited has to keep its window in the low 32 Kwords.
* **`step N`, not `go`.** A broken switch or a lost gate *hangs* rather than failing, and `go` takes
  an address, not a step count. Every `.ini` uses `step 50000000` to turn a hang into a failure.
* **The interval timer cannot be switched off.** It free-runs at 250 Hz and the SIMH `CLK` device has
  no `DEV_DISABLE`, so no `.ini` can stop it and a second tick may land mid-run. Phrase every
  assertion to tolerate exactly one — a draft `p_cpu >= 1` check once passed *only because* a second
  tick arrived after the aging code zeroed it.
* **Gate temp cells go in `.text`, not `.bss`, once the image's bss passes `010000`** — the gate must
  use a bare 12-bit `atx save_a`, which cannot reach further. `crt0c.S` does this; `crt0u.S` and
  `crt0s.S` did not have to.
* **`mmutest` owns the БРЗ-drain bite test**, not `uswtch`: dropping `uload`'s post-copy drain passes
  `uswtch` and still fails `mmutest` (code 17). Know which test proves which hazard before trusting
  one.
* **A user program reports back through a deliberate data-protection fault, not `стоп`.** In user
  mode `стоп` re-dispatches as extracode э63 and check-halts under the reset ПоК; a data fault
  ignores ПоП/ПоК and always vectors.
* **A forged `uprog` cannot use the literal pool.** It runs mapped at virtual page 0, but the pool
  lives in the crt0's `.const` at *physical* page 0, which is not what virtual page 0 maps to — a
  `#(...)` operand reads whatever happens to be there. `ugrow`'s uprog therefore *reads* its
  sentinel out of a data page main() seeded, rather than spelling it as a constant. (`vtm`'s 15-bit
  immediate is fine; it is part of the instruction.)
* **Write the bite test, then verify it bites.** `ugrow` was checked both ways before being trusted:
  reintroducing a stack shuffle makes it fail with `020`, and dropping the `sureg()` after the
  growth with `0212`. A geometry test that cannot fail proves nothing about the geometry.
* **Read a bite test on ACC, never on the halt PC** — and rebuild before believing either. 18b.5's
  first bite test "failed" run 1 and looked like a clean confirmation; it had merely grown a literal
  by one word and moved `halt` from `0575` to `0576`, so the `.ini` tripped its *PC* assertion while
  every check still passed. Run the modified build through a harness that prints PC and ACC
  separately: a wrong ACC is a broken check, a PC that is neither `halt` nor `fault` is a hang, and
  a PC one word off is usually just the tax.
* **Ask what would notice if the code were wrong, and if the answer is "nothing", the test is not
  finished.** 18b.5 classified disk failures into hard and soft, but both ended in the same failed
  request with the same `b_resid` — so the entire classification was undefended, and the bite test
  duly passed runs 1–3 while the source claimed it would fail them. Exposing `mdretries` and
  asserting the exact count is what closed it. A distinction the test cannot observe is a
  distinction the test does not check, however much prose surrounds it.

---

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new): 1024 words
  each way, or ~300 with the "copy only up to the saved r15" optimisation. This is the cost of an
  unmapped kernel; in exchange the trap path costs *nothing* and `copyin` needs no window.
* **The u-area invariant is a footgun, and it has already bitten twice.** Scoping task 16 found a
  fifth site the original four-bullet list had missed (`xswap()` on `newproc()`'s child), which is
  why the test lives inside `xswap()` instead of trusting callers. Doing the work then found a
  **sixth** — `exit()`, which frees the current process's image and leaves the live u-area with no
  home at all — and that one *corrupts memory* rather than merely wasting a copy, which is what
  forced the `NOUHOME` state. A seventh, added later and forgotten, will still be a very confusing
  bug. The whole rule lives in one block comment at `xswap()` in `text.c`; add to it there.
* **`copyin`/`copyout` toggle БлП per word** (~2× a plain copy), and the fat-`char *` byte edges are
  read-modify-write. Reworking `iomove()` for word-aligned bulk I/O is worth doing, but it is an
  orthogonal problem and not part of this work.
* **No kernel-stack guard page.** r15 grows up from ≈ `076214` to `0100000`, and past that a 15-bit
  address wraps to 0 — into the interrupt vectors. Add a depth check in `trap()`/`swtch()` during
  bring-up.

## DONE: two `sysent[]` arities were still counting PDP-11 words

Found while writing `lib/libc/sys/`, which is the first caller of the gate that is not a test.
`sy_narg` is the arity of the *C prototype*, and it is the only thing that tells `syscall()`
where the arguments are: it reads `n-1` of them from below the user stack pointer, takes the
`n`th from the accumulator, and pops the `n-1` on the caller's behalf. A count that disagrees
with the caller reads every argument from the wrong slot **and** drifts the user stack by a word
per call.

Two entries still counted a two-word `long` that is one 48-bit word here — `seek` said 4 and
`stime` said 2, while their own argument structs in `sys2.c`/`sys4.c` have 3 fields and 1. Both
now match. `cmd/sim/syscall.cpp`'s `syscall_nargs()` is the same table for `b6sim` and had to
move with it (its `dup` was the mirror-image error: 1 where the kernel has always read a
two-field struct, v7 hanging `dup2` off the same entry by bit `0100` of the first argument).

`test/usys` is what keeps the two honest — it links the real `syscall()` and checks both the
marshalling and the stack balance — but it exercises `sysent[3]` and `sysent[20]` only. Nothing
checks the other sixty rows against their handlers' argument structs; the libc stubs now do, one
call at a time, from `lib/test/`.

## The scalar types now describe this machine, not the PDP-11

Done as its own pass. `include/sys/types.h` used to carry v7's typedefs verbatim — `long daddr_t`,
`unsigned short ino_t`, `short dev_t`. On this machine `short`, `int`, `long` and `long long` are
**the same type** (one word, 41-bit signed), so those spellings never chose a width; the only thing
they still chose was signedness, and *that* is expensive: signed add/subtract/compare are single
inline instructions, while unsigned `+ - * / < <= > >=` are **calls** (`b$uadd`, `b$udiv`, `b$ult`,
…), because the additive unit reads bits 48-42 as an exponent and a 48-bit value carries data there.

How it turned out:

* **Every scalar typedef is now `int`**, and `daddr_t`/`dev_t` are documented as necessarily signed
  (`bmap()` returns `-1`; `NODEV` is `-1`). The one behavioural change was `ino_t`, which was
  `unsigned short`.
* **`unsigned` survives only where the value is genuinely a 48-bit hardware bit pattern** — `u_upt[8]`
  (МMU page-register words), the ГРП/ПРП masks in `intr.c`, `utab.c`'s descriptors, `trap.c`'s live
  ГРП, `prf.c`'s `printn`. Everything that is a *count* or an *address* is `int`; physical memory is
  512 Kwords = 19 bits, which fits 41-bit signed twenty-two bits over.
* **`btow`/`wtob`/`pground` cast to `int` internally, and that cast is load-bearing** — their
  commonest argument is a `sizeof`, which is unsigned and would otherwise drag `b$uadd`/`b$udiv`
  back in at every call site. Undefined-reference count for the `b$u*` family: **33 → 27**, the
  remainder being the hardware code above.
* **`caddr_t` was doing four jobs and now does one.** It stays `char *` (a fat pointer) only for
  genuine byte-granular user buffers. New alongside it: `chan_t` (sleep/wakeup channel), `carg_t`
  (callout argument) and `paddr_t` (physical word address). `chan_t` and `carg_t` are pointers to
  **undefined** structs — thin, so the cast is free where `caddr_t` cost a three-instruction
  fat-pointer conversion at ~56 `sleep`/`wakeup` sites, and dereferencing one is a compile error.

Three bugs fell out of it, all fixed here:

* **`sleep((caddr_t)ip + 1)` in `pipe.c` and `fio.c` was *byte* arithmetic on a fat pointer.** It
  walks the byte-offset field (5 → 4 → 3) and leaves the word address alone, so a pipe's read and
  write channels differed *only* in bits 47-45 — invisible to anything that masks a wchan, and
  destroyed outright by a thin `chan_t`. Now `CHANOF(ip, n)` (`param.h`), which offsets by whole
  words. Undefined `struct chan` is what turned this from a silent collapse into a build error.
* **`major()` shifted through `unsigned`,** so `major(NODEV)` was `(2⁴⁸-1)>>8` rather than `-1`.
  That accidentally armed every `major(dev) >= n` bounds test against a negative device. It is now
  signed, and the three tests that matter (`bio.c`, `sys3.c`, `fio.c`) reject a negative major
  explicitly — `sys3.c`'s especially, since `i_rdev` comes off the disk and a hostile `mknod` can
  set it.
* **`getxfile()`'s two a.out overflow guards had gone dead.** They caught a too-large header by
  letting a 32-bit sum overflow a 16-bit field and comparing; with every field one 41-bit word the
  comparison can never fire. The segment sizes are hostile input (read from disk), so they are now
  range-checked directly.

Also removed as dead: `t_linep` (written and read nowhere), `ttwrite`/`l_write`'s `caddr_t` return
(always `NULL`, no caller). `t_addr` became `int` — it held a *line number*, and `(caddr_t)d` on a
small integer is the same int→fat-pointer defect as `u_dirp` below.

Cost: text 15616 → 15622 words, i.e. the type work paid for itself and the six new bounds checks
came out roughly free. `kernel/test/biotest.ini`'s expected halt PC moved 00630 → 00627, since the
const laid down ahead of `_start` shrank; that constant is `_start + 2` and every `.ini` here has
one like it.

**Left open on purpose.** Roles C2/C3/C4 of the old `caddr_t` — `fuword`/`suword`'s argument,
`copyin`/`copyout`'s two ends, and `d_ioctl`'s third argument — are still `caddr_t` even though
`usermem.S` masks the fat part straight off (`aax #077777`) and never uses it. Splitting them wants
a `uwaddr_t` (user virtual word address) and must be done in one commit for `d_ioctl`, or the
mismatch will not be diagnosed: an `int` and a pointer are the same word, and `b6cc` converts
between them with a **silent `COPY`**. That last fact is also why the type split does *not* catch
the `u_dirp` bug below — it has to be found by grep, not by the build.

## Still out of scope

The drum and disk drivers (`033` channel programming, `doc/Besm6_Peripherals.md`), the BESM-6
`icode[]`, and the byte-granularity of `iomove()`.

### The disk-block constants are the *other* half-converted unit

Found while doing stage 0, and left alone: this is the filesystem axis, not the MMU retarget, and
settling it means settling the on-disk block layout — a different problem.

`BSIZE` is **3072 bytes = 512 words**, but the constants derived from it were never moved:
`BSHIFT 9` / `BMASK 0777` still describe a 512-*byte* block. So every byte-offset → block
conversion in the kernel is wrong: `rdwri.c:48-49,111-112`, `nami.c:138-156`, `sys1.c:111-114,181-184`
(the exec arg staging), and `dev/bio.c:491`.

**The indirect-block half of this is now fixed** (done alongside making `param.h`
assembler-includable). `NINDIR` is `BSIZE/sizeof(daddr_t)` = **512**, but `NMASK 0177` /
`NSHIFT 7` still said 128, so `bmap()` (`subr.c:67-120`) could only ever index the first
quarter of each indirect block while `tloop()` (`iget.c:245`) freed all 512 — the two
disagreed about the same on-disk structure. `NMASK` is now `0777` and `NSHIFT` `9`.

That half was separable precisely because **`bmap()` works in block numbers, never byte
offsets**: 512 entries per indirect block *is* a power of two, so a mask and a shift express
it exactly. It is `BSHIFT`/`BMASK` — the byte-offset conversions — that are stuck on 3072
not being a power of two. `NINDIR` is also now spelled as a literal `512` rather than
`BSIZE/sizeof(daddr_t)`, because `param.h` may no longer contain `sizeof`.

A 3072-byte block is **not a power of two**, so the shifts and masks cannot survive in that form:
either the byte offsets divide and modulo by `BSIZE`, or the filesystem's offsets themselves become
word counts (`BSHIFT`/`BMASK` then describe 512 words, which is what they accidentally already say).
The second is the BESM-6-shaped answer and the one to think about first — but it reaches into
`filsys.h`, `ino.h`, `struct direct` and the on-disk inode, so it wants a plan of its own.

Note `wtodb(x) = (x) >> 9` (words → blocks), added in stage 0, is *correct* either way: a block is
exactly 512 words.

### DONE: the on-disk layout, and how it turned out

`struct dinode` and `struct direct` are redesigned; `BSHIFT`/`BMASK` are **deleted**, not fixed.

**The inode is 16 words, `INOPB` 32 to a block.** Eight words of metadata then
`daddr_t di_addr[8]` — six direct, one indirect, one double. The old struct was v7 verbatim:
`char di_addr[40]`, 13 addresses packed 3 bytes each for a 24-bit PDP-11 `daddr_t`, against an
`INOPB` of 8 that had been true when a dinode was 64 bytes. Here it came to ~15 words, so the
i-list wasted **77% of every block** — and `iexpand`/`iupdat` were *already broken*, not merely
wasteful: their open-coded `l3tol`/`ltol3` byte loops assume a 4-byte in-core `daddr_t` and it is
6, so they wrote 52 bytes into a 78-byte array at the wrong stride. Both are word copies now,
verified by disassembly to contain no `asx`/`aax` byte-extract sequences at all.

**What made 16 words fit was dropping the third level of indirection**, and it is unreachable
here rather than merely unlikely: one ЕС-5052 is 2000 blocks, and at `NINDIR` 512 the single
indirect already spans 518 blocks while the double spans 262 662 — the volume, 130 times over.
So `NLEVEL` is 2. `bmap()` was already parameterised by the literal `3` in four places and just
took the constant; `itrunc()` lost one switch arm.

**The directory entry is 4 words, `DIRPB` 128 to a block**, `DIRSIZ` 24 → **18**. Five words
divided 512 no better than anything else does. `namei()` now works in **entry numbers** — one
divide by `DIRENTSZ`, then a shift and a mask — which recovers the arithmetic v7 had. It used to
mask the *byte* offset with `BMASK`, i.e. re-read the block every 512 bytes of a 3072-byte block
and index from the wrong base in between.

**`BSHIFT`/`BMASK` are gone.** They cannot be repaired — 3072 is not a power of two — so the
byte-offset sites (`rdwri.c`, `nami.c`, `sys1.c`) divide and take a remainder by `BSIZE`
explicitly. That is one `b$div` per *block crossing*, which is noise beside the `bread()` it
guards. `dev/bio.c` turned out never to have used either. The word-domain pair `BWSHIFT`/`BWMASK`
is what remains, and it is what `9`/`0777` accidentally already spelled.

**`off_t` stays in bytes.** The word-offset idea above is still open and still attractive — it
would delete the remaining divides — but it changes `read`/`write`/`lseek` semantics and
`iomove()`'s granularity, and it is user-visible. Separate problem.

Two things worth knowing next time:

* **`_Static_assert` works and has teeth; the `extern int x[1 - 2*(cond)]` idiom does not.**
  `b6cc` accepts a negative array size without a word, so the classic trick is decorative here.
  `ino.h` and `dir.h` now assert both the struct size and that `INOPB`/`DIRPB` of them tile a
  block; checked by deliberately breaking `INOPB` and watching the build fail.
* **`DIRSIZ` moves `u_upt`.** `struct user` holds `u_dbuf[DIRSIZ]` and a `struct direct` ahead of
  the shadow page table, whose word offset `uarea.S` and `seg.S` hardcode as `UPT` (b6as has no
  `offsetof()`). Both shrank by a word, so `UPT` went **35 → 33** in `kernel/uarea.S`,
  `kernel/seg.S` and `kernel/test/mmutest.c`. mmutest's check 13 exists for exactly this and is
  what caught it — the MMU tests are load-bearing for a filesystem change, which is not obvious.

### DONE: the mkfs — `cmd/fsutil`, installed as `b6fsutil`

This was the blocker for task 18b.6, and it is gone: `b6fsutil` builds a root filesystem in
this layout from a manifest, checks it (five-pass fsck), lists it, extracts it, and converts
it into the container SIMH attaches. See [cmd/fsutil/README.md](../cmd/fsutil/README.md).

```sh
b6fsutil -n -M manifest.txt root.img && b6fsutil -c root.img
b6fsutil -S root.img md2053.disk        # then: attach md00 md2053.disk
```

Three things learned building it that this file should record:

**The SIMH disk container is not a flat block file, and nothing here had said so.** SIMH
stores each word as **eight little-endian bytes with a two-bit tag** above the 48 — an empty
data word on disk is `0x0002000000000000`, not zero — and **interleaves eight service words
per zone**, a filesystem block being a half-zone. One drive is 8,256,000 bytes, not
6,144,000. `b6fsutil -S` converts; `cmd/fsutil/simh.cpp` has the layout, transcribed from
`besm6_disk.c:380-400` and verified byte-for-byte against what `attach -n` writes.

**`s_isize` is the first data block, not a count of i-list blocks.** `ialloc()` bounds its
scan with it and `badblock()` rejects `bn < s_isize`, so one too high is a runaway read in
the kernel rather than a wrong answer. A mkfs ported from a BSD source gets this off by one.

**The free list must be built descending.** `alloc()` pops the superblock cache from the top,
so the last block freed is the first handed out: freeing from the end of the volume down to
`s_isize` is what makes the kernel allocate ascending. Built the other way it still passes
every self-consistency check and lays every file backwards across the platter. Note also that
the sentinel `free()` plants costs a cache slot, so the first chain block holds only
`NICFREE-1` real blocks — the first spill on a 2000-block volume lands at block 1680, not 1679.

**`sbcheck()` still has not run for real** (see below), and booting with a root disk attached
still hangs before `iinit()`. That is a driver/boot-path problem, not a format one, and it is
now the only thing between here and a mounted root. To close the gap without it,
`cmd/fsutil/test/kernel_model_test.cpp` is a **second, independent reader** — `sbcheck()`,
`alloc()`, `bmap()` and `namei()`'s directory loop transcribed again, in the kernel's shape,
sharing nothing with the tool — and it mounts and walks a real image, including a 2 Mb file
through the double indirect. Two implementations agreeing is the strongest statement available
until the hang is fixed.

Next step for 18b.6, and it does **not** need the hang fixed: extend `kernel/test/mdtest` to
attach a converted image, `bread()` block 1 through the real driver and call the real
`sbcheck()` on the buffer. That would take the "compiled but never executed" note off this
file.

### DONE: the superblock

`struct filsys` is **exactly one block — 512 words**, and `_Static_assert` holds it there.

v7's layout came to **165 words**, wasting 68% of the block, because `NICINOD 100` and
`NICFREE 50` were sized for a 512-*byte* block and were never retuned. Filling the block is
nearly free: the superblock lives in a `geteblk()` buffer held for the life of the mount, and a
buffer is `BSIZEW` words whether the struct uses them or not. `NICFREE` is now **320** and
`NICINOD` **160** — split 2:1 toward free blocks because that is the hot path (every write
allocates blocks; only `creat()` allocates inodes). The free-list chain on a 2000-block drive
goes from 40 blocks deep to 7.

The lock and flag fields are **`int`, not v7's `char`**. A char *array* packs six to a word, but
whether adjacent scalar `char` members share one is documented nowhere in `doc/` — and the size
of this struct is now load-bearing. `int` removes the question rather than depending on the
answer.

Sizing it to the block fixed a real bug rather than merely tidying: `iinit()` copied
`btow(sizeof(struct filsys))` = 165 words into a fresh buffer, while `update()` wrote `BSIZEW` =
512 words of that buffer back to block 1. **347 words of uninitialised buffer went to the disk on
every sync.** Now the two agree by construction, and the assertion is what keeps them agreeing.
Two smaller ones went with it: `free()` built its chain block with `getblk()` and no `clrbuf()`,
so everything past `df_free[]` was stale kernel memory written to disk; and `struct fblk` had
never been asserted to fit a block, though `alloc()`/`free()` `wcopy()` between it and `s_free[]`
sizing the copy from the `filsys` side.

**`sbcheck()` (`alloc.c`) is new, and v7 has no equivalent.** It checks `FS_MAGIC`, then the
geometry words `s_bsize`/`s_inopb`/`s_naddr` against the kernel's own `BSIZEW`/`INOPB`/`NADDR`,
then `SUPERB < s_isize < s_fsize`, then the two counts. `iinit()` calls it before installing the
superblock and before seeding the clock from `s_time`; `smount()` calls it and fails `EINVAL`.
The geometry words are not ceremony — `INOPB` went 8 → 32 and `NADDR` 13 → 8 one commit ago, and
an image from a `mkfs` one generation out of step would otherwise mount cleanly and read every
inode from the wrong offset.

Two things did **not** turn out as planned, and both matter for task 18b.6:

* **`sbcheck()` is still unexercised at runtime.** The plan expected the boot panic to change
  from `iinit` to `no root fs`. It does not: with no disk attached, `bread()` fails first
  (`err on dev 0/0`) and `iinit()` panics before `sbcheck()` is ever reached. The boot is
  therefore byte-for-byte unchanged, which is good for regression but means the new code path
  has only been compiled, not run.
* **Booting with a root disk attached HANGS**, and this is *pre-existing* — verified by running
  the same image against the previous commit, which hangs identically. Attach a formatted disk
  as `md00` (`attach -n md00 <name>2053.disk`; the filename must carry a volume number in
  2048..4095) and the kernel prints `mem = ...` and then stops, with no panic and no further
  output. So `bread()` on an *attached* root disk never completes. Whoever picks up 18b.6 will
  hit this before they hit anything about the filesystem layout — it is a driver or boot-path
  problem, not a format one.
