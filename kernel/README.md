# The BESM-6 Unix v7 kernel

The Unix v7 kernel, retargeted to the BESM-6. Sources are derived from Robert Nordier's v7/x86
port; the upstream copyright is in the top-level [COPYRIGHT](../COPYRIGHT).

This file is the **reference**: what the port is, the design it settled on, the hardware rules
that design obeys, and the things that cost real time to establish. The **work plan** is next
door in [TODO.md](TODO.md). Findings belong in the source comments and in [../doc/](../doc/) —
this file records only what more than one task has to know.

## Where the port stands

The machine-dependent half of the kernel is **done**, and so is everything under a shell:
`cd kernel && make` links an image that boots under SIMH, mounts `root3072.disk`, execs the real
`/etc/init`, which forks `/bin/sh` — and you get a prompt. The shell runs `cat`, `echo`, `ls`,
`pwd` and `sync` off the disk, the kernel does the erase, kill and `^D`, a session that writes
files and calls `sync` leaves an image that fscks clean, the libc test programs run off
`/usr/test` and produce byte for byte what they produce under `b6sim`, and on a 31-page machine
the swapper moves real images through the drum while `/bin/sh` and `/usr/test/puret` share one
copy of their text. `make run` is where you talk to it.

**The drums must be attached to exec anything**: they are `swapdev` ([conf.c](conf.c)), and
`exece()` stages the argument list in swap — `getblk`/`bawrite` in, `bread` out — before it ever
touches the new image. With no drum that `bread` comes back `B_ERROR` and every exec fails with
`exec init: error 5` (EIO). Every `.ini` that boots therefore says `attach -n drum0 …`.

## Building

```sh
cd kernel && make          # produces `unix' (BESM-6 a.out), unix.nm and unix.dis
make run                   # boot it under SIMH (`besm6 unix.ini')
make clean
```

The Makefile is a thin wrapper over the top-level CMake `build/` tree, and cross-compiles with
the **in-tree** tool targets, so a rebuilt `b6as` relinks the kernel with no `make install` in
between. The link takes `-lruntime` **alone** — the kernel defines its own `printf` in
[prf.c](prf.c) and calls no library routine.

`make` finishes by printing `b6size -w unix`: the image **must end below `062000`** (`KEND` in
[../include/sys/param.h](../include/sys/param.h)), because supervisor instruction fetch is never
mapped and the top of the unmapped space is spoken for — see the map below. The kernel is
archived into one link-pulled `libunix.a` so unused code is dropped; `besm6.o` must come **first**
in `OBJ` so its const contribution pins the interrupt/extracode vectors at their fixed addresses.
The `###` block at the foot of the Makefile is the header dependency list and is **hand-
maintained**: `b6cc` and `b6cpp` implement no `-M` family, so nothing regenerates it.

## The tests

Six tests cover the image the build produces ([../root.manifest](../root.manifest) →
`root3072.disk`), each going one step past the last, which is what keeps the diagnosis apart.

| test | asserts |
|---|---|
| `fstest` | the superblock and root inode read through the real `md` driver, buffer cache and `sbcheck()`, strictly below the boot path |
| `boot` | process 1 leaves the kernel, execs `/etc/init`, which forks `/bin/sh`, and the shell **prompts** |
| `console` | a typed dialogue with that shell: erase, kill, a line longer than a clist block, `>/dev/tty`, `pwd`, `ls /bin`, and `^D` round through `/etc/rc` to the next prompt |
| `session` | the shell **writes** — files, an inode past its direct blocks, `sync` — and the host then fscks the container and diffs what was written |
| `libtest` | the twenty-three [../lib/test/](../lib/test/) programs run off `/usr/test`, each matching **the same `.expected` file `b6sim` is held to** (`memt` and `shellt` run here only) |
| `swap` | the same kernel on a machine of **31 pages** (`phymem` deposited before the boot), running more processes than fit: `sched()`/`newproc()` swap through the drum, two processes share one text, and the counters say so |

`boot` attaches the pristine disk read-only — an assertion in itself — and the others each convert
their own copy at their own volume number, so no test ever writes a build artifact. The rest of
[test/](test/) exercises one kernel component at a time against a hand-built environment; see
"Writing a standalone SIMH test" below.

## Reading list

