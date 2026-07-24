# Bringing the port up: from `panic: iinit` to a single-user shell

A work plan. The machine-dependent half of the kernel is **done**: the memory model, the mapped
brackets, `_start`, all three trap doors, the timer, the context switch, the signal frame and the
user memory layout all work, two processes alternate under the real scheduler on SIMH, and the
console, drum and disk drivers are written and their failure modes classified. The on-disk layout
is settled and `b6fsutil` (`cmd/fsutil/`) builds root filesystem images in it.

What remains is putting a shell in user mode. That is tasks **24–25** below, in order.
Tasks 26–29 are what is left after the prompt appears.

Where the port stands: `cd kernel && make` links an image that boots under SIMH. With no root
disk it reaches `panic: iinit`; **with `root3072.disk` and a drum attached it boots through
`iinit()`, hands process 1 the icode, enters user mode, execs `/etc/init` and holds a
conversation with whoever is at the console** — reading typed lines, honouring erase, kill and
`^D`, and answering through `/dev/tty` (tasks 20 through 23 are done). The `/etc/init` on the
image is a stand-in, `kernel/test/coninit.S`, until task 24 puts a real one there. `make run`
is where you talk to it.

**The drums must be attached to exec anything**, which is new with task 23 and easy to be
caught by: they are `swapdev` ([conf.c](conf.c)), and `exece()` stages the argument list in
swap — `getblk`/`bawrite` in, `bread` out — before it ever touches the new image. With no
drum that `bread` comes back `B_ERROR` and every exec fails with `exec init: error 5` (EIO).
Every `.ini` that boots therefore says `attach -n drum0 …`.

The harness the boot is checked against: the build produces a root image
(`kernel/test/root.manifest` → `root3072.disk`, now carrying `/etc/init`),
`kernel/test/fstest` reads its superblock and root inode through the real `md` driver, the
real buffer cache and the real `sbcheck()` (strictly below the boot path), `kernel/test/boot`
boots the whole `unix` image with that disk attached and asserts that init's banner reaches
the console, and `kernel/test/console` types at init and checks what comes back.

Read [../doc/Memory_Mapping.md](../doc/Memory_Mapping.md) before touching memory management,
[../doc/Intrinsics.md](../doc/Intrinsics.md) for how C reaches `002 «рег»` and `033 «увв»`,
[../doc/Unix_Context_Switch.md](../doc/Unix_Context_Switch.md) for the gates, and
[../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) before touching `dev/`.

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
   ...      must all end below 064000 = KEND
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

(The same black hole is why a user image starts at **word 8** (`BADDR`), not word 0: the a.out
header hole occupies words 0–7, so nothing the program touches lands on the virtual-word-0 sink.
See `getxfile()` in [sys1.c](sys1.c).)

An interrupt taken inside a bracket is harmless *for addressing*: the hardware forces БлП = 1 at the
vector, so the handler sees the kernel's normal unmapped world and its stack at `076000` resolves
physically; `выпр` restores БлП from SPSW and the bracket resumes mapped.

It is **not** harmless for `uload`, which is overwriting the page the handler's stack frame is in —
so that bracket holds БлПр. And note that **`vtm N,0` writes БлПр along with БлП and БлЗ**, all three
from the address field: a bare `vtm 2`/`vtm 3` *enables* interrupts as a side effect. A bracket that
wants them off must say `02002`/`02003`, and restore PSW afterwards (`ita 021`/`ati 021` — supervisor
takes a 5-bit register number, so `M[021]` is reachable).

### Five hardware rules everything obeys

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

   **And before user code FETCHES a word the kernel wrote through the map.** The instruction path
   does not consult the write cache — `mmu_prefetch()` reads memory directly, while a data load
   does look in БРЗ — so a word `copyout()` has just written can be read back correctly as data
   and still be fetched as garbage. Measured: right after `main()`'s `copyout()` of the icode,
   two of its nine words were in memory and **seven were still in `BRZ0`–`BRZ7`**. `exec()` gets
   the drain for free from the `estabur()` that follows its `readi()`; `main()`'s icode copy and
   `sendsig()`'s planted trampoline word drain for themselves.
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

