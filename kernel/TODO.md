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

**`besm6.o` cannot go into a standalone test** — its `0500` vector reaches into the C kernel and its
`_start` seeds no stack. That is why every routine a test has to link lives in its own file
(`brz.s`, `uarea.s`, `seg.s`, `usermem.s`, `syscall.c`) and why the gates are duplicated in the
tests' own crt0s.

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
own constant pool with it. The extracode vectors now point at `sysgate` (`0577`, э77) and `badext`
(`0550`–`0576`); an extracode never reaches `trap()`.

Two things stage 1 learned, that the tasks below depend on:

- **`drainbrz()` cannot be written in C** — hence `brz.s`, the one routine in the kernel that has to be
  assembly. The nine stores to physical 1–7 must be consecutive, and `b6cc` materializes the
  destination pointer through a frame slot, so each C store emits two ordinary stores of its own and
  resets the flush counter.
- **The drain is load-bearing, and `test/mmutest` proves it.** Remove the `drainbrz()` calls and it
  fails under `set mmu cache` — the store sits dirty in a БРЗ line tagged with the *virtual* address,
  and a physical read carries a different tag and sees stale memory — and **passes anyway with the
  cache off**. Every task below that reloads РП or reads a process's image out of memory faces exactly
  that hazard.

### Stage 2 — the brackets. DONE

Four groups of routines, each in its own file rather than in `besm6.S` so `test/mmutest` can link the
real thing, and each verified by `mmutest` under `set mmu cache`.

**10. `uflush()` / `uload()` — `kernel/uarea.s`.** Windows the process's u home and the live u-area
(physical page 31) into virtual pages 1 and 2 — one `mod 020` to steal the quartet, one to put it
back — and drains the БРЗ on both sides of the copy. The contract **task 16 depends on**: `uflush`
only reads the live page and is safe to call from C, but **`uload` overwrites the kernel stack its
caller is standing on**, so only `resume()` — assembly, keeping its state in registers — may call it.
Both are map-neutral, and both hold БлПр across the copy.
**Still to do, as planned:** copy only up to the saved r15 (`struct user` plus the live stack,
typically ~300 of the 1024 words).

**11. `copyseg()` / `clearseg()` — `kernel/seg.s`.** The same bracket shape as task 10, over the same
two window pages. They take **page-aligned word addresses**, not click numbers, and every loop that
calls them steps by `PGSZ`.

**12. `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword` — `kernel/usermem.s`.** No window: the
user's map is already in РП, so the loop toggles БлП per word — read the user word mapped, store it
to the kernel buffer unmapped. Three facts that reach forward:

- **`copyin`/`copyout` are word-only**, exactly like the x86 original — they copy `nbytes / NBPW`
  whole words. Every caller passes a word-aligned address and a word-multiple count; an unaligned
  copy stays on the `fubyte`/`subyte` byte path in `iomove`.
- **Validation is `useracc()`, called from the routine itself**, and a range that runs into a zero
  descriptor returns the clean C `-1`. **There is no `nofault` path** — which is why a
  supervisor-mode fault is a kernel bug, and why `trapgate` (15c) may switch the stack
  unconditionally.
- **`fubyte`'s fat-pointer marker bit (48) is load-bearing**: build a `char *` by hand from an `int`
  and its `asx` shifts by `8·off − 64` and returns 0. Use the compiler's `int*`→`char*` conversion,
  which is the real path.

**13. `wcopy` / `wzero` / `cli` / `sti` — `besm6.S`.** The block copies are named `wcopy`/`wzero` and
**take a word count** (`btow()` at every call site; `BSIZEW` = 512 for whole blocks), so the loop is
pure words with no six-chars-per-word tail — plain unmapped, register-only, no window and no drain.
`DIRSIZ` is now 24, so `struct direct` is 5 words and directory entries are word-aligned. `cli`/`sti`
are a **read-modify-write** of the БлПр bit of ПСВ (`ita 021` / `aox 02000` or `aax 075777` /
`ati 021`), *not* a `vtm`, which writes the whole ПСВ and would clobber БлП/БлЗ/ПОП/ПОК. The seven
x86 port-I/O routines are deleted; `dev/hd.c` and `dev/sr.c` survive as BESM-6 **driver skeletons**
(port I/O stripped to `// TODO`, still wired in `conf.c` so their `bdevsw`/`cdevsw` hooks resolve),
and `machdep.c` lost the 8253 PIT and the CMOS RTC — so **time-of-day is not read at boot**
(`clkstart()` is now just `spl0()`).