Read [../doc/Memory_Mapping.md](../doc/Memory_Mapping.md) before touching memory management,
[../doc/Intrinsics.md](../doc/Intrinsics.md) for how C reaches `002 «рег»` and `033 «увв»`,
[../doc/Unix_Context_Switch.md](../doc/Unix_Context_Switch.md) for the gates,
[../doc/Kernel_Assembly_Routines.md](../doc/Kernel_Assembly_Routines.md) for the machine assist,
and [../doc/Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) before touching [dev/](dev/).

---

## The design

**The kernel runs unmapped.** БлП = 1 and БлЗ = 1 the whole time the kernel is in control, so a
kernel address *is* a physical address. The kernel image occupies the **first 32 physical pages** —
everything below `0100000`, which is exactly the reach of an unmapped data access.

**РП always holds the current process's map.** The kernel never programs a map of its own, so a
trap costs nothing: the hardware forces БлП/БлЗ on at the vector, which is already the kernel's
mode. Entry and exit touch no mapping register.

**The u-area is the last two pages of the kernel space** — physical `074000`–`077777`, holding
`struct user` and, above it, the per-process kernel stack growing up to `0100000`. `struct user`
is therefore at a fixed *physical* page and is **copied in and out on a context switch**. That is
the price of an unmapped kernel, and it is the one we pay.

**Only the first of those two pages is copied** — `USIZE` words from `UBASE`. The page above it is
stack **overflow**: the stack may grow into it and run there correctly, but it is in no process
image and no context switch saves it, so a process that reaches `sleep()` or `swtch()` with `r15`
above `076000` loses those frames ([TODO.md](TODO.md) task 31). The rule is written once, at
`UBASE` in [../include/sys/param.h](../include/sys/param.h).

**Mapping is enabled only inside a few short assembly brackets** — to touch a user page
(`copyin`/`copyout`/`fubyte`/…), and to reach a physical page above `0100000` (`copyseg`/`clearseg`,
and the u-area save/restore itself).

```text
PHYSICAL, pages 0..31 — the kernel, addressed with БлП = 1 (no translation)

   0        const   (interrupt vector 0500/0501, extracodes 0550-0577, literal pool)
            text    (fetched unmapped: РП is irrelevant to it, always)
            data + bss
   ...      must all end below 062000 = KEND
   062000   BUFFERS ------ buffers[NBUF][BSIZE], NBUF*BSIZEW = 5120 words -----
              a fixed PHYSICAL area, not bss: the drum/disk controllers transfer
              to a physical address.  `buffers = BUFBASE', absolute, in besm6.S;
              main.c declares it `extern'.  Raising NBUF lowers KEND with it.
   074000   U AREA, saved half ---- USIZE words: what a switch copies --------
              struct user     (~140 words)   `u = 074000`, an absolute symbol
              kernel stack    (884 words, grows UP past 075777 into...)
   076000   U AREA, overflow ------ 1024 words, saved by NOTHING -------------
              the stack may run here but must not SLEEP here (TODO.md task 31)
   0100000  end of the unmapped reach; everything above is the page pool

РП — the current process's map, 32 pages, loaded by sureg()

   page  0..     user text (physical page != 0), data, bss, break growing up
   page 28..31   user stack, base 070000, grows UP to the 0100000 ceiling
   unallocated pages: РП = 0 (non-executable) and РЗ bit set (no data access)

   The user gets all 32 pages. The u-area is not in this map — it is physical.
```

### Shared text, and what the paging store owes it

**A pure (`NMAGIC`) binary's const+text is one shared region**, loaded once and mapped into every
process running it; an impure (`FMAGIC`) one has none at all, because `getxfile()` folds const and
text into data and forces `ux_tsize` to 0, at which point `xalloc()` returns on its first line —
so until task 26 linked something `-n`, the whole of [text.c](text.c) was unreachable. Two images
are pure now, `/bin/sh` and `/usr/test/puret`, and `b6_prog()`/`b6_libtest()` take a `PURE` keyword
for a third.

"Pure" here means **shared, not protected**: РЗ closes a page to reads as well as writes, so a
read-only text page would take the program's own constant pool with it. What `-n` buys is one copy
of the text in core, which on a 31-page machine is the difference between four processes and none.