`resume()` ([switch.s](switch.s)): if `paddr != uhome`, `uflush(uhome)`, then `uload(paddr)`, then
`uhome = paddr`. Only then restore r1–r7, r13, r15 from the label — which, being at `076000+n` in
*every* process, now names the incoming process's saved state. That constant is the whole trick.

**Anything else that reads or frees the current process's image must flush first.** This is the
sharpest edge in the whole design; it has bitten twice, and both times the site was one the list did
not have. The complete rule — all six sites, and why the test belongs inside `xswap()` rather than at
its call sites — is written up **once**, in the block comment at `xswap()` in [text.c](text.c). Add
to it there, not here.

---

## Tasks

Each task leaves the tree building (`cd kernel && make`) and the suite passing
(`ctest -L kernel`). Verification is under **SIMH** ([../doc/Simh_Simulator.md](../doc/Simh_Simulator.md))
via `kernel/test/*.ini` — `b6sim` runs a user `a.out` with no kernel underneath and cannot exercise
any of this. `test/mmutest` is the model to copy: it links the kernel's own objects against a
hand-built process, checks itself from C, and lets the `.ini` assert on the machine state afterwards.
Run every MMU test with **`set mmu cache`**.

**`besm6.o` cannot go into a standalone test** — its `0500` vector reaches into the C kernel and its
`_start` seeds no stack. That is why every routine a test has to link lives in its own file
(`brz.s`, `uarea.S`, `seg.S`, `usermem.S`, `switch.s`, `syscall.c`, `sendsig.c`) and why the gates
are duplicated in the tests' own crt0s.

**Tasks 1–23 are done and their writeups have been removed**; the design they settled on is the
section above, and how each turned out is in the source comments and in `../doc/`. The numbering is
**left as it was** — task numbers are cited from the source (`seg.S`, `dev/bio.c`, `dev/mem.c`,
`clock.c`, `trap.c`, `sig.c`, `test/crt0*.S`, `test/fstest.c`, `sys1.c`, `text.c`) and from `doc/`
— so the list below starts at 24. The one task that was still open, 18b.6 (wiring up and bring-up),
became tasks 19 and 20. Since then the root filesystem mounts (task 20 — its bug a fat-pointer
`b_un` pun, now written up on the union in [../include/sys/buf.h](../include/sys/buf.h)), `exec`
reads the BESM-6 `a.out` (task 21 — `getxfile()`/`xalloc()` and `u_exdata`), process 1 runs in
user mode (task 22 — the icode and `_start`'s second half in [besm6.S](besm6.S), and the drain in
[main.c](main.c); the icode is copied to the address it was *linked* at, which is what lets it name
its own string and argv vector at all), and the console became a controlling terminal (task 23 —
the clists, which had to be rewritten because v7 finds a character's block by masking its byte
address and a `char *` here is a fat pointer; the account is in [prim.c](prim.c), the program that
proves it is `test/coninit.S`, and `test/console` is what types at it).

### Stage 7 — the userland

**24. `rootfs/` — the tree the image is built from.**

A new top-level `rootfs/`, cross-built by the same CMake machinery `lib/` uses
([../scripts/BesmCross.cmake](../scripts/BesmCross.cmake) and the in-tree `b6*` targets), staging
into **`build/rootfs/`**, which the root-image manifest (`test/root.manifest`) reads with
`source build/rootfs/…`:

* `rootfs/init/` — a v7-shaped `/etc/init`: single-user, `/dev/console` on fds 0/1/2,
  `fork`+`exec` of `/bin/sh`, `wait` and respawn.
* `rootfs/sh/` — the shell. Start with a minimal one (word splitting, `fork`/`exec`/`wait`,
  redirection, `cd`, `exit`) to get a prompt at all, then port v7's `sh` once the syscalls have been
  shaken out by task 25.
* `rootfs/bin/` — enough to prove the prompt: `cat`, `echo`, `ls`, `pwd`.
* `rootfs/etc/` — the static files (`passwd`, `rc`, `motd`).

Every program links `crt0.o … -lc -lruntime`, in that order — the archive-scan contract in
[../lib/README.md](../lib/README.md) — and must fit the 32-page user space with the stack based at
`070000` (`b6size -w`).

