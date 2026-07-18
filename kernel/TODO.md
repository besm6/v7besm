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

**13. `bcopy` / `bzero` in `besm6.S`. DONE**, and `cd kernel && make` links with no stub left that
silently returns 0 (image ends at `060040`, 24608 words, still below `076000`). How it turned out:

- **Renamed `wcopy`/`wzero`, and they take a WORD count**, not a byte count. Every call site converts
  its byte count with `btow()` (`param.h`), and the three whole-block copies use the new `BSIZEW`
  (= 512, block size in words); `BSIZE` (bytes) stays for the still-deferred filesystem block-unit
  rework. So the loop is pure words with no six-chars-per-word tail — it is `copyin`'s inner loop
  (`usermem.s`) minus the per-word БлП toggle, the `useracc` validation and the ПСВ save: plain
  unmapped, register-only (r10 count, r11/r12 pointers, word in A), no window and **no `drainbrz`**
  (РП is never written). `aax #077777` strips the caller's fat pointer to a 15-bit word address.
- **`DIRSIZ` is now 24** (a whole 4 words), so `struct direct` is 5 words and directory entries are
  word-aligned — which incidentally makes `nami.c`'s `b_addr + (u_offset & BMASK)` source a word
  boundary (fat-pointer offset 0), so the plain word copy there is correct with no byte shuffling.
- **`cli`/`sti` are implemented**: set / clear the БлПр bit of ПСВ (`M[021]`, bit `02000` = external
  interrupts off; `doc/Memory_Mapping.md`). It is a read-modify-write (`ita 021` / `aox 02000` or
  `aax 075777` / `ati 021`), **not** a `vtm`, because `vtm` writes the whole ПСВ and would clobber
  the other mode bits (БлП/БлЗ/ПОП/ПОК). Verified from `unix.dis`: the immediates materialise through
  the constant pool as `02000` and `075777`.
- **The seven x86 routines are deleted** (`ld_cr0/2/3`, `inb`, `outb`, `insw`, `outsw`), with their
  prototypes, and every remaining caller excised so the link stays clean: `dev/hd.c` and `dev/sr.c`
  are kept as **BESM-6 driver skeletons** (the x86 port-I/O stripped to `// TODO` stubs, but still
  built and wired in `conf.c`, so their `bdevsw`/`cdevsw` hooks resolve); `machdep.c` lost the 8253
  PIT and the CMOS RTC (`clkstart()` is now just `spl0()`, and `readrtc`/`getrtc`/`inrtc` are gone —
  time-of-day is no longer read at boot); and `trap.c`'s x86 panic dump lost its `cr0/cr2/cr3` line.
  (`savfp`, `restfp`, `stst` and `invd` were already gone — stage 1 took them.)
- **No SIMH test.** `wcopy`/`wzero`/`cli`/`sti` have no MMU interaction, so unlike tasks 10–12 there
  is nothing for `test/mmutest` to exercise; the copy loop is the already-proven `copyin` inner loop
  with the map toggles removed, and the build/link gate is the task's own "done when".

### Stage 3 — boot, traps, switching

**14. `_start` and boot. DONE.** `_start` seeds the kernel stack pointer (r15) and calls `main()`;
the machine resets straight into the kernel's own mode (supervisor, unmapped, interrupts off, faults
halt — `doc/Memory_Mapping.md`, "Reset state"), so there is nothing else to set up. Everything the
boot phase must do that can be said in C is done at the top of `main()`. How it turned out, and why
it differs from the sketch above:

- **bss-zero and memory sizing moved to C (`main.c`), out of `_start`.** The bss size is `end - edata`,
  a difference of two linker-defined externals, which `b6as` rejects ("`-` requires its right operand
  to be absolute", `doc/Assembler_Manual.md` §7); in C the compiler emits the pointer subtraction for
  free. So `main()` opens with `wzero(edata, end - edata)` (bss cleared before anything reads it —
  `_start`, the prologue, and `wzero` touch no bss) and then `phymem = 512 * 1024` (the fixed SIMH
  `MEMSIZE`; the kernel runs unmapped, 32 Kword reach, and cannot probe the 512 Kword store). Both
  precede `startup()`, which is the first code to read either. **The `wzero` is now guarded out under
  an `ON_SIMH` `#ifdef`**: SIMH starts every word at zero, so the clear is redundant on the simulator;
  the code is kept for the day the kernel boots on real hardware.
- **r15 = &u.u_stack (~`076214`).** `machdep.c` seeds it as `int *const ustkbase = &u.u_stack[0]`
  — `u_stack` is the last member of `struct user`, so this is UBASE + wordsizeof(struct user) - 1.
  b6cc now folds `&u.u_stack[0]` (a symbol+offset) into the static relocation, so it is spelled
  directly; the earlier `&uarea[…]` integer-alias workaround (b6cc could not take a struct member's
  address in a static initializer) is gone, and with it the `extern int uarea[]` in `machdep.c`.
  `_start` loads it with `xta <ustkbase>; aax #077777; ati 017`.
- **`uhome = proc[0].p_addr` and the 8253/RTC drop** — `uhome` is set in `main.c` right after
  `proc[0].p_addr`; the `machdep.c` clock/RTC removal was already done in task 13 (`clkstart()` = `spl0()`).
- **`make run` + `kernel/unix.ini`** added to boot the image under SIMH (no `set mmu cache`: task-14 boot
  programs no page registers).

Verified under SIMH: r15 = `076214`, `phymem` = `02000000` (524288), `uhome` = `0100000`, `startup()`
runs and prints `mem = ` on the console. **Not fully done:** the *number* in `mem = …` does not render
on the console — `printf`/`printn` are proven correct (the exact source prints `mem = 490496 words`
under `b6sim`) and were reworked to use `<stdarg.h>` (the `&fmt+1` idiom, `%D` dropped), so this is a
polled-console / driver issue (chars dropped after `mem = `; `putchar`'s `msgbuf` logging also found not
populating). To debug later; it is not a `_start`/boot defect.
- **Done when** `load unix; go` prints `mem = …` from `startup()` and halts at a known PC. *(Prints
  `mem = `; a clean halt awaits the trap/scheduler of tasks 15–16 — today it runs on into the stub
  init/scheduler.)*

**15. Trap / extracode / interrupt entry and exit (`besm6.S`, `include/sys/reg.h`, `trap.c`).** The
entry is *not* the map-free one-liner an earlier draft of this task claimed, and the one door that is
written — `intrgate` — inherits that mistake. No map switch and no БРЗ drain, true (the hardware's
forced БлП/БлЗ is already the kernel's mode); but there are **three doors with two save disciplines**,
and the async ones must preserve more of the machine than the ABI's callee-saved set. Read
[../doc/Context_Switch.md](../doc/Context_Switch.md) first — §4/§7/§8/§9/§13/§14 are the authority for
everything below — and use `intr.c`'s `extintr()` as the model of a C handler reached from an asm gate.

This work is split into five sub-tasks, done in order; each leaves the tree building
(`cd kernel && make`). Four facts cut across all of them:

- **Async vs. synchronous.** An external interrupt (`0501`→`intrgate`) and an internal fault
  (`0500`→`trap`) land between arbitrary instructions, so the interrupted code owns *every* register
  and the stub must save the full visible machine the C handler can touch. An extracode
  (`0577`→`syscall`, `0550`–`0576`→`badext`) is a synchronous *call*: the caller owns its live
  registers, so the gate saves almost nothing (Context_Switch.md §9), and the hardware has already
  clobbered r14 (= the effective address).

- **The stack switch — r15 is not banked.** The PDP-11 got the kernel/user stack swap for free from a
  mode-banked SP; the BESM-6 has one stack register shared across modes, so **every door entered from
  user mode must repoint r15 at the kernel stack by hand, and every door that nests inside the kernel
  must leave it alone.** There is exactly one kernel stack — the u-area is a single fixed physical page
  — so the switch is a load of a constant, the same `15 vtm [ustkbase]` `_start` already uses (r15 ≈
  `076214`). The signal is the saved mode word: the trap stashes `(old ПСВ БлП/БлЗ/БлПр) |
  IS_SUPERVISOR(RUU)` in СПСВ (`M[027]`) — Memory_Mapping.md, "Entering and leaving supervisor mode" —
  so **`СПСВ & 014`** (РежЭ | РежПр, `RUU_EXTRACODE|RUU_INTERRUPT`) is zero **iff the interrupted
  context was user**. Test *that*, not БлП: `copyin`/`copyout` run in supervisor mode with БлП clear, so
  a БлП test would misread a fault taken mid-`copyin` as "from user" and reset r15 out from under the
  syscall's frame. Why it bites: the trap forces БлП *on*, so a user r15 (exec seeds it at `070000`,
  growing up — task 17) is now an **unmapped physical** word index pointing *into the kernel image below
  the u-area*; run a C handler on it and it silently corrupts the kernel. `trap`/`syscall` get the switch
  implicitly the moment they "build the frame on the kernel stack"; `intrgate` calls `extintr()` with no
  frame step, so it must do the switch explicitly.

- **One exit, three doors.** An extracode returns via ERET (`2 ij`), an interrupt/fault via IRET
  (`3 ij`), and the two cannot share a hardcoded `выпр` unless the door is normalised first. Steal
  Dubna's `OUTMACRO` (Context_Switch.md §8): copy ERET→IRET in two instructions and let one `3 ij`
  serve both gates — the syscall return then inherits the interrupt epilogue's `runrun`/pending checks
  for free. The fault path still needs a PC fixup the extracode path does not (see 15c).

- **Only one saved-state slot.** СПСВ, IRET and ERET are single registers, not a stack: a second
  internal fault while handling the first is fatal (`STOP_DOUBLE_INTR`). A gate must save СПСВ/IRET to
  the kernel stack before it does anything that can fault or re-enables interrupts (Memory_Mapping.md,
  "Notes for an operating-system port", note 12).

**Verification is a `kernel/test` case in the spirit of `sctest`, but entered from user mode** — the
current `sctest` enters from kernel mode and would pass even with the stack switch missing, so it
cannot stand in. Task 16's real entry-to-user (`resume()`/SELECT-forge) does not exist yet, so 15a
**builds the scaffolding**: a new test crt0 that forges user-mode entry (a Dubna `SELECT`-style гейт —
forge IRET+СПСВ, `выпр` into user) and programs a user map with the `mmuhelp`/`sureg` machinery
`mmutest` already links. That harness is reused by 15c and 15d.

**15a. `intrgate`: save the interrupted context into the reg.h frame, and switch the stack. DONE**, and
a from-user test (`kernel/test/uintr`) takes an external interrupt in forged user mode and confirms
R, Y (РМР), M[16] (M[020]) and r15 all survive it, on the real machine. The gate goes **beyond** the
§14 subset sketch below: it builds the full canonical reg.h frame (§2 — `0 A · 1 R · 2 Y · 3 IRET ·
4 ERET · 5 СПСВ · 6 M16 · 7 M15 … 21 M1`) on the kernel stack with the Dubna FULSAV `xts`/`its`
pipeline (§6) and reloads it with the symmetric `stx`/`sti` pop (§7/§12), so the async door produces
exactly the frame the trap/syscall gates (15c/15d) and `resume()` (16) will read. How it turned out:

- **Only five temp cells, not the flat subset block.** The FULSAV fill is a store-and-load pipeline
  whose only scratch is the accumulator, so any register the pipeline can reach — M1–M14 and
  IRET/СПСВ (via `its`) — is read **live** into the frame, because the fill runs *before*
  `13 vjm extintr` clobbers anything. Only the five the pipeline cannot reach are spilled first:
  `save_a`, `save_r`, `save_y` (A/R/Y can't ride an `its`/`xts` chain), `save_m16` (must be spilled
  before the stack-switch `u1a`/`vtm`, whose `utc` for a long address would clobber M16), and
  `save_m15` (the pipeline's own stack pointer, repointed by the switch). The restore is the exact
  inverse pop; the spec-register reloads (`sti 027`/`032`/`033`) are no-ops — extintr never touched
  M027/M032/M033 — but keep it symmetric.
- **`besm6.S`'s `intrgate` is bare-addressed, not escaped.** The five cells are in `.const`/`.text` (they
  link at `~0700`, under the 12-bit short-address field), so `atx save_a` reaches them directly. This
  is load-bearing, not a nicety: a `< sym >` escape emits a `мода`/`utc` that loads M[16], so an escape
  ahead of the `ita 16` M[16] **save** would overwrite the very C register the gate is saving.
- **The frame lives on the kernel stack, at M15.** After the switch M15 *is* the frame base — the
  kernel-stack base (`[ustkbase]`) when the interrupt came from user, or the interrupted kernel SP when
  it nested — and the fill builds there, exactly as reg.h describes. **`u.u_ar0` is not stored by the
  gate**: the kernel sets it in C (`trap.c: u.u_ar0 = &tr.acc`), and `extintr()` is frame-agnostic
  (takes `void`); when `clock()` joins this path (15e) it receives the frame by value and points
  `u_ar0` itself. Doing it in asm would need a hardcoded `u_ar0` offset (the `UPT=35`-class constant
  that bit `u_upt` in commit af5b619) for no runtime benefit today.
- **The test harness (`crt0u.s` + `uintr.c`) is the scaffolding 15c/15d reuse.** `gouser()` forges a
  Dubna `SELECT`-style entry — plant IRET + СПСВ (with `SPSW_MOD_RK` so `выпр` re-arms the modifier),
  set R via `ntr`, Y via the `xta`/`aex` side effect, r15 to a user-stack value — and `3 ij` into a
  tiny mapped user program at virtual page 0. `sureg()` builds the map from `uprog`'s own physical page
  (its word address comes from a linker-filled `.word uprog`, **not** `(unsigned)&uprog`, whose fat
  pointer is not simply its low bits).
- **The interrupt is raised in software** with `увв 031` ("simulate ГРП") on `GRP_TIMER`, so there is
  no device and no timing: unmask МГРП, raise the bit while БлПр still masks it, and it fires at
  `uprog`'s first instruction the instant `выпр` clears БлПр — which is what makes the modifier-armed
  M[16] test deterministic. `extintr()` dismisses it **and masks МГРП**, because the interval timer
  re-arms `GRP_TIMER` at reset and would otherwise storm. Keeping БлПр set through `main` (an explicit
  `vtm 02003` in `_start`) is what stops the bit from firing early, in supervisor.
- **The user reports back through a deliberate data-protection fault, not `стоп`.** `стоп` in user mode
  re-dispatches as extracode э63, but reset leaves ПоК set, so it check-halts; a data-protection fault
  **ignores ПоП/ПоК and always vectors** (Memory_Mapping.md), so `uprog` reads a closed page to reach
  the `report()` checker at vector `0500`. `report()` runs unmapped and compares five things.
- **The read-back values live in memory, not index registers** — the index registers are 15 bits and
  would truncate SENT / R‹‹41 / Y. `uprog` stores each full-width to its mapped data page; `report()`
  reads them back through the still-loaded map and the physical stack-switch sentinel unmapped.
- **The stack-switch discriminator** seeds physical `074000` (= the forged user r15) with a sentinel:
  if `intrgate` fails to switch, `extintr()`'s frame — БлП forced on, so r15 is a physical index — lands
  there and overwrites it. `report()` catches that (bit `020`). **Bite-tested:** dropping the switch
  yields ACC `020`, dropping the `xtr save_r` restore yields ACC `2`.
- **No `set mmu cache`.** `intrgate` programs no page registers, so the БРЗ hazard mmutest chases is not
  in play here; the run stays simple.

The original four gaps, for the record — each confirmed against SIMH and Dubna
(Context_Switch.md §14 has the full corrected prologue verbatim):
  - **R** (ALU mode — ω plus the NTR suppress bits). The C ABI exits `NTR 3` / ω = logical
    ([../doc/Besm6_Runtime_Library.md](../doc/Besm6_Runtime_Library.md), the helper contract), so
    `13 vjm extintr` returns with R changed and the resumed user runs in the wrong arithmetic mode.
    The hardware does **not** save it: SIMH `op_int_1` and `выпр` never touch RAU, and Dubna saves it
    by hand (`rte`/`xtr`). The "Floating-point state" comment in `besm6.S` — *"the arithmetic mode
    rides in СПСВ across a trap"* — is therefore **false** and must be corrected when this code lands.
  - **Y** (younger-bits): any logical op (`и`/`слц`/`сл`) overwrites it, and the interrupted user
    may hold it live.
  - **C register M[16]**: `extintr()` reaches globals through the compiler's `utc name`+load idiom,
    which overwrites M[16]; the interrupted user's `SPSW_MOD_RK` still rides in СПСВ, so the closing
    `3 ij` re-arms the modifier from the clobbered value and mis-modifies the resumed instruction
    (Context_Switch.md §13).
  - **The stack** (the shared "r15 is not banked" fact above): `intrgate` calls `extintr()` with no
    frame step, so it must do the r15 switch explicitly, gated on `СПСВ & 014 == 0`.

  *Implemented (superseding the flat-subset sketch this paragraph used to hold):* instead of extending
  a scattered `sr`/`srmr`/`sc`/`s15` block, the gate spills the five pipeline-unreachable registers to
  `save_a`/`save_r`/`save_y`/`save_m16`/`save_m15`, switches M15 to `[ustkbase]` **only when
  `СПСВ & 014 == 0`** (`ita 027`, `aax #(014)`, `u1a` past the `15 vtm`), then runs the FULSAV
  `xts`/`its` fill into the frame at M15 (M1–M14 and IRET/СПСВ read live). On exit the inverse
  `stx`/`sti` pop reloads the whole frame and re-stashes old M15/Y/R/A to the cells, applied last in
  the **forced order Y → A → R** (`xta save_y`/`aex`, then `ati 15`, `xta save_a`, `xtr save_r`), then
  `3 ij`. The order is not negotiable (Context_Switch.md §7); `выпр` reloads neither the index
  registers nor R/Y, so the gate must.
- **Done when** (done-when *c*): a from-user test (built on the new forge-entry crt0 above) takes an
  external interrupt with ω-mode / a `utc` armed and r15 holding a user-stack value, runs `extintr()`
  on the kernel stack (not on the user's r15) and resumes the user with R, Y, M[16] **and r15**
  intact. Touches `besm6.S` + the new test only — no `reg.h`/`trap.c` dependency, so it can land first.

**15b. `reg.h`: the BESM-6 trap frame, and retarget the C readers.** `reg.h` is still
`struct trap{eax…ss}` with EAX/EIP/ESP/EFL. Define the BESM-6 `u_ar0[]` frame (ACC, r1–r15,
C = M[16], R, Y, ГРП, СПСВ, ERET/IRET, errno). The frame lives on the kernel stack, so `u_ar0`
points at it in place — the x86 "changed registers are copied back on return" convention becomes "the
asm epilogue reloads the registers from the frame." Update every reader to keep the tree building —
`trap.c` (return values, panic dump), `sig.c` (ptrace / single-step, `u_ar0[EFL]`, `u_ar0[EIP]`),
`sys1.c` (exec register setup; the fork `u_ar0[EIP]+=2` trick → returning distinct ACC values, task
17), `machdep.c` (`sendsig` stack build), `clock.c`, plus `regloc[]`. EFL has no BESM-6 analogue —
single-step is the address-break registers (М034/М035) and a syscall error is errno-in-r14, not a
carry flag — so that logic is rewritten, not remapped. Not yet exercised at runtime: the gates that
fill the frame are 15c/15d.
- **Done when** the tree builds (`cd kernel && make`, image still below `076000`) with the new frame,
  and every reader compiles against it.
- **Done.** `include/sys/reg.h` follows Dubna's canonical register-save block (Context_Switch.md
  §2) with **one deliberate departure — the two return slots are collapsed into one**: `ACC 0`,
  `RREG 1` (R), `RMR 2`, `RET 3`, `SPSW 4`, `CREG 5` (= М16 = M[16]), then the register file
  **descending** — `R15 6`, `R14 7` … `R1 20` (М15…М1) — plus `NREGFRAME 21`. Dubna keeps IRET
  (interrupt/fault `выпр`/`3 ij`) and ERET (extracode `2 ij`) as separate slots, but a frame is
  filled by exactly one gate, so only one return address is ever live in it — the single `RET` slot
  holds whichever applies, and the gate that built the frame picks the matching `ij`. `errno` is
  `r14` (М14), no own slot. **ГРП is not framed** — the fault cause is read live via
  `__besm6_mod(MOD_GRP,0)` in `trap()` (15c), as Dubna does. All indices non-negative, `u_ar0 =
  &tr.acc` at word 0; `struct trap` stays **by value** into `trap()`/`clock()` (b6cc took the width
  fine) — **superseded by 15c: `trap()` takes `struct trap *`**, because `b$ret` eats the last
  parameter word (= `frame[20]`) of a by-value struct; see 15c for the derivation. Aliases: `R_ERRNO R14` (0 = success, no carry bit), provisional `R_VAL2 R13` (15d
  finalizes); `USERMODE(spsw)=((spsw)&014)==0`; `BASEPRI(x)=(0)` (15e placeholder). `regloc[]` is
  `{ACC, R1..R14, R15, RET}` (17 entries): setregs zeroes `[0..14]` (`sys1.c` bound `&regloc[15]`),
  procxmt validates `[0..15]` (`sig.c` bound `i<16`). The collapse resolves the old
  IRET-vs-ERET resume-slot ambiguity: all readers just use `RET`. Deferred
  behavior compile-stubbed with `TODO 15c` (ГРП dispatch, breakpoint single-step), `TODO 15d`
  (syscall ABI + r_val2 slot), `TODO 15e` (BASEPRI/clock), `TODO 17` (sendsig up-growing stack,
  fork `+=2`→distinct ACC, ptrace resume-slot, single-step via М034/М035). Image top `060106`
  (bss), well under `076000`; no `GRP`/`RETPC`/x86 name survives in kernel C.

**15c. The trap gate (0500) and the fault path in `trap.c`.** The vector `uj trap` resolves straight
to the C `trap()` with no frame under it. Interpose an asm stub that — after the r15 switch, when the
fault came from user — saves the frame onto the kernel stack at `076000`, points `u.u_ar0` at it, calls
`trap()`, restores, and returns via IRET (`3 ij`) — with the **restart protocol** (back the PC up from
`SPSW_NEXT_RK`/`SPSW_RIGHT_INSTR` before retrying a faulted instruction). Retarget `trap.c` from x86:
`trap(struct trap)` by value → the BESM-6 frame; dispatch on ГРП (bit 20 = data protection, faulting
page in bits 5–9 → `grow()` or SIGSEG; bit 14 = instruction protection; bit 13 = illegal instruction;
bit 15 = check) instead of x86 vector numbers.
- **Done when** (done-when *b*): a from-user test's data-protection fault (touching a closed page) grows
  the stack or signals.
- **Done.** `0500` now vectors at `trapgate` (`besm6.S`), which is `intrgate`'s prologue and FULSAV fill
  instruction for instruction, calls `trap(struct trap *)`, and leaves through **`intrgate`'s epilogue,
  reused as-is** — the restore block carries the label `intret` and `trapgate` ends `uj intret`. How it
  turned out:
  - **`trap()` takes a POINTER, superseding 15b's "struct trap stays by value"** (annotated there).
    Passing the 21-word frame by value looks free — the FULSAV fill already builds exactly the argument
    block the ABI wants — but the ABI says otherwise, and it was checked rather than assumed
    (`b6cc -S` on a 21-word by-value call, plus `c-compiler/libc/besm6/unix/b_{save,ret}.s`): `b$save`
    does *not* copy a by-value struct, it points r6 at the caller's block — so `u_ar0` would alias in
    place either way — but **`b$ret` reuses the LAST parameter word as its return-value scratch**
    (`7 stx -5`), which for a 21-word struct is `frame[20]` = М1, and it returns r15 at the block base,
    not above it. With one pointer argument that scratch word is the dead slot at `F+21` and r15 comes
    back at `F+21` — exactly `intret`'s precondition, so the epilogue needs no fixing up and the gate
    is a straight `uj`. `clock()` (15e) should take the same shape.
  - **The stack switch is unconditional** — no `СПСВ & 014` discriminator. A fault from supervisor is a
    kernel bug (`useracc` validates up front; there is no `nofault` path), `trap()` takes it to the
    `default` arm and panics, and resetting r15 under a panic costs nothing. So this door never nests.
  - **One set of temp cells still serves both gates**, and it must: `intret` re-applies them. It is safe
    because a cell is live only across a prologue/epilogue, and both run with БлПр forced on and the
    kernel unmapped — no interrupt and no fault can land in that window, so the gates cannot overlap.
  - **The restart protocol is two lines, not eight, and the machine corrected the plan.** The saved
    `M[IRET]` is the faulting **word plus one** in *both* cases, and `SPSW_RIGHT_INSTR` already names the
    half that faulted (`выпр` reloads the indicator from it) — so the fixup is `tr->ret--` plus clearing
    `SPSW_NEXT_RK`, and nothing else. The first draft "reconstructed" the half-word too and skipped every
    faulting instruction; `utrap` caught it (ACC `0600`). `doc/Memory_Mapping.md`'s restart-protocol
    section now carries the derivation and the verified recipe. It lives in C, at the top of `trap()`,
    because the frame is aliased in place.
  - **`trap.c` dispatches on ГРП** read live (`__besm6_mod(MOD_GRP,0)`), folded into kernel-local trap
    kinds (`T_DATA`/`T_INSN`/`T_ILL`/`T_CHECK`/`T_BREAK`, plus `T_SYSCALL` left untouched for 15d) and
    dismissed with `MOD_GRPCLR` so a fault bit cannot fire afterwards as a spurious external interrupt.
    The x86-only arms are gone; the panic dump gained ГРП and the faulting page. `GRP_OPRND_PROT`,
    `GRP_INSN_PROT`, `GRP_ILL_INSN`, `GRP_INSN_CHECK`, `GRP_BREAKPOINT`, `GRP_PAGE_MASK` are new in
    `sys/besm6dev.h`. `grow()` still takes a word address and still assumes the x86's downward stack, so
    the faulting page is converted back to an address (`grow(page << PGSH)`) — flagged `TODO 17`.
  - **`kernel/test/utrap`** (`crt0t.S` + `utrap.c` + `utrap.ini`) is `uintr`'s counterpart on the fault
    door: it forges user mode, leaves virtual pages 4/5/6 closed and reads them in turn, faulting **once
    from a right half and once from a left** so both arms of the fixup are exercised. `trap()` there
    checks the *frame* (R, Y, М16, r15) rather than having the user read its own registers back — which
    is the point of the aliasing pointer — clobbers all three, and opens the faulted page; the retried
    `xta` must come back with the sentinel behind it. Needs **`set mmu cache`** (unlike `uintr`): the
    stub reprograms РП on every fault and the user's reports are mapped stores read back physically.
    **Bite-tested:** dropping the PC fixup yields ACC `0600`, dropping the stack switch `020`, dropping
    `xtr save_r` from the shared epilogue `2`.
  - Image top `060126` (bss), under `076000`; the full `kernel/test` suite is green.

**15d. The syscall gate (0577) and `badext`.** `syscall` and `badext` are `stop` stubs. Build the
extracode frame, dispatch, and return via ERET (`2 ij`, or normalise ERET→IRET the `OUTMACRO` way —
but note the fault path needs the PC fixup the extracode path does not). Rewrite `trap.c`'s inline
syscall path (`case 48+USER`) to the BESM-6 ABI, mirroring `cmd/sim/syscall.cpp`: number from the
`$77 N` operand (the effective address, in r14), last arg in ACC and the rest below r15, **result in
ACC and errno in r14** — not the x86 EAX/esp/carry convention. Because the args sit below the caller's
r15, read them (or copy the frame) *before* the r15 switch, then repoint r15 at the kernel stack to run
the C dispatcher — an extracode always comes from user mode, so the switch is unconditional here.
- **Done when** (done-when *a*): a from-user test issues `$77 N` and returns to user mode with the
  correct ACC/errno.

**15e. Unblock the timer (`clock` / GRP_TIMER).** With the frame in place: retarget `clock()` off
`struct trap` by value onto the BESM-6 frame, add `GRP_TIMER` to `IRQ_ON` and dispatch it from
`extintr()` (`intr.c`), and remove the standing TODO at `intr.c:144–148`. **Leans on task 16:**
reschedule-on-return (`runrun`) and any fault that sleeps need `save()`/`resume()`; the exit path
checks `runrun`, but the switch itself is task 16.
- **Done when** a timer tick reaches `clock()` and a callout fires under SIMH.

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