**A swap slot must be written in full before it is read.** On the PDP-11 a never-written block read
back as whatever was on the platter, which v7 relied on: `expand()` raises `p_size`, swaps out only
the *old* size, and `swapin()` reads the new one back. Here a drum zone the container has never
reached is an **I/O error** and `swap()` panics, so `xswap()` zero-fills the tail of the slot before
it lets go of the core. The reason is at the call site.

### The click is dead

v7's "click" has no place on a word-addressed machine. Every size and address in the kernel is
counted in **48-bit words**: `p_addr`, `p_size`, `x_caddr`, `x_size`, `u_tsize`/`u_dsize`/`u_ssize`,
the coremap, `USIZE`. Where the hardware needs a page, the value is a word address that is a
multiple of `PGSZ` (1024) and the map builder shifts by `PGSH` (10). `ctob`/`btoc`/`ctod` are
replaced by `btow`/`wtob`/`pground`/`wtodb`; the swapmap still counts disk blocks (512 words), only
the coremap changed unit. `copyseg`/`clearseg` still move exactly one page, so every loop that
calls them steps by `PGSZ`.

### The mapped brackets

Each is a short assembly routine that runs **entirely out of index registers**: while mapping is on,
the kernel's own data — including its stack — is not addressable, because virtual `074000` then
names the *user's* page 30.

| bracket | why | what it maps |
|---|---|---|
| `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword` | reach a user page | nothing — the user's map is already loaded. The loop toggles БлП per word: read the user word mapped, store it to the kernel buffer unmapped. |
| `copyseg`/`clearseg` | reach a physical page above `0100000` | steals virtual pages 1–2 as windows (one `mod 020`), restores the quartet from `u.u_upt[]` afterwards |
| `uflush()`/`uload()` | save/restore the **saved half** of the u-area across a context switch | steals virtual page 1 for the process's u home and virtual page 2 for the live u-area (the descriptor is derived from `UBASE`, not spelled, so it cannot drift from the geometry); both live in quartet 0, so one `mod 020` steals them and one puts them back |

**Never virtual page 0.** A store to virtual address 0 is dropped and a load returns 0:
`mmu_store()`/`mmu_load()` test `addr == 0` *before* translation, so the black hole is in the
**virtual** address, whatever page 0 is mapped to. Pages 1 and 2 are also the cheap choice: they
share quartet 0 and their addresses (`02000`–`05777`) fit the 12-bit short address field, so the
copy loop needs no `utc`. (The same black hole is why a user image starts at **word 8** (`BADDR`):
the a.out header hole occupies words 0–7, so nothing the program touches lands on the sink. See
`getxfile()` in [sys1.c](sys1.c).)

An interrupt taken inside a bracket is harmless *for addressing* — the hardware forces БлП = 1 at
the vector, so the handler sees the kernel's normal unmapped world — but it is **not** harmless for
`uload`, which is overwriting the page the handler's frame would be in, so that bracket holds БлПр.
Note that **`vtm N,0` writes БлПр along with БлП and БлЗ**: a bare `vtm 2`/`vtm 3` *enables*
interrupts as a side effect. A bracket that wants them off says `02002`/`02003` and restores PSW
afterwards (`ita 021`/`ati 021` — supervisor takes a 5-bit register number, so `M[021]` is
reachable).

### Five hardware rules everything obeys

1. **РП/РЗ cannot be read back.** The map is a shadow table in memory: `u.u_upt[8]`, eight words,
   each carrying four РП descriptors *and* (in bits 21–28 of the even words) the matching РЗ byte —
   so `sureg()` is 8 × `рег 020+i` plus 4 × `рег 030+j` with no shifting.
2. **The kernel keeps БлЗ set (protection off).** РЗ is consulted even when mapping is off, against
   `addr >> 10`, so a kernel running unmapped with the previous process's РЗ loaded would fault on
   its own bss. The hardware sets БлЗ at the vector — never clear it, not even in a bracket.