**25. Boot to the prompt, and shake the syscalls out.**

* `make run` boots with the image attached and gives a shell on the SIMH console.
* Then run the `lib/test/` programs — which today run only under `b6sim` — from the image, under the
  real kernel. This is the first time the `$77` gate is driven from user mode by anything but the
  kernel's own tests, and it is where `sysent[]`'s sixty untested rows meet their handlers'
  argument structs. `procs`, `execs`, `spawn`, `signals`, `sbrkt` and `stdiot` are the ones that
  bite; `b6sim` and the kernel disagreeing is a bug in one of them, and the `.expected` files say
  which.
* `sync`, then `b6fsutil -c` on the image afterwards. A clean fsck after a session that wrote is the
  end-to-end statement that the filesystem half works.
* **Done when** the image survives boot → shell → create and write files → `sync` → fsck clean, as a
  ctest case.

### Stage 8 — after the prompt

Ordered, but none of it blocks the shell.

**26. Swapping and shared text under load.** `sched()`/`xswap()` through the drum with more
processes than core, and two processes sharing one binary's text. The code is written and `usched`
covers the scheduler in isolation, but nothing has ever swapped a real image, and `swap()` on the
drum has only ever moved a forged page. This is also where the u-area invariant above gets its first
real workout.

**27. `/dev/mem` and `/dev/kmem`.** Minors 0 and 1 are `ENXIO` in [dev/mem.c](dev/mem.c); minor 2
(`/dev/null`) works. `/dev/mem` must reach a physical page above `0100000`, which needs a
`copyseg`-style mapped bracket ([seg.S](seg.S) is the worked example) — or, simpler and with no new
assembly, a bounce through a page-aligned kernel buffer using `copyseg()` itself, whose БРЗ drains
are already in the right places. `/dev/kmem` is direct below the unmapped reach.

**28. The leftovers.** Each is small, independent, and has been deferred deliberately:

* **`addupc()` is a stub** ([besm6.S](besm6.S)), so `profil()` is inert.
* **Single-step / the address-break registers М034/М035.** `ptrace()`'s "continue with a trace trap"
  has no flag bit on this machine — no EFL/TBIT — so arming it means writing М034/М035, and
  `procxmt()` must re-arm after each match. The sites carry `TODO 17` markers: [sig.c](sig.c)
  (cases 6 and 9), [trap.c](trap.c) (the `GRP_BREAKPOINT` arm) and `GRP_BREAKPOINT` in
  [../include/sys/besm6dev.h](../include/sys/besm6dev.h).
* **`iomove()` tests alignment with `n & (NBPW - 1)`** ([rdwri.c](rdwri.c)) and `NBPW` is 6 — not a
  power of two, so the mask is meaningless and the fast `copyin`/`copyout` path is taken more or
  less at random. Marked `/* XXX even addresses */` in the v7 original. Correctness is unaffected;
  it is a performance bug, and it wants the same real-`char *` treatment `exece()`'s argument block
  got.
* **`uflush`/`uload` copy the whole 1024-word page.** Copying only up to the saved `r15` —
  `struct user` plus the live stack, typically ~300 words — was planned and never done.
* **No kernel-stack guard page.** `r15` grows up from ≈ `076214` to `0100000`, and past that a
  15-bit address wraps to 0 — into the interrupt vectors. A depth check in `trap()`/`swtch()` is the
  cheap answer.
* **Every other `int` → `char *` conversion.** `u.u_dirp = (caddr_t)u.u_arg[0]` is fine — that cast
  is a silent `COPY`, so the caller's marker and byte offset survive and `namei()`'s
  `fubyte(u.u_dirp++)` is right. What is *not* checked is everywhere else that fabricates a pointer
  from a small integer. It has to be found by grep: an `int` and a pointer are the same word here,
  and `b6cc` converts between them without a word.
* **`off_t` is in bytes.** Making file offsets word counts would delete the remaining `b$div` calls
  at every block crossing and is the BESM-6-shaped answer, but it is user-visible and changes
  `read`/`write`/`lseek` and `iomove()`'s granularity. Separate problem, wants its own plan.

