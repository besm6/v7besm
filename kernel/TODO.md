# Retargeting the kernel's memory management to the BESM-6 MMU

A work plan. The memory model, the mapped brackets, boot, all three trap doors, the timer, the
context switch and the user memory layout are **done**: two processes alternate under the real
scheduler on SIMH, each seeing its own `u`, and the user stack grows on demand. What remains is
I/O — task 18 below.

Where the port stands: `cd kernel && make` links an image that boots under SIMH and reaches
`panic: iinit`. The hang after that is `panic()`'s own `for(;;) idle()`, which is now a real spin;
there is no disk driver yet, which is task 18b.

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
   ...      must all end below 076000  (~31 Kwords; today `end` = 060131, 24665 words)
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
physically; `выпр` restores БлП from СПСВ and the bracket resumes mapped.

It is **not** harmless for `uload`, which is overwriting the page the handler's stack frame is in —
so that bracket holds БлПр. And note that **`vtm N,0` writes БлПр along with БлП and БлЗ**, all three
from the address field: the bare `vtm 2`/`vtm 3` of `test/mmuhelp.s` *enables* interrupts as a side
effect. A bracket that wants them off must say `02002`/`02003`, and restore ПСВ afterwards
(`ita 021`/`ati 021` — supervisor takes a 5-bit register number, so `M[021]` is reachable).

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
(`brz.s`, `uarea.s`, `seg.s`, `usermem.s`, `switch.s`, `psw.s`, `syscall.c`) and why the gates are
duplicated in the tests' own crt0s.

**Tasks 1–16 are done and their writeups have been removed**; the design they settled on is the
section above, and how each turned out is in the source comments and in `../doc/`. The numbering
below is **left as it was** — task numbers are cited from the source (`seg.s`, `dev/bio.c`,
`dev/mem.c`, `clock.c`, `trap.c`, `sig.c`, `test/crt0*.S`) and from `doc/`.

### Stage 4 — process memory and I/O

**17. Stack growth and the user layout — DONE.** `grow()` (`sig.c`) now takes the **virtual page
number** the machine reports and has **no `copyseg` shuffle**: with the stack growing up from
`USTKPAGE`, `sureg()` appends a new page at the next higher virtual address *and* at the end of the
image at once, so the pages already in the stack keep the addresses they had — there is nothing to
move. `trap.c` passes `page` straight through. `exec()` seeds the arg block at the fixed base
`070000` growing up (layout below) with `r15` above it; `sendsig()` pushes upward by one word.
`sbreak()` keeps its shuffle — data still grows in the *middle* of the image — and stops at page 28
via `estabur()`'s `nt + nd > USTKPAGE * PGSZ`. The fork trick was already gone (`u_r.r_val2`).

Three things this turned up that the plan did not have:

* **`estabur()` assigns the sizes, so the trailing increments double-counted.** The v7/x86 original
  kept them in a separate `u_utab[]`, which made `u.u_ssize += si` in `grow()` and `u.u_dsize += d`
  in `sbreak()` the *real* assignments; this port's `estabur()` writes `u_tsize`/`u_dsize`/`u_ssize`
  directly, so both lines counted the change twice. Both are gone. Anything else that calls
  `estabur()` with an adjusted size must not then adjust it again.
* **The exec arg block had two unit bugs the layout change exposed.** `suword()` takes a **word**
  address, so the pointer vector strides by 1, not `NBPW` (the x86 stride skipped six words per
  pointer); and `subyte()` takes a **fat pointer**, byte offset at acc bits 47–45 over a word
  address, so the string cursor is now an explicit `(word, offset)` pair — `ucp++` on a plain
  integer stepped whole words and laid down one character per word. Note the 1-based bit numbering:
  the offset's LSB is acc bit 45, i.e. `1U << 44`. This closes the deferred fat-pointer item **for
  exec only**; `u.u_dirp` is still open.
* **`getxfile()` was sized from the strings alone.** It now gets `nc + (na + 4) * NBPW`, so the
  initial stack covers the pointer vector too.

The arg-block contract, which a BESM-6 `crt0` will have to know:

```text
   070000   argc                    <- USTKPAGE * PGSZ, a FIXED address
            argv[0] .. argv[argc-1]    (word addresses of the strings)
            0
            envp[0] .. envp[ne-1]
            0
            the strings, byte-packed six to a word
      r15 = the first free word above the block
```

`argc` is at a fixed address so no register hand-off is needed, and `r15` starts above the block so
the program's own stack growth cannot walk back over its arguments.