3. **Drain the БРЗ write cache** — nine consecutive stores to physical 1–7 with mapping off.
   `drainbrz()` is the one routine in the kernel that **has to be assembly** (see [brz.s](brz.s));
   `test/mmutest` is what proves the drain is load-bearing. Stores made *unmapped* are tagged
   physical and survive a map change; stores made *mapped* are tagged virtual and do not. Three
   obligations, each with a different reason:
   * **before every РП write** — the hazard is invisible under default SIMH and fatal under
     `set mmu cache`;
   * **before user code FETCHES a word the kernel wrote through the map** — the instruction path
     does not consult the write cache (`mmu_prefetch()` reads memory directly) while a data load
     does, so a word `copyout()` has just written reads back correctly as data and fetches as
     garbage. Measured: right after `main()`'s `copyout()` of the icode, seven of its nine words
     were still in `BRZ0`–`BRZ7`. `exec()` gets the drain for free from the `estabur()` after its
     `readi()`; `main()`'s icode copy and `sendsig()`'s trampoline word drain for themselves;
   * **before a DEVICE reads memory** — a write exchange transfers out of memory, not out of the
     cache. Both mass-storage drivers drain on the write path ([dev/md.c](dev/md.c),
     [dev/mb.c](dev/mb.c)); the disk's sector header, stored two instructions before the exchange,
     is what made it visible. A *read* needs nothing: the device is the writer.
4. **There is nothing to invalidate** — writing РП refills the TLB in the same instruction, so a
   stale translation is not a state the machine can be in. v7's `invd()` is deleted, not stubbed.
5. **A fault reports the faulting *page* (ГРП bits 5–9), and the saved PC points *past* the faulting
   instruction.** Anything that means to retry — stack growth — must back the PC up using
   `SPSW_NEXT_RK` and `SPSW_RIGHT_INSTR`.

### The u-area invariant

The live u-area is at `074000`; the copy in the process's image at `p_addr` is stale between
switches. A kernel global `uhome` records whose home the live u-area belongs to, and `NOUHOME` (0)
says it has no home at all — the state `exit()` and a freeing `xswap()` leave behind, without which
the next `resume()` would flush 1024 words into core `malloc()` may already have handed out.

`resume()` ([switch.s](switch.s)): if `paddr != uhome`, `uflush(uhome)`, then `uload(paddr)`, then
`uhome = paddr`. Only then restore r1–r7, r13, r15 from the label — which, being at `074000+n` in
*every* process, now names the incoming process's saved state. That constant is the whole trick.

**Anything else that reads or frees the current process's image must flush first.** This is the
sharpest edge in the whole design; it has bitten twice, and both times the site was one the list
did not have. The complete rule — all six sites, and why the test belongs inside `xswap()` rather
than at its call sites — is written up **once**, in the block comment at `xswap()` in
[text.c](text.c). Add to it there, not here.

---

## Writing a standalone SIMH test

Verification is under **SIMH** ([../doc/Simh_Simulator.md](../doc/Simh_Simulator.md)) via
`test/*.ini`: `b6sim` runs a user `a.out` with no kernel underneath and cannot exercise any of
this. Each test is a standalone BESM-6 program that links kernel objects against a hand-built
environment, plus a `.ini` that loads it into the real simulator, runs it, and asserts on the
machine state afterwards. `test/mmutest` is the model to copy — it links the kernel's own
`utab.o` and `brz.o`, lets `sureg()` program the MMU, and checks the mapping both from C and by
examining РП/РЗ from the `.ini`. **Run every MMU test with `set mmu cache`**: the БРЗ write-back
hazards are invisible without it, and a kernel that only worked with the cache off would not have
worked on the real machine.

**`besm6.o` cannot go into a standalone test** — its `0500` vector reaches into the C kernel and
its `_start` seeds no stack. That is why every routine a test has to link lives in its own file
(`brz.s`, `uarea.S`, `seg.S`, `usermem.S`, `switch.s`, `syscall.c`, `sendsig.c`) and why the gates
are duplicated in the tests' own crt0s.

What the ones already in [test/](test/) cost to get right:

* **A round trip proves nothing about addressing.** Write a pattern, read it back, compare — and a
  driver that put the data in the wrong place passes, having been consistently wrong twice.
  `mbtest`'s first version passed with page mode forced on and `ctlr` nailed to 0. What works is
  leaving *two different* patterns from two different requests and reading the region back whole, so
  the check is about where the boundary between them fell.
* **Make the test cross the boundary it is about.** `console.ini`'s short lines never leave the first
  clist block, so one stage types 48 characters against a `CBSIZE` of 30 on purpose; `session.sh`
  copies `/bin/ls` because 33 KB is the smallest file that must reach through an inode's indirect
  block. Ask what the sizes in the test are relative to the sizes in the code.
