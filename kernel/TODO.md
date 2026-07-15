# Retargeting the kernel's memory management to the BESM-6 MMU

A work plan. What remains is the *machinery*: the brackets that reach a page the kernel cannot
address, the boot code, the trap gate, the context switch, and the process-memory calls that sit on
top of them.

The model itself is in place. Every size and address in the kernel is a count of 48-bit words (stage
0), and there is a real shadow page table (stage 1): `u.u_upt[8]`, which `sureg()` builds and blasts
into РП and РЗ with twelve `рег`s. The x86 two-level page table, its permission bits and the `PHY`
window are gone — the kernel runs unmapped, so a kernel address *is* a physical address — and `u` is
an absolute symbol at `076000`, not a bss placeholder.

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
   ...      must all end below 076000  (~31 Kwords; today `end` = 061147, 25183 words)
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
   invisible under default SIMH and fatal under `set mmu cache`.
4. **There is nothing to invalidate** — writing РП refills the TLB in the same instruction, so a stale
   translation is not a state the machine can be in. v7's `invd()` is deleted, not stubbed.
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
(`../doc/Simh_Simulator.md`) via `kernel/test/*.ini` — `b6sim` runs a user `a.out` with no kernel
underneath and cannot exercise any of this. `test/mmutest` is the model to copy: it links the kernel's
own objects against a hand-built process, checks itself from C, and lets the `.ini` assert on the
machine state afterwards. Run every MMU test with **`set mmu cache`**.

**Stages 0 and 1 (tasks 1–9) are done and have been removed.** The numbering below is **left as it
was** — task numbers are cited from the source (`utab.c`, `dev/bio.c`, `besm6.S`, `brz.s`) and from
`doc/`.

*Stage 0 — units and sizing.* Retargeted `param.h` to the BESM-6, dropped the x86 drivers, killed the
click (see "The click is dead" above), re-specified every size and address field in words, and passed
the sizing gate.

*Stage 1 — the shadow map.* `u_upt[8]` replaced `u_utab[4]`; the x86 FP state went with it, and so did
`pdir`/`upt`/`mem`/`u` — 2563 words of bss, leaving `end` at `061147`, `076000` less 6521 words of
headroom. `u` is now `076000 A`. `utab.c` is a BESM-6 file: `sureg()` packs the quartets with
`__besm6_aux` and writes twelve registers, `physaddr()`/`useracc()` read descriptors back out with
`__besm6_apx`, and `invd()` is gone rather than stubbed. `estabur()`'s `xrw` and `sep` are inert —
this machine has no read-only page and no I/D separation, which is also why **text pages are left open
to data**: РЗ closes a page to reads as well as writes, and a closed text page would take the program's
own constant pool with it. The extracode vectors now point at `syscall` (`0577`, э77) and `badext`; an
extracode never reaches `trap()`.

Two things stage 1 learned, that the tasks below depend on:

- **`drainbrz()` cannot be written in C** — hence `brz.s`, the one routine in the kernel that has to be
  assembly. The nine stores to physical 1–7 must be consecutive, and `b6cc` materializes the
  destination pointer through a frame slot, so each C store emits two ordinary stores of its own and
  resets the flush counter. It lives in its own file, not in `besm6.S`, so `test/mmutest` can link the
  real routine (`besm6.o` cannot go into a standalone test: its `0500` vector reaches into the C kernel
  and `_start` seeds no stack).
- **The drain is load-bearing, and `test/mmutest` proves it.** Remove the `drainbrz()` calls and it
  fails under `set mmu cache` — the store sits dirty in a БРЗ line tagged with the *virtual* address,
  and a physical read carries a different tag and sees stale memory — and **passes anyway with the
  cache off**. Every task below that reloads РП or reads a process's image out of memory faces exactly
  that hazard.

### Stage 2 — the brackets

**10. `uflush()` / `uload()`. DONE**, and `mmutest` round-trips a u-area under `set mmu cache`.
Three things turned out differently from the sketch above, and each is a fact about the machine
rather than a matter of taste:

- **They live in `kernel/uarea.s`, not `besm6.S`.** The "done when" requires `mmutest`, and
  `besm6.o` cannot enter a standalone test — its `0500` vector reaches into the C kernel and
  `_start` seeds no stack. Same reason, and same shape, as `brz.s`, which they call.
- **The window is virtual pages 1 and 2, not page 0 plus an РП[31] identity.** A store to virtual
  address **0** is dropped and a load returns 0: `mmu_store()`/`mmu_load()` test `addr == 0`
  *before* translation and before the "already physical" tag, so the black hole is in the virtual
  address, whatever page 0 is mapped to. A window there would have silently lost word 0 of the
  u-area — `u_rsav[0]`, a saved register, zeroed on every context switch. Pages 1 and 2 are also
  cheaper: both descriptors live in quartet 0, so it is **one `mod 020`** to steal and one to
  restore, and both window addresses fit the 12-bit short address field, so the copy loop needs no
  `utc`.
- **The bracket holds БлПр and restores ПСВ exactly.** `vtm N,0` writes БлП, БлЗ *and* БлПр from
  the address field — all three — so the plain `vtm 2`/`vtm 3` of `test/mmuhelp.s` would enable
  interrupts as a side effect. During `uload` that is fatal: an interrupt builds its frame on the
  kernel stack, which is in the very page being overwritten. The bracket runs at `02003`/`02002`
  and puts ПСВ back with `ita 021`/`ati 021` (supervisor takes a 5-bit register number).

Both drains are load-bearing, and `mmutest` proves each separately: without the one *before* the
copy, a user's mapped stores are written back through the stolen map (returns 17); without the one
*after* it, the copy never reaches memory (returns 15). Both pass with the cache off.

The contract, for task 16: `uflush` only reads the live page and is safe to call from C, but
**`uload` overwrites the kernel stack its caller is standing on**, so only `resume()` — assembly,
keeping its state in registers — may call it. Both are map-neutral: the old quartet 0 is read
before the copy and put back after.

Still to do, as planned: **copy only up to the saved r15** (`struct user` plus the live stack,
typically ~300 of the 1024 words).

**11. `copyseg()` / `clearseg()`. DONE**, and `mmutest` copies a page above `0100000` to another
and back under `set mmu cache`. Same shape as task 10: steal two virtual pages as windows,
copy/zero register-only, restore the quartet from `u.u_upt[]`. They take **page-aligned word
addresses**, not click numbers. How it turned out:
- **They live in `kernel/seg.s`, not `besm6.S`** — for the same reason as `uarea.s`/`brz.s`: the
  "done when" is `mmutest`, and `besm6.o` cannot enter a standalone test (its `0500` vector reaches
  into the C kernel and `_start` seeds no stack). The C stubs that were in `utab.c` are gone; wired
  into both Makefiles next to `uarea.o`.
- **Windows are virtual pages 1 and 2** (`02000`/`04000`), not 0–1: virtual address 0 is a black
  hole (task 10). Both descriptors share quartet 0, so one `mod 020` steals both and one restores;
  both window addresses fit the 12-bit short field, so the copy loop needs no `utc`. `copyseg`
  scatters the source into the k=1 slot and the dest into the k=2 slot
  (mask `#(.47|.43|.39|.35|.31|.[11:15])`) and `aox`es them; `clearseg` uses the one k=1 window and
  clears A with a bare `xta` (the addr-0 black hole reads 0) before the zeroing loop.
- **The bracket holds БлПр and restores ПСВ exactly**, verbatim from `uarea.s` (`vtm 02003`/`02002`,
  `ita`/`ati 021`) — interrupts are held off across the copy even though `copyseg` does not overwrite
  the stack page, which keeps it identical to the proven `uflush`/`uload` shape.
- **`copyseg` needs no `s == d` guard**: no caller passes equal addresses, and a page copied to
  itself through two windows is idempotent.