- **Done when** — verified by **`test/ugrow`** (`crt0g.S` + `ugrow.c` + `ugrow.ini`), a new
  standalone SIMH test: a forged user with a one-page stack at `070000` stores one word past the
  top, takes the data fault on page 29, the handler grows the stack, and the store re-executes into
  the page that now exists — while the pre-existing stack page keeps its physical address *and* its
  contents. Both bite tests fire: reintroducing the x86 shuffle gives `020` (the old page lost its
  sentinel), dropping the `sureg()` gives `0212`. The fork/exec half of the original criterion is
  not reachable — `icode[]` is still x86 and the BESM-6 `icode[]` is out of scope until there is a
  disk (task 18b).

**18. The disk.** Two halves: the addressing plumbing that lets a request name a physical word
anywhere in the machine (18a), and the driver that actually moves it (18b). 18a is a prerequisite of
18b — without `b_paddr` a transfer cannot name a page above 32767, which is most of memory — so do
them in that order.

**18a. I/O addresses — DONE.** `struct buf` gained `unsigned b_paddr`, a physical **word** address
valid when `B_PHYS`, filled by `swap()` and `physio()`. Read it through **`bufpaddr(bp)`**
(`include/sys/buf.h`) — the one place `B_PHYS` is read, and the call 18b's driver should make: a
kernel buffer is unmapped so its address *is* physical, and only a `B_PHYS` request needs the extra
field. Buffer I/O needs no bracket, as planned.

How it turned out, where that differed from the sketch:
- **`b_bcount` is gone; the field is `b_wcount` and counts WORDS.** It had six writes and *no
  readers*, so the unit was changed while the field was still write-only rather than after 18b put
  readers in an interrupt path. `b_resid` was already words; the two now agree. The four
  buffer-cache writes are `BSIZEW` (512), not `BSIZE` (3072).
- **`physio()` verifies contiguity instead of looping over runs.** A transfer is one physical
  address plus a length, so scattered pages cannot be expressed. `physrange(addr, count)`
  (`kernel/utab.c`, beside `useracc()` because `uptget()` is static there) returns the physical base
  only if the whole range is mapped *and* one contiguous run, else 0. It cannot fire today —
  `malloc()` is first-fit and returns one run, `expand()` keeps it one, `sureg()` maps sequentially
  — so it asserts the allocator's invariant rather than coding around a case it forbids.
- **The old checks are gone, not renamed.** `physio()` now takes the word address out of the fat
  pointer with `ptrword()` (new in `sys/param.h`, with `ptrbyte()`), rejects a start that is not on
  a word boundary and a count that is not a whole number of words, keeps a text guard — `useracc()`
  deliberately makes no read/write distinction — and leaves the `NPAGE * PGSZ` ceiling to
  `physrange()`. The old `1024` literals were dead code once `base` is masked to 15 bits, and the
  old `nb < u_tsize` compared a page number against a word count, refusing *every* data transfer.
- **`b_blkno` was wrong too**: `u_offset >> BSHIFT` scaled a byte offset as if `BSHIFT` counted
  bytes, but a block is 3072 bytes / 512 words. Now `wtodb(btow(u.u_offset))`.
- **`rdwri.c:71`** did `BSIZE - bp->b_resid` — bytes minus words. Fixed here; it was benign only
  because `b_resid` is 0 on every path today.
- **Done when / verified by `test/biotest`** (`biotest.c` + `biotest.ini`), a new standalone SIMH
  test that links the real `bio.o` against stubs and a recording strategy routine. Three legs:
  `swap()` to physical page 60 in two clamped transfers; `physio()` over a map laid entirely above
  physical page 32, both sides of the `NPAGE * PGSZ` ceiling, the text guard, the data/stack gap,
  the alignment rejects; and `physrange()` directly — virtual pages 1–2 are physically adjacent
  (41, 42) but 0–1 are not (39, 41), since the image's u-area page sits between them and is not in
  the map, so the contiguity walk is exercised with no hand-built descriptor. Six bite tests are
  listed in `biotest.ini` and all six fire on the predicted check.
- **`mem.c` was deferred** — see the entry below.

**18b. The drum/disk driver (`dev/hd.c`).** `hd.c` is a skeleton: `hdstrategy()` sets `B_ERROR` and
calls `iodone()` on every request, and `hdintr()` is empty. `rootdev`, `swapdev` and `pipedev`
(`conf.c:52-54`) all name major 0, so this one strategy routine stands between the kernel and every
criterion below. Write the real thing against the `033 «увв»` channel: control-word assembly with the
9-bit physical page number, the zone model (8 service words at the controller's fixed low buffer —
`010` drum 1, `030` disk 3 — plus 1 Kword of data), request queueing through `hdtab`, and completion
off ГРП in `hdintr()`. Read [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) first.
- **Done when** `iinit()` reads the super block and `swap()` round-trips a page.
- Wants a new standalone SIMH test (see the notes at the end of this file): attach a disk image,
  issue one zone read, assert the words landed where the control word said.