* **Ask what would notice if the code were wrong.** 18b.5 classified disk failures into hard and
  soft, but both ended in the same failed request with the same `b_resid`, so the classification was
  undefended and the bite test duly passed while the source claimed it would fail. Exposing
  `mdretries` and asserting the exact count is what closed it.
* **Write the bite test, then verify it bites.** `ugrow` was checked both ways: reintroducing a stack
  shuffle makes it fail with `020`, dropping the `sureg()` after the growth with `0212`.
* **When a hazard is a RACE, say so instead of pretending a green test covers it.** `main()`'s
  `drainbrz()` can be deleted and `boot` still passes; the argument for it is a *measurement* (break
  after the `copyout`, `ex BRZ0`–`BRZ7`). Measure the state directly, write the measurement down at
  the call site, and mark the test as not covering it — `test/boot.ini.in` says so in as many words.
* **A test that re-uses another harness's expectation file is worth more than one of its own.**
  `libtest` diffs each program against the very `lib/test/*.expected` that `b6sim` is held to, and
  allowed for kernel-side overrides. **Not one override was needed** — every divergence was a kernel
  bug. Had each program been given a freshly recorded expectation, both bugs would have been checked
  in as the correct answer.
* **`step N`, not `go`.** A broken switch or a lost gate *hangs* rather than failing, and `go` takes
  an address, not a step count. Every `.ini` uses `step 50000000` to turn a hang into a failure.
* **A test that needs a TYPED line drives it with `expect`/`send`** (`console.ini`). Injected input
  arrives through `sim_poll_kbd()`, which serves it *before* the real keyboard, so the dialogue runs
  under ctest with no terminal on stdin — a plain pipe into `besm6` is **not** a substitute. A match
  **halts** the simulator and then runs the rule's action, so each action must end with its own
  `step N`; the run is then bounded by the ctest `TIMEOUT`, and the trailing `echof …; exit 1` after
  the last `step` is what a never-fired rule falls into. `-c` is **CLEARALL**, not "compare
  literally": a bare `expect "…"` already matches exactly, and `-c` on any but the last rule throws
  the others away.
* **An `expect` action that does not `step` hands control back to the script** — which the *last*
  action can exploit, using `goto` instead, so the commands at the label run with the machine stopped.
  That is how a boot-level test asserts on memory, and it is also what distinguishes a finished run
  from one that merely exhausted its step budget.
* **`send` DROPS A CHARACTER now and then, and it is not the kernel.** Under `CTEST_PARALLEL_LEVEL=8`
  `session` fails perhaps one run in four with `s: not found` — the shell really received `s` where
  the script sent `sh /etc/session`. Measured on an unmodified tree. Re-run before believing it.
* **The interval timer cannot be switched off.** It free-runs at 250 Hz and the SIMH `CLK` device has
  no `DEV_DISABLE`, so a second tick may land mid-run. Phrase every assertion to tolerate exactly one
  — a draft `p_cpu >= 1` check once passed *only because* a second tick arrived after the aging code
  zeroed it.
* **`deposit phymem` before `go` is the memory-pressure knob**, and the banner is its receipt.
  `phymem` (machdep.c) is one initialized word with one reference, and `startup()` derives `maxmem`,
  the coremap extent and the two banner lines from it. A full machine frees 479 pages of core against
  512 pages of swap, so the swapper never runs; always `expect` the `user mem =` line too, or a
  deposit that missed leaves a test quietly running on a full machine.
* **A `.ini` CAN assert on a kernel variable, but not in the parenthesised form.** `if (...)` goes
  through SIMH's expression evaluator, which cannot name an address; the bare form `if <addr> <cond>`
  reads memory. Two traps: the word comes back **50 bits wide** (the top two are the parity tag
  `mmu_store()` wrote, and it is 0 or 1 depending on which half-word the storing instruction sat in,
  so an unmasked comparison changes when you recompile — mask with `&07777777777777777`), and no
  space may appear inside the condition token. `ex <addr>` prints it untagged and is what FAIL
  diagnostics should use. `test/swap.ini.in` is the worked example.
* **Read a bite test on ACC, never on the halt PC** — and rebuild before believing either. A "failed"
  run once turned out to have grown a literal by one word, moving `halt` from `0575` to `0576` so the
  `.ini` tripped its *PC* assertion while every check passed.