### Stage 3 — boot, traps, switching

**14. `_start` and boot. DONE.** `_start` seeds the kernel stack pointer (r15) and calls `main()`;
the machine resets straight into the kernel's own mode — supervisor, unmapped, interrupts off
(`doc/Memory_Mapping.md`, "Reset state") — so there is nothing else to set up. bss-zero and memory
sizing live at the top of `main()`, in C rather than in `_start`, because the bss size is a
difference of two linker externals and `b6as` rejects that: `wzero(edata, end - edata)`, guarded out
under `#ifdef ON_SIMH` since SIMH starts every word at zero, then `phymem = 512 * 1024` (the kernel
runs unmapped, 32 Kwords of reach, and cannot probe the 512 Kword store). `uhome` is set in `main.c`
right after `proc[0].p_addr`. r15 comes from `machdep.c`'s `int *const ustkbase = &u.u_stack[0]`
(≈ `076214`). `make run` + `kernel/unix.ini` boot the image under SIMH.

**Left open, and not a boot defect:** the *number* in `startup()`'s `mem = …` does not render on the
console. `printf`/`printn` are proven correct — the same source prints `mem = 490496 words` under
`b6sim` — and were reworked onto `<stdarg.h>`, so this is a polled-console/driver problem:
characters are dropped after `mem = `, and `putchar`'s `msgbuf` logging was found not to populate
either. To debug when the console driver is next touched.

**15. Trap / extracode / interrupt entry and exit (`besm6.S`, `include/sys/reg.h`, `trap.c`).** There
are **three doors with two save disciplines**, and the async ones must preserve more of the machine
than the ABI's callee-saved set. No door needs a map switch or a БРЗ drain — the hardware's forced
БлП/БлЗ is already the kernel's mode. Read
[../doc/Context_Switch.md](../doc/Context_Switch.md) first; §4/§7/§8/§9/§13/§14 are the authority.
**15a–15d are done; 15e remains.** Four facts cut across them:

- **Async vs. synchronous.** An external interrupt (`0501`→`intrgate`) and an internal fault
  (`0500`→`trapgate`) land between arbitrary instructions, so the interrupted code owns *every*
  register and the gate must save the full visible machine. An extracode (`0577`→`syscall`,
  `0550`–`0576`→`badext`) is a synchronous *call*: the caller owns its live registers, so the gate
  saves almost nothing (§9), and the hardware has already clobbered r14 (= the effective address).

- **The stack switch — r15 is not banked.** Every door entered from user mode must repoint r15 at the
  kernel stack by hand, and every door that nests inside the kernel must leave it alone. There is one
  kernel stack, so the switch is the constant load `15 vtm [ustkbase]` that `_start` already uses.
  The signal is **`СПСВ & 014`** (РежЭ | РежПр), zero **iff the interrupted context was user**. Test
  *that*, not БлП: `copyin`/`copyout` run in supervisor mode with БлП clear, so a БлП test would
  misread a fault taken mid-`copyin` as "from user" and reset r15 out from under the syscall frame.
  Why it bites: the trap forces БлП *on*, so a user r15 (exec seeds it at `070000`, growing up —
  task 17) is an **unmapped physical** word index pointing into the kernel image below the u-area.

