# Retargeting the kernel's memory management to the BESM-6 MMU

A work plan. The kernel still carries the memory management of the v7/x86 port it came from: a
two-level page table (`pdir`/`upt`) of 4 KB pages, with permission bits in the page-table entries.
None of that exists on this machine. `besm6.S` papers over it — `u` is a 512-word bss placeholder
that is neither per-process nor mapped, and `pdir`/`upt` are dummy arrays that exist only so
`utab.c` links.

The units are already converted (stage 0, done): every size and address in the kernel is a count of
48-bit words, and the `PHY` window is gone — the kernel is unmapped, so a kernel address *is* a
physical address.

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
   ...      must all end below 076000  (~31 Kwords; today `end` = 066105, 27709 words)
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
| `copyseg`/`clearseg` | reach a physical page above `0100000` | steals virtual pages 0–1 as windows (one `mod 020`), restores the quartet from `u.u_upt[]` afterwards |
| `uflush()`/`uload()` | save/restore the u-area across a context switch | steals virtual page 0 for the process's u home, and sets РП[31] = 31 (identity) so `076000` names the u-area itself; then it is a straight mapped copy |

An interrupt taken inside a bracket is harmless: the hardware forces БлП = 1 at the vector, so the
handler sees the kernel's normal unmapped world and its stack at `076000` resolves physically; `выпр`
restores БлП from СПСВ and the bracket resumes mapped.

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
   invisible under default SIMH and fatal under `set mmu cache`.
4. **`invd()` is a no-op** — writing РП refills the TLB in the same instruction.
5. **A fault reports the faulting *page* (ГРП bits 5–9), and the saved PC points *past* the faulting
   instruction.** Anything that means to retry — stack growth — must back the PC up using
   `SPSW_NEXT_RK` and `SPSW_RIGHT_INSTR`.

### The u-area invariant

The live u-area is at `076000`; the copy in the process's image at `p_addr` is stale between
switches. A kernel global `uhome` records whose home the live u-area belongs to.

* `resume(paddr, lbl)`: if `paddr != uhome`, `uflush(uhome)`, then `uload(paddr)`, then
  `uhome = paddr`. Only then restore r1–r7, r13, r15 from `lbl` — which, being at `076000+n`, now
  names the *new* process's label. `sleep()`'s `resume(u.u_procp->p_addr, u_qsav)` (`slp.c:88`)
  takes the `paddr == uhome` fast path and copies nothing.
* `newproc()` must `uflush()` **before** copying the parent's image, or the child inherits a stale
  `u_ssav` and never returns from its `save()`.
* `expand()` copies only the data/stack pages (skipping the u page) and sets `uhome = a2`; the live
  u-area is untouched and reaches the new home at the next switch.
* `xswap()` of the *current* process must `uflush()` first — `swap()` DMAs the image straight out of
  physical memory and would otherwise write a stale `struct user` to disk.

**Anything else that reads the current process's image out of memory must flush first.** This is the
sharpest edge in the whole design.

---

## Tasks

Each task leaves the tree building (`cd kernel && make`). Verification is under **SIMH**
(`../doc/Simh_Simulator.md`) via `kernel/test/*.ini`, in the style of the existing `hello.ini` /
`sctest.ini` — `b6sim` runs a user `a.out` with no kernel underneath and cannot exercise any of
this. Run every MMU test with **`set mmu cache`**.

**Stage 0 — units and sizing (tasks 1–5) is done and has been removed.** It retargeted `param.h` to
the BESM-6, dropped the x86 drivers, killed the click (see "The click is dead" above), re-specified
every size and address field in words, and passed the sizing gate: the image is 27709 words with
`end` at `066105`, `076000` less 4027 words of headroom. The numbering below is **left as it was** —
task numbers are cited from the source (`utab.c`, `dev/bio.c`, `besm6.S`) and from `doc/`.

### Stage 1 — the shadow map

**6. `include/sys/user.h`: the u-area.** Drop the x86 FP state (`u_fps[108]`, `u_fper`,
`u_fpsaved`). Replace `u_utab[4]` with `unsigned u_upt[8]` — the ready-to-load shadow map (four РП
descriptors per word, plus the matching РЗ byte in bits 21–28 of the even words). Rewrite the stale
PDP-11 header comment: the u-area is one physical page at `076000`, `struct user` at the bottom and
the kernel stack growing up from `u_stack` to `0100000`. Check the size: `struct user` should come
out around 140 words, leaving ~880 for the stack.
- **Done when** a `sizeof` print (or the `.ast`/`b6nm` output) confirms `sizeof(struct user) < 200`.