* **A user program reports back through a deliberate data-protection fault, not `стоп`.** In user
  mode `стоп` re-dispatches as extracode э63 and check-halts under the reset ПоК; a data fault
  ignores ПоП/ПоК and always vectors.
* **A C pointer cannot name anything above word 32767** (`ptrword()`, 15 bits). A test buffer at
  physical page `040` wrapped silently to address 0 and overwrote low memory. This binds the test,
  not the kernel — `b_paddr` reaches all 512 Kwords — but any test that inspects what a device
  deposited must keep its window in the low 32 Kwords.
* **A forged `uprog` cannot use the literal pool.** It runs mapped at virtual page 0, but the pool
  lives in the crt0's `.const` at *physical* page 0, so a `#(...)` operand reads whatever happens to
  be there. `ugrow`'s uprog reads its sentinel out of a data page `main()` seeded instead. (`vtm`'s
  15-bit immediate is fine; it is part of the instruction.)
* **The test crt0s' interrupt gate must save the C register (М020).** `wtc`/`utc` arm the address
  modifier for the *next* instruction only, and b6cc routinely emits the pair in two different words,
  so an interrupt lands between them; `выпр` re-arms from М020, but from *whatever is there*. The
  symptom is a lost store, one word, no fault — with М020 left at 0 the store addresses virtual word
  0, the black hole. `uswap`'s fill loop lost exactly one word of a 4096-word image per run, at an
  index that moved whenever anything else in the file changed. `crt0w.S` did not do this and
  `kernel/besm6.S` always did.
* **A gate's temp cells must be reachable by a BARE address, which is why they go in `.text`.**
  `< sym >` assembles as `мода` (utc) *plus* the instruction, and `utc` loads М020 — so `atx <sa>` as
  the gate's first instruction destroys the very C register it is about to save. A bare address is 12
  bits and cannot reach a bss the linked kernel objects have pushed past `010000`.
* **`mmutest` owns the БРЗ-drain bite test**, not `uswtch`: dropping `uload`'s post-copy drain passes
  `uswtch` and still fails `mmutest` (code 17). Know which test proves which hazard.

## Gotchas worth not re-deriving

Facts that cost real time to establish and are not in `doc/`.

* **A computed `033`/`002` address must have the variable first.** `__besm6_ext(ctlr + EXT_DISKCTL3,
  cw)` folds into one instruction — the address rides the C register (`wtc`) and the constant folds
  into the address field. Written `EXT_DISKCTL3 + ctlr` it does *not* fold and costs a `b$uadd` call
  plus a stack round-trip. See [../doc/Intrinsics.md](../doc/Intrinsics.md) §8.
* **The intrinsics with an immediate first argument demand a compile-time constant**, so
  `__besm6_maskpsw()` cannot take a run-time level — which is why `splx()` uses `__besm6_setpsw()`
  and writes back the whole mode word its cookie carries. **The spl cookie is a PSW word, not a small
  integer**: never compare one against a level, never synthesize one, and never `splx(0)`, which
  would clear БлП/БлЗ and drop the kernel into its own user's address space.
* **v7 PACKS FLAGS INTO SPARE LOW BITS OF ADDRESSES, and this machine has none.** Both bugs `libtest`
  found on its first run were this one pattern:
  * `setregs()`'s `(*rp & 1) == 0` read bit 0 of a signal disposition as "ignored" ([sys1.c](sys1.c)).
    A PDP-11 function is at an even byte address; **a BESM-6 address is a word index and is odd half
    the time**, so every handler at an odd address survived `exec`. Measured: `/bin/sh`'s `fault()`
    is at `03331`, so five signals arrived at every command the shell started pointing into
    hyperspace.
  * `iomove()`'s `(int)cp & (NBPW - 1)` ([rdwri.c](rdwri.c)) masked the *word* address while the byte
    offset sits in bits 47–45, so the word-only fast path was taken on unaligned buffers about one
    time in eight and silently dropped up to five bytes per call.

  The rule: **a flag in a pointer has to be a separate field, and a mask on `(int)ptr` is almost
  always wrong.** [TODO.md](TODO.md) task 34 is the sweep.