- **One exit, three doors.** An extracode returns via ERET (`2 ij`), an interrupt/fault via IRET
  (`3 ij`), and the two cannot share a hardcoded `выпр` unless the door is normalised first. Steal
  Dubna's `OUTMACRO` (§8): copy ERET→IRET in two instructions and let one `3 ij` serve both. The
  syscall return then inherits the interrupt epilogue's `runrun`/pending checks for free — but note
  the fault path needs a PC fixup the extracode path does not.

- **Only one saved-state slot.** СПСВ, IRET and ERET are single registers, not a stack: a second
  internal fault while handling the first is fatal (`STOP_DOUBLE_INTR`). A gate must save СПСВ/IRET
  to the kernel stack before it does anything that can fault or re-enables interrupts.

**15a. `intrgate` (0501). DONE**, and `kernel/test/uintr` takes an external interrupt in forged user
mode and confirms R, Y (РМР), M[16] and r15 all survive it on the real machine. What 15d reuses:

- **The FULSAV fill.** The frame is built with the Dubna `xts`/`its` store-and-load pipeline (§6),
  whose only scratch is the accumulator, so M1–M14 and IRET/СПСВ are read **live** — the fill runs
  before the C call can clobber anything. Only the five registers the pipeline cannot reach are
  spilled to temp cells first: `save_a`, `save_r`, `save_y`, `save_c` (M[16], which must be spilled
  *before* the stack-switch `vtm`, whose `utc` would clobber it) and `save_sp` (the pipeline's own
  M15). The restore is the exact inverse pop, ending in the **forced order Y → A → R** (§7) — `выпр`
  reloads neither the index registers nor R/Y, so the gate must. The gate does **not** store
  `u.u_ar0`; the C handler does, since doing it in asm would need a hardcoded `u_ar0` offset.
- **The five cells are shared with `trapgate`, and are addressed BARE, not `< sym >`-escaped.** They
  link under the 12-bit short-address field, and an escape emits a `мода`/`utc` that loads M[16] —
  which would overwrite the very register the gate is saving. Sharing is safe because a cell is live
  only across a prologue/epilogue, both of which run with БлПр forced on, so the gates cannot
  overlap.
- **The test harness (`crt0u.S` + `uintr.c`), which 15c reused and 15d should.** `gouser()` forges a
  Dubna `SELECT`-style entry — plant IRET + СПСВ (with `SPSW_MOD_RK` so `выпр` re-arms the
  modifier), set R via `ntr`, Y via the `xta`/`aex` side effect, r15 to a user-stack value — and
  `3 ij` into a tiny mapped user program at virtual page 0. Its map comes from `sureg()` over
  `uprog`'s own physical page, taken from a linker-filled `.word uprog` (**not** `(unsigned)&uprog`,
  whose fat pointer is not simply its low bits). The interrupt is raised in software with `увв 031`
  on `GRP_TIMER`, so there is no device and no timing. Read-back values live in memory, not index
  registers — those are 15 bits and would truncate. The user reports back through a **deliberate
  data-protection fault**, not `стоп`: `стоп` in user mode re-dispatches as extracode э63 and
  check-halts under the reset ПоК, while a data fault ignores ПоП/ПоК and always vectors.
- One correction it forced, since the old comment survives elsewhere: "the arithmetic mode rides in
  СПСВ across a trap" is **false**. The hardware never saves R — SIMH's `op_int_1` and `выпр` both
  leave RAU alone — so software must, or the resumed user runs in the wrong arithmetic mode.