---

## Deferred, owned by no task

Loose ends the finished work left behind. None blocks 18.

* **/dev/mem and /dev/kmem (`dev/mem.c`).** Split out of 18a, which it shares no code with: the
  `b_paddr` work is about `struct buf`, and `mmread`/`mmwrite` never touch one. Minors 0 and 1 are
  stubbed to `ENXIO` today; minor 2 (/dev/null) works. /dev/mem must reach a physical page above
  `0100000`, which needs a `copyseg`-style mapped bracket (`kernel/seg.s` is the worked example) —
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
  so arming it means writing М034/М035, and `procxmt()` has to re-arm after each `T_BREAK`. The
  sites still carry the old `TODO 17` markers: `sig.c` (cases 6 and 9), `trap.c` (`T_BREAK + USER`)
  and `GRP_BREAKPOINT` in `include/sys/besm6dev.h`. A `ptrace` feature, not a layout one.
* **`sendsig()` pushes one word and no more.** The direction and the units are right now, but there
  is no signal frame proper — no saved accumulator or R, and no `sigreturn` path back through it.
  Nothing exercises signal delivery yet; build it when something does.
* **`uflush`/`uload` copy the whole 1024-word page.** Copying only up to the saved r15 —
  `struct user` plus the live stack, typically ~300 words — was planned and never done. It is a
  performance change, not a correctness one.
* **`u.u_dirp = (caddr_t)u.u_arg[0]` is an `int`→`char *` conversion**, and a `char *` built by hand
  from an `int` gets the fat-pointer byte-offset field wrong (see `usermem.s`, `fubyte`).
  Pre-existing, affects every path-taking syscall, and belongs with `iomove()`'s byte handling
  rather than with syscall marshalling. Task 17 fixed the *exec* instance of this — the arg-string
  cursor now carries an explicit `(word, offset)` pair — which is the pattern to copy here.
* **`addupc()` is a stub**, so `profil()` is inert. Was nominally task 17; left there.
* **`time` is never seeded from a wall clock.** The x86 CMOS RTC path is gone and this machine has
  no clock-calendar a program can read, so the epoch starts at 0.
* **`b6sim` does not model the extracode left-half return.** An extracode returns to the *left half
  of the next word* on the real machine, so an instruction packed beside one in a left half is never
  executed; `b6sim` services `$77` inline and continues to the next half-instruction. See
  `../doc/Aout_Simulator.md` §3, `../doc/Dubna_Context_Switch.md` §9 and
  `../doc/Unix_Context_Switch.md` §8. Closing it means auditing every existing `$77` stub for
  what follows it.
* **`sy_nrarg` is read nowhere** and is documented as vestigial: exactly one argument arrives in a
  register on this machine, for any `narg >= 1`.

## Notes for the next standalone SIMH test

Task 18b will want one. What the seven existing tests cost to get right:

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
  reintroducing the x86 stack shuffle makes it fail with `020`, and dropping the `sureg()` after the
  growth with `0212`. A geometry test that cannot fail proves nothing about the geometry.

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

## Still out of scope

The drum and disk drivers (`033` channel programming, `doc/Besm6_Peripherals.md`), the BESM-6
`icode[]`, and the byte-granularity of `iomove()`.

### The disk-block constants are the *other* half-converted unit

Found while doing stage 0, and left alone: this is the filesystem axis, not the MMU retarget, and
settling it means settling the on-disk block layout — a different problem.

`BSIZE` is **3072 bytes = 512 words**, but the constants derived from it were never moved:
`BSHIFT 9` / `BMASK 0777` still describe a 512-*byte* block, and `NINDIR` is now `BSIZE/sizeof(daddr_t)`
= **512** while `NMASK 0177` / `NSHIFT 7` still say 128. So every byte-offset → block conversion in
the kernel is wrong: `rdwri.c:48-49,111-112`, `nami.c:138-156`, `sys1.c:89-128` (the exec arg
staging), `dev/bio.c:492`, `dev/hd.c:135,169`, and `bmap()` in `subr.c:68-121`.

A 3072-byte block is **not a power of two**, so the shifts and masks cannot survive in that form:
either the byte offsets divide and modulo by `BSIZE`, or the filesystem's offsets themselves become
word counts (`BSHIFT`/`BMASK` then describe 512 words, which is what they accidentally already say).
The second is the BESM-6-shaped answer and the one to think about first — but it reaches into
`filsys.h`, `ino.h`, `struct direct` and the on-disk inode, so it wants a plan of its own.

Note `wtodb(x) = (x) >> 9` (words → blocks), added in stage 0, is *correct* either way: a block is
exactly 512 words.
