# Retargeting the kernel's memory management to the BESM-6 MMU

A work plan. The memory model, the mapped brackets, boot, all three trap doors, the timer, the
context switch and the user memory layout are **done**: two processes alternate under the real
scheduler on SIMH, each seeing its own `u`, and the user stack grows on demand. What remains is
I/O — task 18 below.

Where the port stands: `cd kernel && make` links an image that boots under SIMH and reaches
`panic: iinit`. The hang after that is `panic()`'s own `for(;;) idle()`, which is now a real spin;
both drivers are now written (tasks 18b.3 and 18b.4) but no root filesystem image exists for
`iinit()` to mount, which is task 18b.6.

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
(`brz.s`, `uarea.S`, `seg.S`, `usermem.S`, `switch.s`, `psw.s`, `syscall.c`) and why the gates are
duplicated in the tests' own crt0s.

**Tasks 1–16 are done and their writeups have been removed**; the design they settled on is the
section above, and how each turned out is in the source comments and in `../doc/`. The numbering
below is **left as it was** — task numbers are cited from the source (`seg.S`, `dev/bio.c`,
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
anywhere in the machine (18a), and the drivers that actually move it (18b, itself a sequence of six
steps). 18a is a prerequisite of 18b — without `b_paddr` a transfer cannot name a page above 32767,
which is most of memory — so do them in that order.

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

**18b. The drum and disk drivers.** `dev/hd.c` is a skeleton — the x86 IDE driver with its
programmed-I/O body removed: `hdstrategy()` sets `B_ERROR` and calls `iodone()` on every request,
and `hdintr()` is empty. `rootdev`, `swapdev` and `pipedev` (`conf.c:52-54`) all name major 0, so
this one strategy routine stands between the kernel and every criterion below.

Read [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) § *Magnetic drums* and § *Magnetic
disks* first, and [../doc/Intrinsics.md](../doc/Intrinsics.md) — **there is no assembly in this
task**. `__besm6_ext(addr, acc)` *is* `033 «увв»` and `__besm6_mod(addr, acc)` *is* `002 «рег»`,
each one inline instruction, so a control word is ordinary C arithmetic and issuing it is a function
call that isn't one. `doc/Intrinsics.md` §6.3 is a drum page read written out in C and is the model
for 18b.3; §6.2 is the ГРП dispatch pattern 18b.2 builds on.

**Two drivers, not one split by minor.** The exchange control word is bit-for-bit identical on the
two devices (BLOCK 27–24, READ_SYSDATA 21, PAGE_MODE 19, READ 18, PAGE 17–13, UNIT 10–8), and that
much is shared code — but nothing above it is. `hdtab` is v7's one-request-outstanding queue head
per `bdevsw` entry, and the drum and the disk are independent channels that can transfer at the same
time; one major would serialise swap traffic behind filesystem traffic for nothing. Starting a
transfer is a single store on the drum and a three-command state machine on the disk. Completion
arrives on disjoint ГРП bits that the dispatcher has already separated. A block is one native
transfer on the disk (`DISK_HALFZONE`) and two sector transfers on the drum. And the disk has a
status register, seeks and retries where the drum has none. v7 agrees: `rk.c`/`rp.c`/`hp.c`/`rf.c`
are one driver per controller family, and the minor number selects unit and partition *within* one.

---

**18b.1. The device header and the driver split — DONE.** `dev/hd.c` is retired into **`dev/md.c`**
(disks, major 0 — root, pipedev, filesystems) and **`dev/mb.c`** (drums, major 1 — swap), both still
`B_ERROR` stubs. The `033` addresses and the four wired ГРП completion bits went into
`sys/besm6dev.h`; the exchange-control-word layout and the service-word buffers into a new
`sys/besm6disk.h`. `conf.c` has two `bdevsw` rows and two `cdevsw` rows, and `swapdev = makedev(1, 0)`.

**Verified by** a clean `make` (`060653`, was `060553` — 64 words), `b6nm -n unix` showing all ten
entry points and all four buffers pulled out of `libdev.a`, and all **11** `test/` SIMH tests still
passing (`biotest` in particular — it is the one the `d_strategy` change below touches).

How it turned out, where that differed from the sketch:
- **The header split went two ways, and the line is written into `besm6disk.h`'s comment.**
  `besm6dev.h` owns the machine's two *global* namespaces — the `033`/`002` address map and the
  ГРП/ПРП bit map — where every device competes for the same numbers. `besm6disk.h` owns one
  family's *accumulator layout*. The deciding fact: `besm6dev.h` is included by `besm6.S` and by
  eleven files under `test/`, none of which want two dozen control-word masks.
- **`d_strategy` was fixed rather than papered over.** `conf.h` declared it
  `int (*)(struct buf *)` while every driver defines it `void` and `physio()` takes
  `void (*)(struct buf *)`; `conf.c` hid the conflict behind a K&R `int hdstrategy();`. All five
  call sites (`dev/bio.c:77,99,112,138,415`) discard the result, so `conf.h` is now `void` and the
  trick is gone. This is why `test/biotest.c` changed: `recstrategy()` lost its `int`, and the
  `PHYSIO` macro lost the function-pointer cast that existed only to bridge the mismatch.
- **`EXT_IOERR`, not `EXT_DRUMERR`.** `033 4035` is the shared ОШМ trigger — it returns
  `drum_errors() | disk_errors() | mg_errors()` — not a drum register.
- **No `GRP_WIRED` composite.** The complete wired set also covers seven tape-channel bits this
  kernel cannot raise, and a mask by that name covering four of eleven is a trap. 18b.2 owns the
  mechanism and can define the mask it actually needs.
- **The shared helper takes a page number, not a word address.** `cwpage(pg)` splits nine bits
  across the two non-adjacent fields (`<< 12` and `<< 23`). There is no shared sub-page field —
  the disk's is `DISK_HALFPAGE` (512 words), the drum's is `DRUM_PARAGRAF` (256, sector mode only)
  — so a macro taking a word address would silently drop its low ten bits. See the blocker under
  18b.4.
- **`include/sys/part.h` deleted** with `hd.c`: pure x86 MBR (`PTMAGIC 0xaa55`, `struct hsc`),
  included by nothing, referenced only by a stale `kernel/Makefile` dependency line.
- **`dev/bio.c:404`'s `037 * PGSZ` swap clamp kept, comment reworded.** `test/biotest.c:227,230`
  asserts on that exact value in three places, deliberately, to force a two-transfer `swap()`.
  18b.3 moves the clamp and those assertions together.
- **The kernel is not built with `-Wall -Werror`** (`Makefile:26` is just `-I../include -DKERNEL`);
  those flags live only in the commented-out i486 validation block. Nothing here was caught by the
  compiler, and prototype changes have to be got right by reading.

**18b.2. ГРП: the free bits are wired, and МГРП has to become dynamic — DONE.** A prerequisite of
both drivers, and the one place the existing interrupt design had to give.

`intr.c` stated an invariant this task broke: *"МГРП is the SOURCE ENABLE, armed once by
`intrinit()` with `IRQ_ON` and never rewritten."* Two hardware facts made it untenable for mass
storage: the completion bits are **wired** (`MOD_GRPCLR` cannot lower them — SIMH clears
`GRP &= ACC | GRP_WIRED_BITS`), and **"free" means idle**, so such a bit stands whenever no
transfer is running. `IRQ_ON` keeps its value and changes meaning: the sources armed at *boot*,
never "every source this kernel can service". `mgrpon()`/`mgrpoff()` mirror `mprpon()`, for the
same reason (write-only whole-word register, so a driver writing it directly drops every other
device's bits), and a driver brackets one exchange with them.

**Verified by** a clean `make` (`060704`, was `060653` — 21 words), the new `test/ugrp`, and all
**12** `test/` SIMH tests passing. `uclock` needed no change: its `mgrp_seen == (GRP_SLAVE |
GRP_TIMER)` assertion still holds exactly, because nothing in that test arms a device bit — which
is the invariant worth keeping, not a check to relax.

How it turned out, where that differed from the sketch:
- **Both halves, not either/or.** The sketch offered the `mgrpon()`/`mgrpoff()` pair *or* an
  explicit wired-bit case in `extintr()`. They cover different failures. The pair is the
  mechanism the drivers use; the `extintr()` guard is what makes the done-when reachable at all,
  since a *forged* bit is by definition one nobody armed correctly, and without the guard the
  first driver to leak an arm still spins.
- **The guard is a probe, not a table.** `extintr()`'s fallback arm now clears the bit, re-reads
  ГРП, and calls `mgrpoff(bit)` if it is still standing. No `GRP_WIRED`-style constant — 18b.1
  already rejected one as a trap (it would name four of the machine's eleven wired bits). The
  probe covers all eleven *and* any level-driven source whose device nobody cleared, needs
  nothing kept in step with the hardware, and costs one extra `002 0237` read on a path where
  something has already gone wrong.
- **`setipl()` still does not write МГРП, deliberately.** Making the mask per-level again is the
  arrangement that already failed once (the gates hold БлПр from the vector, so an МГРП-only
  `spl0()` masks nothing back on); re-adding it would cost a `002 036` write on every `spl()` and
  buy nothing over two levels. Written into the design block in `intr.c`.
- **`test/ugrp` is the simplest test in the directory**, and on purpose: no gate, no user mode, no
  forged context. It blocks delivery with the real `spl7()` and calls `extintr()` as an ordinary C
  function, so every ГРП bit arrives exactly where the source says. Part 1 forges
  `GRP_DRUM1_FREE` (`увв 031`), arms it, and requires `extintr()` to *return* — and checks the bit
  is **still up in ГРП** afterwards, which is what proves the wired path was the one exercised.
  Part 2 is the contrast that makes the probe honest: an ordinary flip-flop nobody handles (bit
  30, имитация) must come **out of ГРП** and **stay armed** in МГРП. A guard that disarmed every
  unhandled source would pass part 1 and quietly deafen the kernel to any device that glitches.
- **Bite test run both ways.** With the probe removed, `ugrp` hangs: `step 50000000` expires at
  `01040`, inside `extintr()`, and the `.ini` fails on the halt PC.
- **Two comment blocks were already stale before this task, and were fixed with it.** Both
  described the pre-БлПр design where the level lived in МГРП: `besm6.S`'s "raising the priority
  is one write of the МГРП mask (002 036)" — it is `cli()`/`sti()` in `psw.s`, a read-modify-write
  of ПСВ — and `BASEPRI()` in `include/sys/reg.h`, which justified itself with "setipl() leaves
  МГРП nonzero only at spl0". `BASEPRI(x)` is still `0`, and for a reason that got *stronger*, not
  weaker: every `splN` above `spl0` sets БлПр, so code holding a raised level cannot be
  interrupted at all. Only the argument needed replacing. Both now carry the warning that МГРП is
  a source enable and says nothing about the level — the misreading that produced the original
  text.

**18b.3. The drum driver (`dev/mb.c`) — DONE.** The one that had to come first: one
`__besm6_ext(EXT_DRUM1, cw)` and no command sequence, so it proved the control word, the zone
model, the service-word buffer and the whole interrupt path with the least in the way. `swap()`
now round-trips a page; everything after this is disk-specific.

Geometry as sketched: 256 zones per drum × `8 + 1024` words, two drums = 1024 blocks of `BSIZEW`.
`mbstart()` builds the control word and issues it, `mbintr()` disarms and calls `iodone()`, and
the two drums are one linear block space so `nswap = 1024` is a single number and the minor
selects nothing.

**Verified by** a clean `make` (`061327`, was `060704` — 403 words), the new `test/mbtest`, and
all **13** `test/` SIMH tests passing. The drum's own trace (`set drum debug`) shows exactly the
twelve exchanges the test intends:

```
### запись МБ 10 зона 00 память 40000-41777          <- check 1, page mode
### чтение МБ 10 зона 00 память 40000-41777
### запись МБ 10 зона 00 сектор 2 память 40000-40377 <- check 2, four chained sectors
### запись МБ 10 зона 00 сектор 3 память 40400-40777
### запись МБ 10 зона 01 сектор 0 память 41000-41377
### запись МБ 10 зона 01 сектор 1 память 41400-41777
### запись МБ 20 зона 00 память 40000-41777          <- check 3, the other drum
```

How it turned out, where that differed from the sketch:

- **Both transfer modes, not one.** The sketch said "pick one, page mode is simpler if `swap()`
  always arrives page-aligned". It does — but the *block number* need not be even, and that is
  the half the sketch missed: swap space is handed out by `malloc(swapmap, …)` in blocks, and
  `sys1.c:61` allocates `(NCARGS + BSIZE - 1) / BSIZE` of them for exec arguments, which nothing
  rounds to a zone. One odd allocation and every later one starts mid-zone. There is no
  half-zone field on the drum (`DISK_HALFZONE` is the disk's), so an odd block *must* go as
  sectors. `mbstart()` therefore takes page mode when the exchange is zone-aligned,
  page-aligned and a whole zone long, and one 256-word sector otherwise; `mbintr()` chains.
  The common swap case is one exchange, the awkward one four.
- **`__besm6_ext()` with a COMPUTED address is broken in b6cc, and cost an afternoon.**
  `__besm6_ext(EXT_DRUM1 + ctlr, cw)` reads well and is wrong: the compiler emits `14 ext 0`
  — address from r14 — while leaving a frame pointer in r14, so the exchange goes to whatever
  device that address lands on. It landed on a tape controller, which halted SIMH with
  "Clearing interrupts AND attempting to do something else". `doc/Intrinsics.md` documents the
  computed path as a fallback for addresses above `07777`; it does not work today. The driver
  branches to two constant addresses instead, which is also the better code — a constant folds
  into the instruction's own address field, which is the entire point of the intrinsic. **Use
  constant addresses in `dev/md.c` too** (18b.4 has four: `EXT_DISK3/4`, `EXT_DISKCTL3/4`).
- **The `EXT_IOERR` poll was pulled forward out of 18b.5**, and it is not optional. An
  unattached drum transfers nothing and **never interrupts** — it says so only in the error
  mask — so without the poll a missing drum is a kernel that waits forever, not a failed
  request. Bite-tested: remove it and `mbtest` run 2 hangs. That is the drum's whole error
  story, so 18b.5 is now purely the disk's.
  - Worth knowing for 18b.5: `033 4035` also stands after *reading a zone the backing file
    does not reach yet* (`drum_read()` in `besm6_drum.c` sets the same bit), so the driver
    reports an unwritten zone as an I/O error. That is right for swap, which always writes
    before it reads, and it is why `mbtest` writes zone 0 before reading anything.
- **`mbintr()` has an idle guard, and it needed one.** A drum completion with `mbtab.b_active`
  clear cannot happen from this driver, but the bit is wired: returning without disarming
  leaves it standing and `extintr()` calls back forever. Bite-tested: neuter the guard and
  `mbtest` hangs.
- **`ugrp` part 1 had to move.** It forged `GRP_DRUM1_FREE` to prove `extintr()`'s *fallback
  probe* does not spin on a wired bit. That bit now has a handler, so the test moved to
  `GRP_CHAN3_FREE` — same kind of bit, still unclaimed. Bite-tested: point it back and `ugrp`
  hangs. (18b.4 duly claimed `GRP_CHAN3_FREE` and moved the test again, to `GRP_CHAN5_FREE` —
  a tape channel, so that eviction was the last one.) The drum
  half of what `ugrp` used to cover now lives in `mbtest` as check 5, where the real `mb.o` is
  linked and the idle guard is the thing under test rather than the probe.
- **A round trip is not a test of the mapping, and the first version of `mbtest` was fooled by
  exactly that.** Writing a pattern and reading it back passes whether or not the data went
  where `b_blkno` says: forcing page mode unconditionally still passed, because the driver then
  wrote and re-read the whole of zone 0 self-consistently. Two checks fix it, and both are
  bite-tested — read zone 0 back *whole* after the sector write and require it to be half check
  1's pattern and half check 2's (a wrong `DRUM_SECTOR` or `DRUM_PARAGRAF` shift fails here
  too), and re-read zone 0 after the drum-2 round trip, since with `ctlr` stuck at 0 block 512's
  zone field wraps in eight bits back to zone 0 and would destroy it unnoticed.
- **`mbtest` runs twice from one `.ini`**, with a mode word deposited at `0100`. The machine has
  exactly two drums, so a real second-drum transfer and a missing-drum check cannot coexist in
  one run: run 1 has both attached, run 2 detaches drum 2.
- **The test buffer has to live below word 32767.** Physical page `040` — word 32768 — is one
  past the 15-bit word field of a C pointer, so `fill()` silently wrapped to address 0 and
  overwrote the service words and the mode word. This constrains only the *test*, which has to
  look at what arrived: `b_paddr` is a plain `unsigned` and the driver reaches all 512 Kwords.
  The same trap is waiting for anything else that inspects high memory from C.
- **The swap clamp is now `PGSZ`**, one zone, which is the most one exchange can move;
  `test/biotest.c`'s three assertions moved with it, as 18b.1 said they would.
- **Four `.ini` files needed their halt PC edited** (`uclock`, `uswtch`, `usched`, `biotest`)
  because adding an `mbintr()` stub to three tests, and removing a multiply from a fourth,
  shifted `halt` by a word. Every test in this directory hardcodes that address. It is a
  standing tax on any change to shared code and will be paid again; worth replacing with a
  symbol lookup if it is paid much more often.

**18b.4. The disk driver (`dev/md.c`) — DONE.** The two-step protocol, and the last driver 18b
needs. `mdstrategy()` moves data: four commands per exchange instead of the drum's one
instruction, a half-zone that is exactly a `BSIZEW` block, and two controllers of four groups
each behind one major number.

**Verified by** a clean `make` (`047753`, was `047310` — 291 words), the new `test/mdtest`, and
all **14** `test/` SIMH tests passing. The disk's own trace (`set md0 debug=OPS;DATA`) shows
exactly the exchanges the test intends:

```
::: КМД 3: cmd 00004000 = выдача адреса дорожки 0000.0
::: запись МД 00 полузона 0000.0 память 40000-40777   <- check 1, an even block
::: КМД 3: cmd 00004001 = выдача адреса дорожки 0000.1
::: запись МД 00 полузона 0000.1 память 40000-40777   <- check 2, the ODD half of the zone
::: чтение МД 00 зона 0000 память 40000-41777         <- check 3, the same zone read WHOLE
::: запись МД 00 полузона 0001.0 память 41000-41777   <- check 4, DISK_HALFPAGE on the write
::: чтение МД 00 полузона 0001.0 память 40000-40777   <-   ...and read back to the page base
::: КМД 3: cmd = 00002001, выбор устройства 10        <- check 5, group 1
::: КМД 4: cmd = 00002001, выбор устройства 40        <- check 6, the second controller
```

How it turned out, where that differed from the sketch:

- **The blocker was already gone.** `buffers[]` is no longer bss: `besm6.S` names it absolutely
  (`buffers = u - NBUF*BSIZEW`) and it sits at `064000` = word 26624 = page 26 exactly. Buffer *i*
  starts at `26624 + 512*i`, so even buffers are page-aligned and odd ones half-page-aligned —
  which is precisely what `DISK_HALFPAGE` expresses. The old note claiming `044730` described the
  bss layout and had been stale since that change.
- **The unit-select mask is NOT inverted, and the sketch repeated the error from the doc.** The
  simulator's `disk_ctl()` tests `BBIT(8)` → drive 7 down to `BBIT(1)` → drive 0, i.e. plain
  `1 << unit`. The "bit 8 → unit 0" reading comes from a *comment* in `besm6_disk.c` that
  contradicts the code directly beneath it, and it had propagated into
  `doc/Besm6_Peripherals.md`, into `md.c`'s skeleton comment and into this file. All three fixed.
  Bite-tested: invert the mask and `mdtest` fails with `ACC 0777`.
- **`DISK_HALFZONE` is dead, so the sketch's "bit 1 makes one block one transfer" was right about
  the outcome and wrong about the mechanism.** The simulator declares the bit and reads it
  nowhere; which half of the zone moves comes from **bit 1 of the track-address command**
  (`c->track = cmd & 1`). Setting it in the control word instead gets the wrong half silently.
  Bite-tested: `ACC 0420` — the placement check and the cross-controller check, which are exactly
  the two written to catch a wrong half. `CW_UNIT` is dead on the disk for the same reason;
  `besm6disk.h` now says so at both fields.
- **Both transfer modes, as on the drum, but for speed rather than necessity.** A half-zone is a
  block, so unlike `mbstart()` nothing *forces* a second mode; `CW_PAGE_MODE` is taken when the
  request is zone-aligned, page-aligned and a whole zone long, which halves the exchanges for a
  `physio()` page and is what `mdtest` check 3 rides on. Bite-tested: force page mode on and check
  3 fails.
- **The command ORDER was corrected, and the correction is NOT defended by the test.** Group and
  unit select *raise* the controller's ГРП free bit and only the control word lowers it, so
  arming after "step 1 then step 2" arms on a bit already standing. `mdstart()` therefore issues
  both selects *before* the control word. But rewriting it the documented way and re-running
  `mdtest` still passes: SIMH performs the whole transfer synchronously inside the track-address
  command, so a completion taken early still finds the data in memory. The order is kept because
  it is right on the real machine, where an early completion is a torn buffer. Written into
  `md.c`'s header and `mdtest.ini` as a rule this test does *not* prove — the alternative was a
  comment claiming a guarantee nobody checks.
- **The `EXT_IOERR` poll carried over unchanged and is still not optional.** An unattached unit
  records itself in the mask and returns without scheduling a completion, exactly as an
  unattached drum does. Bite-tested: remove the poll and `mdtest` run 2 hangs at `02230` instead
  of failing. `mdintr()`'s idle guard likewise — neuter it and run 1 hangs at `02176`.
- **The minor number is a flat drive index, and there are no partitions.** `minor` is bit 5 the
  controller, bits 4–3 the group, bits 2–0 the drive — deliberately identical to the simulator's
  own unit subscript, so a minor number and a SIMH unit name are the same number, which is worth a
  great deal when a transfer goes somewhere unexpected. `rootdev` and `pipedev` are now
  `makedev(0, 0)` = `md00`. One drive is 1000 zones = 2000 blocks ≈ 6 Mb and swap is on the drums,
  so a whole-drive filesystem is the honest first cut; the x86 minor 56 (an MBR slot) is gone.
- **The drive type is fixed at ЕС-5052 (7.25 Mb), the simulator default**, as the sketch asked.
  ЕС-5061 (29 Mb) was considered and rejected: `IS_29MB(u)` short-circuits both the read and the
  write path to a whole-zone transfer and ignores `CW_PAGE_MODE`, so the smallest exchange there
  is 1024 words while the buffer cache asks for 512 — a block read would splatter over the next
  buffer. Bridging it needs a bounce buffer with a read-modify-write on every sub-zone write, or a
  filesystem block of one zone (`BSIZE` 6144 / `BSIZEW` 1024, which `param.h`'s own comment
  anticipates) — which ripples through `BSHIFT`, `BMASK`, `NINDIR`, `itod`/`itoo`, `mb.c`,
  `nswap`, `biotest` and the on-disk format. The cost of the choice is capacity: ~6 Mb a drive
  instead of ~24. `mdtest.ini` says so, because `set md0 ec-5061` breaks the test by design.
- **No seek state machine**, as the sketch said: `001`–`004` and `006` are logged and do not act,
  the direction coming from the control word and the transfer from the bit-12 command.
- **`ugrp` part 1 moved again, and this time for good.** It forged `GRP_CHAN3_FREE`, which now has
  a handler, so it moved to `GRP_CHAN5_FREE` — tape channel 5. The two earlier bits were chosen
  because they were *unclaimed yet*, a property with an expiry date; this one is unclaimed because
  there is no driver to write. `GRP_CHAN5_FREE` was added to `besm6dev.h` for it. The disk half of
  what `ugrp` used to cover now lives in `mdtest` as check 8, where the real `md.o` is linked.
- **Five `.ini` files needed their halt PC edited again** (`ugrp`, `uclock`, `uswtch`, `usched`,
  `mbtest`), all by exactly one word, because four tests gained an `mdintr()` stub and `intr.o`
  gained a dispatch arm. 18b.3 predicted this tax would be paid again; it was. The value to use is
  the `halt` symbol from the test's own `.nm`, which the build already generates.
- **Three disk images, ~8.25 Mb each**, because three drives have to be attached to prove the
  group and the controller, and `attach -n` formats a whole unit. Their filenames must carry a
  digit run in 2048..4095 — the simulator scrapes a volume number out of the name and refuses
  anything else. `make clean` removes `*.disk` alongside `*.drum`.

*Not done here, and still 18b.6:* `iinit()` reading a real super block. That needs a root
filesystem image to exist, which nothing in this tree builds yet.

**18b.5. Disk status and errors.** Split out because the drum has no equivalent and the happy path
should land first.

* Read the status register with `__besm6_ext(EXT_DISKSTAT3, 0)`. SIMH sets only `STATUS_READY` (to
  command `011`) and `STATUS_ABSENT`/`STATUS_POWERUP`/`STATUS_READONLY` (to command `031`, shifted
  right by 12) — so the retry logic is written against the documented bits but can only be
  *exercised* for those. Say so in the source rather than leaving untestable code looking tested.
* Error count and retry through `b_errcnt`, `B_ERROR` and `b_resid` on give-up, the v7 way.
* ~~Also the drum's much smaller version~~ — **done in 18b.3**, which had to: without the
  `EXT_IOERR` poll a missing drum is a hang, not a failed request. `IOERR_DISK(n)` is already in
  `sys/besm6dev.h` next to `IOERR_DRUM(n)`; the disk's is the same check plus everything below.
* **Done when** an unattached unit fails the request instead of hanging.

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
  from an `int` gets the fat-pointer byte-offset field wrong (see `usermem.S`, `fubyte`).
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

Task 18b wanted three — one per step 18b.2, 18b.3 and 18b.4 — and all three are written: `ugrp`,
`mbtest` and `mdtest`. What they cost to get right, for whoever writes the fourth:

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
`BSHIFT 9` / `BMASK 0777` still describe a 512-*byte* block. So every byte-offset → block
conversion in the kernel is wrong: `rdwri.c:48-49,111-112`, `nami.c:138-156`, `sys1.c:89-128`
(the exec arg staging), and `dev/bio.c:492`.

**The indirect-block half of this is now fixed** (done alongside making `param.h`
assembler-includable). `NINDIR` is `BSIZE/sizeof(daddr_t)` = **512**, but `NMASK 0177` /
`NSHIFT 7` still said 128, so `bmap()` (`subr.c:68-121`) could only ever index the first
quarter of each indirect block while `tloop()` (`iget.c:246`) freed all 512 — the two
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