**7. `kernel/besm6.S`: symbols and the vector block.** `u = 076000` as an absolute global (`.globl u`
+ `u = 076000`; `b6as` emits `N_EXT|N_ABS` and `b6ld` passes it through unrelocated). Delete the
`pdir`, `upt`, `mem` and `u` bss placeholders. Add `uhome`. Extend the `.const` block to `0577` so
the extracode vectors exist — today text starts at `0522`, so `0550`–`0577` is *code* (`test/crt0.s`
already flags this): `uj syscall` at `0577` (э77), a bad-extracode stub for the rest.
- **Done when** `b6nm unix | grep ' u$'` prints `076000 A u`, and `b6disasm` shows the vectors at
  `0500`, `0501` and `0577`.

**8. `drainbrz()` in `besm6.S`.** Nine consecutive stores to physical 1–7 with mapping off (the
first only *arms* the counter; eviction starts with the second, so nine are needed to drain all
eight lines). Nothing else may be interleaved — any ordinary store resets the counter.
- **Done when** it assembles and is called from `sureg()`.

**9. `kernel/utab.c`: rewrite (C).**
- `sureg()` — rebuild `u.u_upt[8]` from `p_addr`, `x_caddr`, `u_tsize`/`u_dsize`/`u_ssize` (words,
  shifted by `PGSH`), then `drainbrz()` and blast it out with twelve `__besm6_mod()` calls.
  *Writing РП here is safe precisely because the kernel is unmapped* — it changes nothing about how
  the kernel addresses its own memory. Unallocated pages get РП = 0 (non-executable) and their РЗ
  bit set. `__besm6_aux` does the quartet packing.
- `estabur()` — **already in**: stage 0 gave it the word limits `nt+nd+ns <= MAXMEM`,
  `nt+nd <= USTKPAGE*PGSZ`, `ns <= (NPAGE-USTKPAGE)*PGSZ`, because its old x86 bound (`> 1023`) would
  have rejected any image over 1023 *words* the moment the units changed.
- `physaddr(v)` — shadow lookup, returning a physical **word** address (0 if unmapped).
- `useracc(addr, count, rw)` — new: walk `u_upt`; an РП = 0 page means `EFAULT`. This is what lets
  the assembly copies assume a valid address, and it is why v7's whole `nofault` machinery
  disappears.
- Delete `pdir[]`, `upt[]`, `invd()`, `clearseg`/`copyseg`'s `PHY` window (task 11 rewrites them).
- **Done when** `test/mmutest.c` builds a map, calls `sureg()`, and a `.ini` `examine mmu РП0..РП7
  РЗ` matches the worked examples in `doc/Memory_Mapping.md` §"Programming the MMU".

### Stage 2 — the brackets