- `mmutest` (checks 18–19) fills the live page at `076000`, `copyseg`s it low→high then high→high,
  reads it back mapped, then `clearseg`s and checks zero. The **trailing** drain is load-bearing and
  `mmutest` proves it (remove it and the copy never reaches memory); the **leading** drain is the
  standing "drain before every РП write" rule and is the same construct the u-area leg's return-17
  check already exercises. Adding `seg.o`'s two mask constants grew the const section, so the test's
  halt PC moved `00575`→`00604` (`mmutest.ini` updated).

**12. `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword`. DONE**, and `mmutest` copies a buffer
both ways through the user map, round-trips a word and a byte at every offset, and rejects an
unmapped address with -1, all under `set mmu cache`. The user's map is already in РП, so there is no
window: the loop toggles БлП per word — read the user word mapped, store it to the kernel buffer
unmapped (or the reverse). Register-only. How it turned out:

- **They live in `kernel/usermem.s`, not `besm6.S`** — same reason as `uarea.s`/`seg.s`: the
  "done when" is `mmutest`, and `besm6.o` cannot enter a standalone test. Wired into both Makefiles
  next to `seg.o`; the `besm6.S` stubs are gone, replaced by a one-line pointer.
- **`copyin`/`copyout` are word-only**, exactly like the x86 original: they copy `nbytes / NBPW`
  whole words. Every caller passes word-aligned addresses and a word-multiple count (iomove's
  `NBPW` guard, the struct copyouts, `icode`); an unaligned copy stays on the `fubyte`/`subyte`
  byte path in `iomove`. The ÷6 is a subtract-6 loop (`divword`) that **floors and cannot spin on
  a non-multiple** — the broken `iomove` `& (NBPW-1)` guard is a separate, out-of-scope bug.
- **Validation is `useracc()`, called from the routine itself** — no C caller calls it, so this is
  what "the C caller has already validated" resolves to: the routine calls `useracc`, and a range
  that runs into a zero descriptor returns -1. There is no `nofault`/trap path.
- **No `drainbrz`.** РП is never written, so the "drain before the РП write" rule does not apply,
  and copyout's mapped stores go back through the *same* loaded map — no tag hazard. (A context
  switch, which does reload РП, drains; that is `resume()`'s job.)
- **The bracket is `uarea.s`'s, minus the `mod`/drain**: save ПСВ (`ita 021`), toggle БлП with
  `vtm 02002`/`02003` holding БлЗ+БлПр, copy register-only, restore ПСВ. Interrupts are held off
  across the copy — these copies are small.
- **`fubyte` extracts the byte with `asx` on the fat pointer's own exponent field** (it is
  `64 + 8·off`, so `asx` right-shifts the word by `8·off`); `subyte` is a read-modify-write using a
  6-entry `bytemask[]` and `aux` to scatter the new byte, with the offset field (bits 47–45) read
  out by `asn 64+44`. Note the fat-pointer **marker bit (48) is load-bearing** for `fubyte`:
  building a fat pointer by hand from an `int` (no marker) makes `asx` shift by `8·off − 64` and
  return 0 — the test must use the compiler's `int*`→`char*` conversion, which is the real path.
- **The fault return is the clean C `-1` (`.[1:41]`), not all 48 ones**, so `fubyte(...) == -1` at
  a caller (`sig.c`) matches the compiler's own `-1` literal.
- `mmutest` grew checks 20–24; the const growth happened to leave the halt PC at `00604`, so the
  `.ini` PC assertion is unchanged and its single `ACC == 0` check catches any of them.

**13. `bcopy` / `bzero` in `besm6.S`** — plain unmapped word loops. Delete `ld_cr0/2/3`, `inb`, `outb`,
`insw`, `outsw`. (`savfp`, `restfp`, `stst` and `invd` are already gone — stage 1 took them: the first
three with the u-area's FP block, and `invd` outright rather than as a no-op, since nothing calls it.)
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
  which are now words; the `1024` ceilings should be `NPAGE * PGSZ`. Stage 1's `useracc()` is what
  finally gives it a way to validate a user address, so it is fixed here, along with `b_paddr`.

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