**29. Multiuser.** [dev/sr.c](dev/sr.c) is a skeleton: the tty scaffolding and the `cdevsw` surface
exist, and nothing behind them talks to the 24-line multiplexer. That driver, then `getty`/`login`
and a multiuser `init`, is where the road continues past this file.

---

## Notes for the next standalone SIMH test

What the ones already in [test/](test/) cost to get right. Read this before writing the next.

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
* **A test that needs a TYPED line drives it with `expect`/`send`**, which is what `console.ini`
  does and the only way to reach the top half of the tty code. Three things make it work and are
  worth not re-deriving. Injected input arrives through `sim_poll_kbd()`, which serves it *before*
  it looks at the real keyboard — so the dialogue runs under ctest with no terminal on stdin, and
  a plain pipe into `besm6` does **not** work as a substitute. A match **halts** the simulator and
  then runs the rule's action, so each action has to end with its own `step N` to set the machine
  going again; the run is then bounded by the ctest `TIMEOUT` rather than by one step budget, and
  the trailing `echof …; exit 1` after the last `step` is what a never-fired rule falls into.
  And `-c` is **CLEARALL**, not "compare literally" — a bare `expect "…"` already matches the
  quoted bytes exactly, and `-c` on any but the last rule would throw the others away.
  ([../doc/Simh_Simulator.md](../doc/Simh_Simulator.md) has the corrected summary.)
* **Make the test cross the boundary it is about.** `console.ini`'s first three lines are five
  characters long and never leave the first clist block, so the whole of the block-chaining code
  — the half of `prim.c` that had to be rewritten — went untouched by them. The fourth line is 48
  characters against a `CBSIZE` of 30 for exactly that reason. Ask what the sizes in the test are
  relative to the sizes in the code, not just whether the answer came back right.
* **The interval timer cannot be switched off.** It free-runs at 250 Hz and the SIMH `CLK` device has
  no `DEV_DISABLE`, so no `.ini` can stop it and a second tick may land mid-run. Phrase every
  assertion to tolerate exactly one — a draft `p_cpu >= 1` check once passed *only because* a second
  tick arrived after the aging code zeroed it.
* **Gate temp cells go in `.text`, not `.bss`, once the image's bss passes `010000`** — the gate must
  use a bare 12-bit `atx save_a`, which cannot reach further. `crt0c.S` does this.
* **`mmutest` owns the БРЗ-drain bite test**, not `uswtch`: dropping `uload`'s post-copy drain passes
  `uswtch` and still fails `mmutest` (code 17). Know which test proves which hazard before trusting
  one.
* **A user program reports back through a deliberate data-protection fault, not `стоп`.** In user
  mode `стоп` re-dispatches as extracode э63 and check-halts under the reset ПоК; a data fault
  ignores ПоП/ПоК and always vectors.
* **A forged `uprog` cannot use the literal pool.** It runs mapped at virtual page 0, but the pool
  lives in the crt0's `.const` at *physical* page 0, which is not what virtual page 0 maps to — a
  `#(...)` operand reads whatever happens to be there. `ugrow`'s uprog therefore *reads* its
  sentinel out of a data page `main()` seeded, rather than spelling it as a constant. (`vtm`'s
  15-bit immediate is fine; it is part of the instruction.)
* **Write the bite test, then verify it bites.** `ugrow` was checked both ways before being trusted:
  reintroducing a stack shuffle makes it fail with `020`, and dropping the `sureg()` after the
  growth with `0212`. A geometry test that cannot fail proves nothing about the geometry.
* **When a hazard is a RACE, say so instead of pretending a green test covers it.** Two of task
  22's findings cannot be turned into a bite test at all. `main()`'s `drainbrz()` can be deleted
  and `boot` still passes, because the epilogue's own stores happen to evict the icode's lines
  first; the argument for it is a *measurement* — break on the instruction after the `copyout`
  and `ex BRZ0`–`BRZ7`, which showed seven of nine words still in the cache with memory reading
  zero. And the `b$padd` reentrancy bug (above) reproduced as a hang once, then stopped
  reproducing when unrelated code motion shifted the tick's alignment by a few instructions.
  Measure the state directly, write the measurement down at the call site, and mark the test as
  *not* covering it — `test/boot.ini.in` says so in as many words.