* **A punned union member reads word 0 and does not fault.** `b_addr` was a *fat* pointer, and
  reading `struct buf`'s block through `b_un.b_filsys`/`b_dino` reinterpreted its bit-48 marker as a
  large exponent — so `fp->s_bsize` silently returned `s_magic` and every member past offset 0 came
  back as offset 0, in ~13 places. Fixed twice: an explicit cast at each site, then by making
  `b_addr` an `int *` so there is no marker to strip and the wrong spelling cannot be written. Prefer
  the fix that makes the bad spelling impossible.
* **A `char *` is a fat pointer** — marker in bit 48, byte offset in bits 47–45 — and the compiler
  walks one with `b$pinc`/`b$pdec`. Never build one out of a `(word, offset)` pair by hand; the
  worked example is `exece()`'s argument block ([sys1.c](sys1.c)), asserted by `mmutest` check 25.
  `(caddr_t)(int *)w` is the fat pointer to byte #0 of word `w`. Note also that `<` between two
  `char *` does **not** order them: the byte offset sits above the word address and decrements as the
  pointer advances. `-` is fine (`b$pdiff` decodes both operands).
* **The `b$` pointer helpers had to be made REENTRANT before the first exec could run**, and the fix
  is in the *external* c-compiler. `b$padd`, `b$pinc`, `b$pdec`, `b$pdiff` and `b$stb` kept their
  working values in static `.bss`, so a clock tick landing inside one ran a handler whose helper
  overwrote them. Observed as `iget()` reading `ip->i_flag` through a pointer that had become a
  pointer to `proc[]`: the root inode looked locked, process 1 slept and the machine idled forever,
  with no fault and no diagnostic. **A stale `libruntime.a` brings the bug back** — and `kernel/`'s
  link does not depend on that archive, so after reinstalling it you must `rm build/kernel/unix` to
  force a relink. The window is a few instructions wide; no test reliably bites it.
* **Unsigned arithmetic is calls.** `+ - * / < <= > >=` on an unsigned are `b$uadd`, `b$udiv`,
  `b$ult`, … because the additive unit reads bits 48–42 as an exponent. Every scalar typedef is
  therefore `int`; `unsigned` survives only where the value is genuinely a 48-bit hardware bit
  pattern (`u_upt[]`, the ГРП/ПРП masks, the a.out magic).
* **`_Static_assert` works and has teeth; `extern int x[1 - 2*(cond)]` does not.** `b6cc` accepts a
  negative array size without a word. `ino.h`, `dir.h` and `filsys.h` use the real thing.
* **`DIRSIZ` moves `u_upt`.** `struct user` holds `u_dbuf[DIRSIZ]` and a `struct direct` ahead of the
  shadow page table, whose word offset [uarea.S](uarea.S), [seg.S](seg.S) and `test/mmutest.c`
  hardcode as `UPT` (b6as has no `offsetof()`). mmutest's check 13 exists for exactly this — which
  makes the MMU tests load-bearing for a filesystem change, and that is not obvious.
* **The compiler folds a shift by a constant against the target's width — now.** `1u << 36` used to
  fold to `020`, the count masked to 36 & 31 on a machine where an unsigned is 48 bits, while the
  identical shift by a *variable* was right. Fixed in the external c-compiler
  (`optimize/const_fold.c`), so **a stale `b6parse`/`b6lower` brings it back**, silently and only
  above bit 32. `test/mdtest.c` is where it surfaced.
* **`b6ld -n` had never been used, and did not work.** The pad between text and data was not counted
  in `a_text`, so every reader computed the data segment's offset and load address from a short text
  and the image came up empty; and `etext` was left at the unpadded end. Note while fixing anything
  there that **`ld.torigin` and `ld.dorigin` are running cursors during pass 2**: in
  `finish_output()` they are the *ends* of their segments, which is why the pad loop reads oddly and
  must not be "corrected".
* **The SIMH disk container is not a flat block file.** Each word is eight little-endian bytes with a
  two-bit tag above the 48 (an empty data word is `0x0002000000000000`, not zero), and eight service
  words are interleaved per zone; one drive is 8,256,000 bytes against the flat image's 6,144,000.
  `b6fsutil -S` converts. Converting a container the kernel has *written* back to flat is the only
  check on those service words there is, which is how `session` found the disk driver never
  maintaining them.