**10. `uflush()` / `uload()` in `besm6.S`.** Steal virtual page 0 for the process's u home (`mod
020`) and set РП[31] = 31, identity, so `076000` still names the u-area itself (`mod 027`); drain;
clear БлП; copy `USIZE` words register-only; set БлП; drain; restore the two quartets from
`u.u_upt[]`. Optimisation, once it works: copy only up to the saved r15 (`struct user` plus the live
stack, typically ~300 of the 1024 words).
- **Done when** `mmutest` round-trips a u-area: fill `076000`, `uflush()` to a page above
  `0100000`, scribble on `076000`, `uload()` back, compare.

**11. `copyseg()` / `clearseg()` in `besm6.S`.** Same shape: steal virtual pages 0–1 as windows,
copy/zero register-only, restore the quartet from `u.u_upt[]`. They now take **page-aligned word
addresses**, not click numbers.
- **Done when** `mmutest`, under `set mmu cache`, copies a page above `0100000` to another and
  reads it back correctly. A missing drain shows up here and nowhere else.

**12. `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword` in `besm6.S`.** The user's map is
already in РП, so there is no window: the loop toggles БлП per word — read the user word mapped,
store it to the kernel buffer unmapped. Register-only. Word at a time when both sides are
word-aligned; the six-chars-per-word edges are read-modify-write. The C caller has already validated
the address with `useracc()`, so there is no fault path.
- **Done when** `mmutest` copies a buffer both ways through a hand-built user map and rejects an
  address in an unmapped page with `EFAULT`.

**13. `bcopy` / `bzero` in `besm6.S`** — plain unmapped word loops. Delete `savfp`, `restfp`, `stst`,
`ld_cr0/2/3`, `inb`, `outb`, `insw`, `outsw`; `invd` stays a no-op.
- **Done when** the kernel links with no stub left that silently returns 0.

### Stage 3 — boot, traps, switching

**14. `_start` and boot.** Zero bss — from `edata` to `end`, both defined by `b6ld` (`besm6.S` already
references them; there is no `kend` variable to set) — size physical memory into `phymem`, put r15
into the u page, call `main()`: all unmapped, which is the mode the machine resets into. Then
`uhome = proc[0].p_addr` in `main.c`, and in `machdep.c` drop the 8253 and the RTC. (Stage 0 already
landed the rest of the C half: `proc[0].p_addr` = the first free word `0100000`, `p_size = USIZE`, and
a `startup()` that frees words `0100000 + USIZE`…`phymem` in one extent — never page 0, a zero РП entry
means "not mapped" — with the x86 memory hole gone.)
- **Done when** `load unix; go` prints `mem = …` from `startup()` and halts at a known PC.

**15. Trap / extracode / interrupt entry and exit (`besm6.S`, `include/sys/reg.h`, `trap.c`).** Entry
is now trivial — the hardware's forced БлП/БлЗ is already the kernel's mode, so there is no map to
switch and nothing to drain: save the frame onto the kernel stack at `076000`, point `u.u_ar0` at
it, call C, restore, `выпр`. `reg.h` describes the BESM-6 frame (ГРП, СПСВ, ЭРЕТ/ИРЕТ, accumulator,
r1–r15) and the `u_ar0[]` indices. `trap.c` dispatches on ГРП: bit 20 = data protection (faulting
page in bits 5–9 → `grow()` or SIGSEG), bit 14 = instruction protection, bit 13 = illegal
instruction, bit 15 = check; extracode `077` → syscall. Implement the restart protocol — back the PC
up from `SPSW_NEXT_RK`/`SPSW_RIGHT_INSTR` — before retrying a faulted instruction.
- **Done when** an icode that issues `$77 N` traps into the kernel and returns to user mode.

**16. `save()` / `resume()` (`besm6.S`) and the u-area invariant (`slp.c`).** `save()` stores r1–r7,
r13, r15 into the label. `resume()` implements the invariant above, with interrupts masked across the
swap. `slp.c`: `newproc()` calls `uflush()` before copying the parent's image; `expand()` skips the
u page and sets `uhome = a2`; `xswap()` of the current process flushes first.
- **Done when** `newproc()` + `swtch()` alternate between proc 0 and proc 1: both `save()` returns
  fire, and each process sees its own `u`.

### Stage 4 — process memory and I/O

**17. Stack growth and the user layout (`sig.c`, `sys1.c`, `text.c`).** `grow()` takes a **page
number** (that is all the machine reports) and **loses its `copyseg` shuffle** (`sig.c:249-255`):
with an upward stack a new page is appended at a higher virtual address *and* at the end of the
image, so the existing stack pages keep their addresses. `exec()` seeds the user stack at `070000`
growing up; `sbreak()`'s shuffle stays (data still grows in the middle of the image) but the break
stops at page 28; the `u_ar0[EIP] += 2` fork trick is replaced by returning distinct accumulator
values. Three pieces of this are **already in** — they fell out of stage 0's `ctob` removal: `core()`
writes one `USIZE`-word u-area block, `procxmt()`'s "read u" takes a plain word index, and the sizes
from the `a.out` header go through `btow()` + `pground()`.
- **Done when** a two-page icode forks and execs, and touching the word past the stack top grows the
  stack instead of raising SIGSEG.

**18. I/O addresses (`include/sys/buf.h`, `dev/bio.c`, `dev/mem.c`).** Add `unsigned b_paddr` to
`struct buf` — a physical **word** address, used when `B_PHYS`: a `char *` is a fat pointer with a
15-bit word field and cannot name a physical word above 32767. `swap()` and `physio()` fill it
instead of `PHY + physaddr(…)`. Device transfers take a *physical* page (the drum/disk control word
carries a 9-bit page number and reaches all 512 Kwords), so **buffer I/O needs no bracket at all** —
kernel buffers are unmapped and their address *is* their physical address. `mem.c`: /dev/mem through
a `copyseg`-style bracket, /dev/kmem direct.
- **Done when** `iinit()` reads the super block and `swap()` round-trips a page.
- Stage 0 deliberately left `physio()` (`dev/bio.c:462-482`) alone — it is the one piece of memory
  arithmetic still in the old units. It derives page numbers with `>> PGSH` from `(unsigned)u.u_base`,
  which is a *fat pointer*, not a byte address, and compares them against `u_tsize`/`u_dsize`/`u_ssize`,
  which are now words; the `1024` ceilings should be `NPAGE * PGSZ`. It cannot be made right before
  `useracc()` (task 9) gives it a way to validate a user address, so it is fixed here, with `b_paddr`.

---

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new): 1024 words
  each way, or ~300 with the "copy only up to the saved r15" optimisation. This is the cost of an
  unmapped kernel; in exchange the trap path costs *nothing* and `copyin` needs no window.
* **The u-area invariant is a footgun.** A fifth flush site, added later and forgotten, will be a
  very confusing bug.
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