* **Read a bite test on ACC, never on the halt PC** — and rebuild before believing either. A "failed"
  run once turned out to have merely grown a literal by one word, moving `halt` from `0575` to
  `0576` so the `.ini` tripped its *PC* assertion while every check still passed. Run the modified
  build through a harness that prints PC and ACC separately: a wrong ACC is a broken check, a PC
  that is neither `halt` nor `fault` is a hang, and a PC one word off is usually just the tax.
* **A punned union member reads word 0 and does not fault.** `fstest` found this: `b_addr` was
  a *fat* pointer (`caddr_t`), and reading `struct buf`'s block through `b_un.b_filsys`/`b_dino`/…
  reinterpreted its bit-48 marker as a large exponent — so `fp->s_bsize` silently returned
  `s_magic` and every member past offset 0 came back as offset 0, with no fault to mark it. The
  kernel's own mount/inode path had the pun in ~13 places. Fixed twice: first with an explicit
  `(struct filsys *)` cast at each site (one `aax`, clearing the marker,
  `doc/Besm6_Data_Representation.md §7`), then by making `b_addr` an `int *` so there is no marker
  to strip and the wrong spelling cannot be written (`sys/buf.h`). The general lesson stands:
  a fat pointer stored where a word pointer is read is silent, not fatal — check the *type*,
  and prefer the one that makes the bad spelling impossible.