* **A DRUM ZONE THAT HAS NEVER BEEN WRITTEN IS A READ ERROR, not garbage.** SIMH's `besm6_drum.c`
  fails the short `fread` and raises the same `drum_fail` an *unattached* drum raises, which
  `dev/mb.c`'s `EXT_IOERR` poll cannot tell apart. A **hole inside** the container reads back as
  zeros with no error, because the file is sparse; only a read **past the highest zone ever written**
  fails — and with `-n` drums that start at zero length, that is exactly where the first grown image
  lands.
* **`s_isize` is the first data block, not a count of i-list blocks**, and **the free list must be
  built descending** — `alloc()` pops the superblock cache from the top, so an ascending build lays
  every file backwards across the platter while passing every self-consistency check.
* **The v7 shell has no comment character, and a `:` line is still PARSED.** A backquote, an
  apostrophe, a parenthesis, a `$`, a `;` or a redirection inside what looks like a comment is a
  syntax error or a command run. This cost two round trips on `test/session.sh`; `../etc/rc` says it
  at length, and it binds anything written for the image.

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new): 1024 words
  each way, or ~300 with [TODO.md](TODO.md) task 30. This is the cost of an unmapped kernel; in
  exchange the trap path costs *nothing* and `copyin` needs no window.
* **Kernel-stack frames above `076000` are not saved.** The overflow page is where a deep path's
  interrupt frames live, and interrupt handlers never sleep, so the measured workload loses nothing:
  at peak the stack reaches `076100`–`076177`, while the deepest `resume()` in a boot → `/etc/rc` →
  shell → `ls /bin` run had `r15 = 075302`, 318 words below the boundary. A path that sleeps deeper
  than 884 words would silently lose those frames. Task 31 is the one-line detector.
* **The u-area invariant is a footgun.** It has bitten twice, and a seventh site added later and
  forgotten will still be a very confusing bug. The whole rule lives in one block comment at
  `xswap()` in [text.c](text.c).
* **`copyin`/`copyout` toggle БлП per word** (~2× a plain copy), and the fat-`char *` byte edges are
  read-modify-write, so an unaligned `read`/`write` goes byte-by-byte. Task 28.
* **`off_t` stays a byte count.** Making file offsets word counts is the BESM-6-shaped answer and
  would delete a `b$div` at every block crossing — but `BSIZE` is 3072 and not a power of two either
  way, the divide is **one per block** against 3072 bytes moved (`readi()` says so at the site), and
  the change is user-visible in `read`/`write`/`lseek` and in `iomove()`'s granularity. The cost is
  real and the benefit is noise. Do not re-open this without a measurement that contradicts it.
* **`time` is never seeded from a wall clock.** This machine has no clock-calendar a program can
  read, so the epoch starts at 0 and `iinit()` takes the superblock's `s_time`. `TIMEZONE` and
  `DSTFLAG` are therefore **0** rather than v7's US Eastern: an offset on top of an invented epoch
  says nothing, and zero makes `ftime()` agree with `b6sim`, which is what lets `lib/test/timet` be
  one file for both harnesses.
* **The tail of an image grown by `expand()` reads back as zeros.** v7 promised nothing there and
  nothing reads it, but this machine cannot leave those blocks unwritten at all, so `xswap()` writes
  zeros — a contract stronger than v7's, asserted by `test/uswap` leg 0.
* **`dev/mb.c`'s `drainbrz()` cannot be made to bite, and `test/uswap` says so.** Deleting it leaves
  the suite green, structurally: the БРЗ is eight lines evicted by age, and between the last store
  that fills a swap page and the `033` lie far more than eight kernel stores. `dev/md.c` is where the
  same hazard *did* bite, because its sector header is stored two instructions before the exchange.
  The drain stays — a future caller need not leave eight stores behind it — but no test covers it.
* **`sy_nrarg` is read nowhere** and is vestigial: exactly one argument arrives in a register on this
  machine, for any `narg >= 1`.
* **There is no read-only user page.** РЗ closes a page to reads as well as writes, so a closed text
  page would take the program's own constant pool with it. `estabur()`'s `xrw` argument, and `sep`,
  are accepted and ignored. A **pure** (`NMAGIC`) binary's text is therefore *shared* but still
  writable by every sharer, and `XWRIT` in `struct text` is the only thing that keeps a modified text
  from being silently discarded.