**15b. `reg.h`: the BESM-6 trap frame. DONE.** The layout every gate and C reader now shares:
`ACC 0`, `RREG 1` (R), `RMR 2` (Y), `RET 3`, `SPSW 4`, `CREG 5` (М16), then the register file
**descending** — `R15 6`, `R14 7` … `R1 20` — plus `NREGFRAME 21`. Dubna keeps IRET and ERET as
separate slots; here they are **collapsed into one `RET`**, because a frame is filled by exactly one
gate, so only one return address is ever live in it and the gate that built it picks the matching
`ij`. `errno` is r14, with no slot of its own; **ГРП is not framed** — the fault cause is read live
via `__besm6_mod(MOD_GRP,0)`, as Dubna does. EFL has no analogue: single-step is the address-break
registers (М034/М035) and a syscall error is errno-in-r14, not a carry flag. Aliases: `R_ERRNO R14`
(0 = success), `R_VAL2` (15d settled it on **r12**, not the provisional r13 — r13 is the
caller's return address), `USERMODE(spsw)`, and `BASEPRI(x)=(0)` as a
15e placeholder. `regloc[]` is `{ACC, R1..R14, R15, RET}` (17 entries): `setregs` zeroes `[0..14]`,
`procxmt` validates `[0..15]`. Deferred behavior is compile-stubbed in place and marked `TODO 15e` (BASEPRI/clock) and `TODO 17` (sendsig up-growing stack, fork
`+=2`→distinct ACC, ptrace resume-slot, single-step via М034/М035).

**15c. The trap gate (0500) and the fault path in `trap.c`. DONE.** `0500` vectors at `trapgate`
(`besm6.S`), which is `intrgate`'s prologue, FULSAV fill and call instruction for instruction, and
leaves through **`intrgate`'s epilogue reused as-is** — the restore block carries the label `intret`
and `trapgate` ends `uj intret`. What the remaining tasks need from it:

- **The stack switch is unconditional, so this door never nests** and the frame is always at
  `[ustkbase]`. A fault from supervisor is a kernel bug (`useracc` validates up front; there is no
  `nofault` path), so `trap()` takes it to the `default` arm and panics, and resetting r15 under a
  panic costs nothing.
- **Nothing is passed to `trap()`; it takes `void`** and opens with
  `register struct trap *tr = (struct trap *)u.u_stack`, the frame base being a link-time constant
  rather than something the gate discovers. The ABI makes the call free: a callee with **no
  parameters returns r15 unchanged** (`doc/Besm6_Calling_Conventions.md`, "On Return"), and the fill
  left r15 at `F+21`, which is exactly `intret`'s precondition — so the gate is `13 vjm trap` with no
  argument setup and a straight `uj` out. **`clock()` could not copy this** — `intrgate` *does* nest,
  so its frame is at the stack base only when the interrupt came from user — which is why 15e gave
  that gate a published `intrframe` cell instead.
- **The restart protocol is two lines, and lives in C** at the top of `trap()`, because the frame is
  aliased in place. The saved `M[IRET]` is the faulting **word plus one** in *both* cases, and
  `SPSW_RIGHT_INSTR` already names the half that faulted (`выпр` reloads the indicator from it), so
  the fixup is `tr->ret--` plus clearing `SPSW_NEXT_RK`, and nothing else. A first draft
  "reconstructed" the half-word too and skipped every faulting instruction; `doc/Memory_Mapping.md`
  now carries the derivation and the verified recipe.
- **`trap.c` dispatches on ГРП** read live, folded into kernel-local trap kinds
  (`T_DATA`/`T_INSN`/`T_ILL`/`T_CHECK`/`T_BREAK`; `T_SYSCALL` is gone -- 15d gave the extracode
  door its own C file) and
  dismissed with `MOD_GRPCLR` so a fault bit cannot fire afterwards as a spurious external interrupt.
  `grow()` still takes a word address and still assumes the x86's downward stack, so the faulting
  page is converted back (`grow(page << PGSH)`) — flagged `TODO 17`.
- **`kernel/test/utrap`** (`crt0t.S` + `utrap.c`) is `uintr`'s counterpart on the fault door: it
  forges user mode, leaves virtual pages 4/5/6 closed and reads them in turn, faulting **once from a
  right half and once from a left** so both arms of the PC fixup are exercised, and checks the
  *frame* rather than having the user read its own registers back. Needs **`set mmu cache`** (unlike
  `uintr`): the handler reprograms РП on every fault. Bite-tested — dropping the PC fixup yields ACC
  `0600`, dropping the stack switch `020`, dropping `xtr save_r` from the shared epilogue `2`.

**15d. The syscall gate (0577), `badext` and the dispatcher. DONE.** The gate turned out even
smaller than planned, and one hardware fact fell out of the test that reaches past this task.

- **`sysgate` is `trapgate` with ONE instruction changed:** the FULSAV fill reads `its ERET`
  (М032) where the fault gate reads `its IRET` (М033). The planned Dubna `OUTMACRO` normalisation
  at entry (`ita ERET` / `ati IRET`) is **not needed at all** — the fill already reads a return
  register live, so it just reads the other one, and `intret` is reused completely unmodified: its
  `sti IRET` + `3 ij` return to user correctly because the `ij` index field only selects *which
  register holds the PC*, while the mode comes from СПСВ either way. Bite-tested: revert that one
  instruction and the return lands nowhere.
- **`badext` carries its OWN prologue**, a third full copy of the block, rather than sharing
  `sysgate`'s body through a discriminator cell. Nothing before the frame is filled can tell the
  doors apart — the hardware identifies an extracode purely by which vector word it lands on — so
  sharing would cost a flag and a branch to save a block that is otherwise identical. Same call
  the file already makes for `trapgate` vs. `intrgate`.
- **The dispatcher is `kernel/syscall.c`, not `trap.c`** (holding `syscall()` and `badextr()`).
  The reason is the one that split out `brz.s`/`uarea.s`/`seg.s`/`usermem.s`: `kernel/test/usys`
  links the **real** dispatcher, and it cannot link `trap.c`, which drags in `printf`, the signal
  machinery and `grow()` — the whole kernel. `nosys`/`nullsys`/`stray` stayed in `trap.c`, and
  `T_SYSCALL` is gone from it entirely.
- **`r_val2` is r12, not the provisional r13.** r13 is the ABI's return-address register and
  belongs to the caller; r12 is ordinary caller-saved scratch, the same class as the accumulator.
  `R_VAL2` in `reg.h` is settled, and `usys` checks that r13 comes back untouched.
- **The stack pop happens BEFORE the dispatch**, not after: `exec()` reseeds r15 on success and
  `sendsig()` builds its frame on the user stack, so both want the popped value and a pop
  afterwards would corrupt the first and come too late for the second. The arguments are already
  copied out by then, so nothing reads the user stack at the old depth.
- The number is range-checked against `NSYSENT` (now in `systm.h`, beside the table) into a
  file-static `badsysent = {0,0,nosys}` — masking would fold an out-of-range number onto a real
  syscall. `sy_nrarg` is left in place but read nowhere, and documented as vestigial: exactly one
  argument arrives in a register on this machine, for any `narg >= 1`.
- Fork's `u.u_ar0[RET] += 2` is deleted, and **`u_r.r_val2` now tells parent from child** (1 / 0),
  which is v7's own answer. `r_val1` alone could not: each side gets the *other's* pid, which is
  distinct but not self-identifying. `cmd/sim` matches.
- *`cmd/sim`* grew `sys_ok2(v1, v2)` (accumulator + r12) and moved `pipe`, `wait`, `fork`,
  `getpid`, `getuid`, `getgid` onto it; `pipe` and `wait` therefore take **no arguments** now.
  Note r12 is an index register — **15 bits** — so a second result above 32767 is truncated;
  nothing a v7 guest produces comes close, but a *host* pid does, which the `Getpid` test masks
  for.

**Two things the test found that were not in the plan:**

- **An extracode always returns to the LEFT half of the next word.** ERET is a **word** address
  (`PC + 1`) and the extracode entry saves **no** right-instruction indicator, so that is where
  `выпр` lands whichever half the extracode itself was in — exactly as `vjm` behaves. The
  extracode may sit in either half; what it costs is what comes *after* it in the same word. From
  a left half, the instruction packed beside it is never executed; from a right half nothing is
  lost, which is why putting it there is the simple convention rather than a hardware rule. No
  gate can repair this — the half is not recorded anywhere for the gate to find. It is a
  constraint on the *caller*, now written up in `doc/Context_Switch.md` §9, and it means stubs
  shaped like `cmd/sim/tmp/putch.s` (`$77 4` in a left half, then `13 uj`) fall through under the
  kernel. **`b6sim` does not model it** — it services `$77` inline and continues to the next
  half-instruction — so that is a live divergence to close (`doc/Aout_Simulator.md` §3), along
  with auditing every existing `$77` stub for what follows it.
- **A simulator bug, fixed upstream in `simh-besm6`.** Its a.out loader tagged const-segment words
  as instructions only in `0500..0547`, so the extracode vectors at `0550`–`0577` loaded as *data*
  and fetching one raised «контроль команды». `BESM6/besm6_sys.c` now covers `0500..0577`. Anyone
  building this kernel needs a simulator with that fix.

*The test — `kernel/test/usys`* (`crt0s.S` + `usys.c` + `usys.ini`), four legs from forged user
mode: a 0-argument call (the two-value return, and r13/r15 untouched), a 3-argument call staged
the real way (prototype order, and r15 popped by 2), an out-of-range number (`-1` + EINVAL from
`nosys`), and `$50` (SIGINS from `badextr`, machine intact). It exits through a data fault as
`utrap` does. Needs **`set mmu cache`**. Bite-tested three ways: reverting `its ERET`, dropping
the stack pop, and masking instead of range-checking each fail it.

**Still open, and deliberately not touched:** `u.u_dirp = (caddr_t)u.u_arg[0]` is an `int`→`char *`
conversion, and a `char *` built by hand from an `int` gets the fat-pointer marker wrong (see
`usermem.s`). It is pre-existing, affects every path-taking syscall, and belongs with `iomove()`'s
byte handling rather than with marshalling.

**15e. Unblock the timer (`clock` / GRP_TIMER). DONE.** A tick now reaches `clock()` from user
mode and a callout fires under SIMH — `kernel/test/uclock`. `clock()` takes `struct trap *`;
`GRP_TIMER` is in `IRQ_ON` and dispatched from `extintr()`; the standing TODO at `intr.c:144-148` is
gone.

**The frame is *published*, not found — and not through `u.u_ar0`.** The sketch had `clock(void)`
locating the frame the way `trap()` does, guarded by `USERMODE(spsw)`. That cannot work: `spsw`
*is* in the frame, so the guard needs what it is trying to find, and on a tick nested inside a
syscall `u.u_stack` holds the **syscall's** frame — whose СПСВ says *user*, so the guard passes and
`clock()` charges `u_utime` and profiles at a stale user PC. `intrgate`'s stack switch is
conditional, so its frame base is a run-time value (`[ustkbase]` from user, the interrupted r15 when
nested), and the gate now stores it in a private cell:
```
extk:   ita     15                  // A := F -- the `aax' result it overwrites is dead here
        atx     intrframe           // bare-addressed: no escape, so M16 is untouched
```
Two instructions, no argument to build, so the call stays the bare `13 vjm extintr` and `intret`'s
`r15 = F+21` precondition is untouched. `trapgate` and `sysgate` need none of this and keep the
link-time constant.

**`u.u_ar0` was tried first and is wrong**, which is worth recording because it is the obvious
choice — v7's own name for the current register frame, and what `trap()`/`syscall()` set. The store
has to be unconditional (from the kernel too, or the nested case reads the wrong frame), so it
transiently overwrites the `u_ar0` of an interrupted syscall — and `exec()` (`sys1.c:269-271`) and
`sendsig()` (`machdep.c:124-128`) write the resumed PC and the signal frame *through* `u_ar0`, from
paths that sleep. A tick between the assignment and the use sends those writes into a frame that is
dead by the time anything resumes. Banking the old value works but **cannot be done in C** — the
gate has already overwritten the cell by the time `extintr()` runs, which `uclock` caught on the
first run — so it costs two more instructions in the gate and two after the call. And `u_ar0` means
the *user's* registers; a tick nested in the kernel frames a *kernel* context. A private cell has
neither problem, and `uclock` now asserts `u.u_ar0` came back untouched.

**`clock()` leaks the ipl, and `extintr()` had to take that over.** Not in the plan, and it would
have bricked the machine on the first tick. `clock()` calls `spl5()` and `spl1()` and restores
neither: on the PDP-11 `rtt` reloads the priority field of PS from the frame, but here `выпр`
restores **БлПр** and МГРП is a separate write-only register outside the mode word. So the first
tick would have left `mgrp == 0` — every source masked for good — and `extintr()`'s own
`grp & mgrp` loop condition would have abandoned a co-pending `GRP_SLAVE` undismissed. `extintr()`
now latches `curipl` on entry and `splx()`es at the **top** of the loop, so the level is repaired
before the next ГРП read rather than after the loop.

**Smaller things that turned out settled rather than open:**
- **`BASEPRI(x)` stays `(0)`, permanently**, and the TODO in `reg.h` became the reason. This machine
  has two levels, not eight; delivery needs БлПр clear *and* `ГРП & МГРП != 0`; `setipl()` leaves
  МГРП non-zero only at spl0. So anything `clock()` interrupts was at base priority by construction.
  (The macro's v7 name reads backwards — *true* means "was **above** base, skip the callouts" — which
  the comment now says, because the next reader will otherwise invert it.)
- **`HZ` is 250**, was 60. The interval timer free-runs at that rate (`CLK_TPS`, "as per the original
  documentation") and cannot be programmed, so 60 would have run `time` 4.17× fast and every
  `timeout()` 4.17× short. Two knock-ons to keep in mind: v7 userland that hardcodes 60 (`/bin/time`,
  `ps`) will misreport, and the raw tick counts in `dev/sr.c:122` / `dev/sc.c:145` are now 4× shorter
  pad delays — harmless for a simulated Consul.
- **`clkstart()` has nothing to program.** The timer free-runs from reset on the hardware and in
  SIMH, so it dismisses the tick that accumulated during boot and calls `spl0()`. `time` is still
  never seeded from a wall clock — the x86 CMOS RTC path is gone and this machine has no
  clock-calendar a program can read — so the epoch starts at 0.
- `addupc()` is still a stub and `waitloc` still 0, so profiling and idle-time accounting are inert;
  they belong to tasks 17 and 16.

*The test — `kernel/test/uclock`* (`crt0c.S` + `uclock.c` + `uclock.ini`), the fourth forge test and
the second (after `usys`) to link the code under test rather than a copy: the real `intr.o` and
`clock.o`, with only their environment hand-built. `увв 031` raises `GRP_TIMER` while БлПр is still
set, so the tick fires at `uprog`'s first user instruction — deterministic, no device, no timing.
`timeout(fn, MAGIC, 1)` and `lbolt = HZ - 1` make *one* tick produce both a callout and a
second-rollover. Fifteen checks, of which the sharp ones are `u_stime == 0` (the frame really was
the interrupted user context), `mgrp == IRQ_ON` (the ipl came back) and `intrframe == ustkbase` with
`u_ar0 == 0` (the frame went to its own cell). Bite-tested three ways: dropping the publish, dropping
the `splx(s)`, and taking `GRP_TIMER` out of `IRQ_ON` each fail it.

Two notes for whoever writes the next one of these. **The interval timer cannot be switched off** —
it free-runs at 250 Hz and the SIMH `CLK` device has no `DEV_DISABLE`, so no `.ini` can stop it and a
second tick may land mid-run; every assertion here is phrased to tolerate exactly one, and a first
draft's `p_cpu >= 1` check was **passing only because** a second tick happened to arrive after the
aging code zeroed it. And `crt0c.S` keeps its gate temp cells in **`.text`**, not `.bss` like
`crt0u.S`/`crt0s.S`: linking the real `clock.o`/`intr.o` (and `proc[NPROC]` with them) pushed this
image's bss past `010000`, out of reach of the bare 12-bit `atx save_a` the gate must use.

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