* **The same trap, a second time: `(int)cp & MASK` on a `char *` is meaningless here.** v7's
  clists found a character's block, and its offset within it, by masking the low bits of a byte
  address — `(int)bp & ~CROUND`, `((int)p->c_cf & CROUND) == 0`. A fat pointer keeps its byte
  offset in bits 47–45, so the mask reaches five bits of the *word* address instead: the boundary
  test fires once every 32 words rather than once per block, and the block-base recovery hands the
  free list a pointer into the middle of some other block. Walking the cursor was never the
  problem (`cp++` is the compiler's `b$pinc`) — arithmetic on the pointer *value* is. Rewritten
  with explicit indices, `CROUND` deleted; see [prim.c](prim.c). **Grep for the pattern before
  trusting any other v7 file that queues bytes.**
* **Ask what would notice if the code were wrong, and if the answer is "nothing", the test is not
  finished.** 18b.5 classified disk failures into hard and soft, but both ended in the same failed
  request with the same `b_resid` — so the entire classification was undefended, and the bite test
  duly passed while the source claimed it would fail. Exposing `mdretries` and asserting the exact
  count is what closed it. A distinction the test cannot observe is a distinction the test does not
  check, however much prose surrounds it.

## Gotchas worth not re-deriving

Facts that cost real time to establish and are not written down in `doc/`.

* **A computed `033`/`002` address must have the variable first.** `__besm6_ext(ctlr + EXT_DISKCTL3, cw)`
  folds into one instruction — the address rides the C register (`wtc`) and the constant folds into
  the instruction's address field. Written `EXT_DISKCTL3 + ctlr` it does *not* fold and costs a
  `b$uadd` call plus a stack round-trip. See [dev/md.c](dev/md.c), [dev/mb.c](dev/mb.c),
  [../doc/Intrinsics.md](../doc/Intrinsics.md) §8.
* **The intrinsics with an immediate first argument demand a compile-time constant**, so
  `__besm6_maskpsw()` cannot take a run-time level: that is why `splx()` uses `__besm6_setpsw()`
  (`ati 021`) and writes back the whole mode word its cookie carries. **The spl cookie is a PSW
  word, not a small integer** — never compare one against a level, never synthesize one, and never
  `splx(0)`, which would clear БлП/БлЗ and drop the kernel into its own user's address space.
* **The `b$` pointer helpers had to be made REENTRANT before the first exec could run**, and the
  fix is in the *external* c-compiler, not here. `b$padd`, `b$pinc`, `b$pdec`, `b$pdiff` and
  `b$stb` kept their working values in static `.bss` (the Madlen originals' `,base,` cells), so a
  clock tick landing inside one — and `clock()` touches `char` members of its own — ran a handler
  whose helper overwrote them, and the interrupted call finished with the handler's values.
  Observed as: `iget()` reading `ip->i_flag` through a pointer that had turned into a pointer to
  `proc[]`, so the root inode looked locked, `IWANT` was set, process 1 slept and the machine
  idled forever. No fault, no diagnostic. They keep their temporaries in a stack frame now
  (`libc/besm6/unix/b_p*.s`, `b_stb.s` — the shape `b$umul` always had). **A stale
  `libruntime.a` brings the bug back**, and note that `kernel/`'s link does not depend on that
  archive, so after reinstalling it you must `rm build/kernel/unix` to force a relink.
  The window is a few instructions wide, so no test in either tree reliably bites it: the
  argument is the source plus the traced failure, not a red-to-green case.
* **`_Static_assert` works and has teeth; `extern int x[1 - 2*(cond)]` does not.** `b6cc` accepts a
  negative array size without a word, so the classic trick is decorative here. `ino.h`, `dir.h` and
  `filsys.h` use the real thing.
* **`DIRSIZ` moves `u_upt`.** `struct user` holds `u_dbuf[DIRSIZ]` and a `struct direct` ahead of the
  shadow page table, whose word offset [uarea.S](uarea.S), [seg.S](seg.S) and `test/mmutest.c`
  hardcode as `UPT` (b6as has no `offsetof()`). mmutest's check 13 exists for exactly this — which
  makes the MMU tests load-bearing for a filesystem change, and that is not obvious.
* **A `char *` is a fat pointer** — marker in bit 48, byte offset in bits 47–45 — and the compiler
  walks one with `b$pinc`/`b$pdec`. Never build one out of a `(word, offset)` pair by hand; the
  worked example is `exece()`'s argument block ([sys1.c](sys1.c)), asserted by `mmutest` check 25.
  `(caddr_t)(int *)w` is the fat pointer to byte #0 of word `w`.
* **Unsigned arithmetic is calls.** `+ - * / < <= > >=` on an unsigned are `b$uadd`, `b$udiv`,
  `b$ult`, … because the additive unit reads bits 48–42 as an exponent. Every scalar typedef is
  therefore `int`; `unsigned` survives only where the value is genuinely a 48-bit hardware bit
  pattern (`u_upt[]`, the ГРП/ПРП masks, the a.out magic).
* **The SIMH disk container is not a flat block file.** Each word is eight little-endian bytes with a
  two-bit tag above the 48 (an empty data word is `0x0002000000000000`, not zero), and eight service
  words are interleaved per zone. One drive is 8,256,000 bytes against the flat image's 6,144,000.
  `b6fsutil -S` converts; `cmd/fsutil/simh.cpp` has the layout.
* **`s_isize` is the first data block, not a count of i-list blocks**, and **the free list must be
  built descending** — `alloc()` pops the superblock cache from the top, so an ascending build lays
  every file backwards across the platter while passing every self-consistency check.

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new): 1024 words
  each way, or ~300 with the optimisation in task 28. This is the cost of an unmapped kernel; in
  exchange the trap path costs *nothing* and `copyin` needs no window.
* **The u-area invariant is a footgun.** It has bitten twice, and a seventh site added later and
  forgotten will still be a very confusing bug. The whole rule lives in one block comment at
  `xswap()` in [text.c](text.c).
* **`copyin`/`copyout` toggle БлП per word** (~2× a plain copy), and the fat-`char *` byte edges are
  read-modify-write. Reworking `iomove()` for word-aligned bulk I/O is task 28.
* **`time` is never seeded from a wall clock.** This machine has no clock-calendar a program can
  read, so the epoch starts at 0 and `iinit()` takes the superblock's `s_time`.
* **`sy_nrarg` is read nowhere** and is vestigial: exactly one argument arrives in a register on this
  machine, for any `narg >= 1`.
* **There is no read-only user page.** РЗ closes a page to reads as well as writes, so a closed text
  page would take the program's own constant pool with it. `estabur()`'s `xrw` argument, and `sep`,
  are accepted and ignored.
