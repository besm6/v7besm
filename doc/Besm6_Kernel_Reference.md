# The BESM-6 Unix v7 kernel: maintainer's reference

The Unix v7 kernel, retargeted to the BESM-6. Sources are derived from Robert Nordier's v7/x86
port; the upstream copyright is in the top-level [COPYRIGHT](../COPYRIGHT).

This file is the **reference behind the article**: how the kernel works is told in
[kernel/README.md](../kernel/README.md), and what is here is the rest of it — the design the port
settled on, the hardware rules that design obeys, and the facts that cost real time to establish.
Anything narrower belongs in the source comments and elsewhere in `doc/` — read
[Memory_Mapping.md](Memory_Mapping.md) before
touching memory management, [Intrinsics.md](Intrinsics.md) for how C reaches `002 «рег»` and
`033 «увв»`, [Unix_Context_Switch.md](Unix_Context_Switch.md) for the gates,
[Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md) for the machine assist, and
[Besm6_Peripherals.md](Besm6_Peripherals.md) before touching [dev/](../kernel/dev/).

## Building

```sh
cd kernel && make          # produces `unix' (BESM-6 a.out), unix.nm and unix.dis
make demo                  # boot it under SIMH (`besm6 unix.ini')
make test                  # the kernel tests (label `kernel')
```

The Makefile is a thin wrapper over the top-level CMake `build/` tree, and cross-compiles with the
**in-tree** tool targets, so a rebuilt `b6as` relinks the kernel with no `make install`. The link
takes `-lruntime` **alone** — the kernel has its own `printf` in [prf.c](../kernel/prf.c) and calls no
library routine. `make` finishes by printing `b6size -w unix`: the image **must end below `054000`**
(`KEND` in [../include/sys/param.h](../include/sys/param.h)) — see the map below. The kernel is
archived into one link-pulled `libunix.a` so unused code is dropped; `besm6.o` must come **first**
in `OBJ` so its const contribution pins the interrupt/extracode vectors at their fixed addresses.
Header dependencies are **deliberately coarse** — `b6cc` implements no `-M` family, so every object
depends on every `include/sys/*.h` through a glob, and a **new header file** needs a re-configure.

**The drums must be attached to exec anything**: they are `swapdev` ([conf.c](../kernel/conf.c)), and
`exece()` stages the argument list in swap before it ever touches the new image. With no drum that
`bread` comes back `B_ERROR` and every exec fails with `exec init: error 5` (EIO). Every `.ini`
that boots therefore says `attach -n drum0 …`.

---

## The design

**The kernel runs unmapped.** БлП = 1 and БлЗ = 1 the whole time the kernel is in control, so a
kernel address *is* a physical address, and the kernel occupies the **first 32 physical pages** —
everything below `0100000`, exactly the reach of an unmapped data access.

**РП always holds the current process's map.** The kernel never programs a map of its own, so a
trap costs nothing: the hardware forces БлП/БлЗ on at the vector, which is already the kernel's
mode. Entry and exit touch no mapping register.

**The u-area is the last two pages of the kernel space** — physical `074000`–`077777`, holding
`struct user` and, above it, the per-process kernel stack. `struct user` is therefore at a fixed
*physical* page and is **copied in and out on a context switch**. That is the price of an unmapped
kernel, and it is the one we pay.

**Only the first of those two pages is copied**, and only as far as it is live — everything below
`r15`, since the stack grows up and the words above it are frames that have returned. `USIZE` is
the ceiling, not the amount; the count travels in the page as `u_stkdepth`, the contract written
once in [uarea.S](../kernel/uarea.S). The page above the saved one is stack **overflow**: the stack may grow
into it and run there correctly, but no context switch saves it, so a process that reaches
`sleep()` or `swtch()` with `r15` above `076000` loses those frames. That rule is written once, at
`UBASE` in [../include/sys/param.h](../include/sys/param.h).

**Mapping is enabled only inside a few short assembly brackets** — to touch a user page
(`copyin`/`copyout`/`fubyte`/…), and to reach a physical page above `0100000`
(`copyseg`/`clearseg`, and the u-area save/restore itself).

```text
PHYSICAL, pages 0..31 — the kernel, addressed with БлП = 1 (no translation)

   0        const   (interrupt vector 0500/0501, extracodes 0550-0577, literal pool)
            text    (fetched unmapped: РП is irrelevant to it, always)
            data + bss
   ...      must all end below 054000 = KEND
   054000   BUFFERS ------ buffers[NBUF][BSIZE], NBUF*BSIZEW = 8192 words -----
              a fixed PHYSICAL area, not bss: the drum/disk controllers transfer
              to a physical address.  `buffers = BUFBASE', absolute, in besm6.S;
              main.c declares it `extern'.  Raising NBUF lowers KEND with it.
   074000   U AREA, saved half ---- USIZE words: the CEILING on a switch's copy
              struct user     (135 words)   `u = 074000`, an absolute symbol
              kernel stack    (890 words, grows UP past 075777 into...)
              a switch copies as far as r15 has reached, ~half of it in practice
   076000   U AREA, overflow ------ 1024 words, saved by NOTHING -------------
              the stack may run here but must not SLEEP here
   0100000  end of the unmapped reach; everything above is the page pool

РП — the current process's map, 32 pages, loaded by sureg()

   page  0..     user text (physical page != 0), data, bss, break growing up
   page 28..31   user stack, base 070000, grows UP to the 0100000 ceiling
   unallocated pages: РП = 0 (non-executable) and РЗ bit set (no data access)

   The user gets all 32 pages. The u-area is not in this map — it is physical.
```

**`NBUF` and `NMOUNT` are one setting in two names.** Every mounted filesystem holds a buffer for
its superblock until it is unmounted (`smount()` takes it with `geteblk()`, `iinit()` does the same
for the root), so `NMOUNT` of the `NBUF` buffers can be out of the cache at once — and
`geteblk()`/`getblk()` *sleep* on an empty free list, so a cache sized under the mount table does
not run slowly, it stops. The pair is **16 and 8**, which put `KEND` at `054000` against an image at
`051245`: about **1300 words of headroom**, which makes `NBUF` and not the next page of code the
thing most likely to run the kernel into its own buffers.

### The kernel-variable table

**A user program cannot find a kernel variable by name any way but asking**, there being no kernel
image on the root filesystem for `nlist(3)` to read — `root.manifest` names no `/unix`, and
[unix.ini](../kernel/unix.ini) has the *simulator* load one off the build host. So [kctl.c](../kernel/kctl.c) carries a
table of the variables the kernel publishes and `kctl(2)` reads it (`<sys/kctl.h>`,
[../doc/Unix_V7_System_Calls.md](Unix_V7_System_Calls.md) §2.5). It costs about **390 words**
and cannot fall out of step with the image, every address being a link-time relocation of the real
declaration rather than a number written down. Three things stay out of it: `u` and `buffers`,
which are absolute symbols with `UBASE`/`BUFBASE` in `<sys/param.h>`; the counts `NPROC`, `NTEXT`,
`MSGBUFS` and the rest, which are `<sys/param.h>` constants a user program already sees; and
anything computed, a digest having no address to relocate. `ks_addr` is a `void *` **because
nothing else compiles**, a static initializer folding an address constant only from `&lvalue`, an
array name, *address ± constant* and no cast at all
([../doc/Besm6_Data_Representation.md](Besm6_Data_Representation.md) §7).

**`KCTL_PSINFO` shares the file but not the table**: `ps` wants three columns that live in the
u-area, so it is a fourth *operation*, dispatched beside `KCTL_LIST`, walking the process table at
the foot of [kctl.c](../kernel/kctl.c) with one record on the kernel stack and a `copyout` per slot — **no
bss at all**, against 750 words of it had `p_comm` and a tick counter gone into `struct proc`. What
it buys is that `ps` opens no memory device and needs no privilege. The pointer-chasers are served
only halfway: `ps` and `pstat` still resolve `p_textp` and `u_ttyp` into indices by arithmetic and
read on through `/dev/kmem`, and through `/dev/mem` for a u-area at `p_addr` above `KREACH` — so
the u-area invariant below applies to them in full.

### Shared text, and what the paging store owes it

**A pure (`NMAGIC`) binary's const+text is one shared region**, loaded once and mapped into every
process running it; an impure (`FMAGIC`) one has none at all, `getxfile()` folding const and text
into data and forcing `ux_tsize` to 0, at which point `xalloc()` returns on its first line.
`b6_prog()`/`b6_libtest()` take a `PURE` keyword. "Pure" here means **shared, not protected**: what
`-n` buys is one copy of the text in core, which on a 31-page machine is the difference between
four processes and none.

**A swap slot must be written in full before it is read.** v7 relied on a never-written PDP-11
block reading back as whatever was on the platter: `expand()` raises `p_size`, swaps out only the
*old* size, and `swapin()` reads the new one back. Here an unreached drum zone is an **I/O error**
and `swap()` panics, so `xswap()` zero-fills the tail of the slot before it lets go of the core.

### The click is dead

v7's "click" has no place on a word-addressed machine. Every size and address in the kernel is
counted in **48-bit words** — `p_addr`, `p_size`, `x_caddr`, `x_size`, `u_tsize`, the coremap,
`USIZE` — and where the hardware needs a page, the value is a word address that is a multiple of
`PGSZ` (1024), the map builder shifting by `PGSH` (10). `ctob`/`btoc`/`ctod` are replaced by
`btow`/`wtob`/`pground`/`wtodb`; the swapmap still counts disk blocks (512 words), only the coremap
changed unit.

### The mapped brackets

Each is a short assembly routine that runs **entirely out of index registers**: while mapping is
on, the kernel's own data — including its stack — is not addressable, because virtual `074000`
then names the *user's* page 30.

| bracket | why | what it maps |
|---|---|---|
| `copyin`/`copyout`/`fubyte`/`fuword`/`subyte`/`suword` | reach a user page | nothing — the user's map is already loaded. The loop toggles БлП per word: read the user word mapped, store it to the kernel buffer unmapped. |
| `copyseg`/`clearseg` | reach a physical page above `0100000` | steals virtual pages 1–2 as windows (one `mod 020`), restores the quartet from `u.u_upt[]` afterwards |
| `uflush()`/`uload()` | save/restore the **live part** of the u-area across a context switch | steals virtual page 1 for the process's u home and page 2 for the live u-area, the descriptor being derived from `UBASE` rather than spelled so it cannot drift. `uflush` measures `r15`, copies that far and records the count at `u_stkdepth`; `uload` reads it back through the window before copying |

**Never virtual page 0.** A store to virtual address 0 is dropped and a load returns 0:
`mmu_store()`/`mmu_load()` test `addr == 0` *before* translation, so the black hole is in the
**virtual** address, whatever page 0 is mapped to. Pages 1 and 2 are also the cheap choice: they
share quartet 0 and their addresses (`02000`–`05777`) fit the 12-bit short address field. (The same
black hole is why a user image starts at **word 8** (`BADDR`), the a.out header hole occupying
words 0–7 — see `getxfile()` in [sys1.c](../kernel/sys1.c).)

An interrupt taken inside a bracket is harmless *for addressing* — the hardware forces БлП = 1 at
the vector — but not for `uload`, which is overwriting the page the handler's frame would be in, so
that bracket holds БлПр. Note that **`vtm N,0` writes БлПр along with БлП and БлЗ**: a bare
`vtm 2`/`vtm 3` *enables* interrupts as a side effect, and a bracket that wants them off says
`02002`/`02003` and restores PSW afterwards.

### Six hardware rules everything obeys

1. **РП/РЗ cannot be read back.** The map is a shadow table in memory: `u.u_upt[8]`, eight words,
   each carrying four РП descriptors *and* (in bits 21–28 of the even words) the matching РЗ byte —
   so `sureg()` is 8 × `рег 020+i` plus 4 × `рег 030+j` with no shifting.
2. **The kernel keeps БлЗ set (protection off).** РЗ is consulted even when mapping is off, against
   `addr >> 10`, so a kernel running unmapped with the previous process's РЗ loaded would fault on
   its own bss. The hardware sets БлЗ at the vector — never clear it, not even in a bracket.
3. **Drain the БРЗ write cache** — nine consecutive stores to physical 1–7 with mapping off.
   `drainbrz()` is the one routine in the kernel that **has to be assembly** ([brz.s](../kernel/brz.s));
   `test/mmutest` proves it is load-bearing. Stores made *unmapped* are tagged physical and survive
   a map change; stores made *mapped* are tagged virtual and do not. Three obligations:
   * **before every РП write** — the hazard is invisible under default SIMH and fatal under
     `set mmu cache`;
   * **before user code FETCHES a word the kernel wrote through the map** — the instruction path
     does not consult the write cache while a data load does, so a word `copyout()` has just written
     reads back correctly as data and fetches as garbage. `exec()` gets the drain for free from the
     `estabur()` after its `readi()`; `main()`'s icode copy and `sendsig()`'s trampoline word drain
     for themselves;
   * **before a DEVICE reads memory** — a write exchange transfers out of memory, not out of the
     cache; both mass-storage drivers drain on the write path. A *read* needs nothing.
4. **There is nothing to invalidate** — writing РП refills the TLB in the same instruction, so a
   stale translation is not a state the machine can be in. v7's `invd()` is deleted, not stubbed.
5. **A fault reports the faulting *page* (ГРП bits 5–9), and the saved PC points *past* the
   faulting instruction.** Anything that means to retry — stack growth — must back the PC up using
   `SPSW_NEXT_RK` and `SPSW_RIGHT_INSTR`. The two **arithmetic** causes are the exception and prove
   the rule: they advance the PC without setting `SPSW_NEXT_RK`, because the instruction completed
   rather than being prefetched past, so the guarded fix-up correctly does nothing and a returning
   handler makes progress. `kernel/test/ufpe`.
6. **A floating overflow or divide by zero is a FAULT, not a flag** — `SIGFPE`, ГРП 22–21 and 23–21
   (the second a superset of the first, so decode it first). **Underflow raises nothing** and
   becomes machine zero. There are no IEEE sticky flags and no trap a program can arm
   (`<fenv.h>`), so anything computing with `double` range-checks *before* the operation; a check
   afterwards has nothing left to check.

### The u-area invariant

The live u-area is at `074000`; the copy in the process's image at `p_addr` is stale between
switches. A kernel global `uhome` records whose home the live u-area belongs to, and `NOUHOME` (0)
says it has no home at all — the state `exit()` and a freeing `xswap()` leave behind, without which
the next `resume()` would flush a dead process's u-area into core `malloc()` may already have
handed out. `resume()` ([switch.s](../kernel/switch.s)): if `paddr != uhome`, `uflush(uhome)`, then
`uload(paddr)`, then `uhome = paddr`. Only then restore r1–r7, r13, r15 from the label — which,
being at `074000+n` in *every* process, now names the incoming process's saved state. That constant
is the whole trick.

**A flush also freezes a length.** `uflush()` copies only as far as `r15` has reached, so it must
be called from a frame at least as deep as every label armed in the page it is saving — otherwise
the frames in between are never written and the `resume()` that lands in one of them returns onto
a stack that does not exist. That is what `SLACK` in [uarea.S](../kernel/uarea.S) is for.

**Anything else that reads or frees the current process's image must flush first.** This is the
sharpest edge in the whole design; it has bitten twice, and both times the site was one the list
did not have. The complete rule — all six sites, and why the test belongs inside `xswap()` rather
than at its call sites — is written up **once**, in the block comment at `xswap()` in
[text.c](../kernel/text.c). Add to it there, not here.

---

## The tests

These cover the image the build produces ([../root.manifest](../root.manifest) →
`root3072.disk`), each going one step past the last, which is what keeps the diagnosis apart.

| test | asserts |
|---|---|
| `fstest` | the superblock and root inode read through the real `md` driver, buffer cache and `sbcheck()`, strictly below the boot path |
| `boot` | process 1 leaves the kernel, execs `/etc/init`, which forks `/bin/sh`, and the shell **prompts** |
| `multi` | ^D out of that shell and the rest of the way: `/etc/rc`, a getty per line of `/etc/ttys`, `crypt(3)`, and **two people logged in at once** on the two Consuls |
| `core` | the test pack mounted on `/mnt`, then one user program (`/mnt/test/coret`) typed at that prompt: `core()` dumping a real image and `ptrace(2)` reaching a real stopped child — the two places the kernel builds a user address out of an integer |

`boot` attaches the pristine disk read-only — an assertion in itself; `multi` and `core` write the
root, so each converts a copy of its own. The **test pack** ([../test.manifest](../test.manifest) →
`test3077.disk`) is the second filesystem, carrying the `lib/test` programs as `/test/*`; it is
attached `-r` on `md01` and mounted `-r`, so one copy serves every test and only `core` wants it.
Nothing mounts it automatically — `/etc/rc` has no line for it, deliberately. All three are **`RUN_SERIAL`**: they type at the guest on a step
budget, and SIMH drops characters out of a `send` when the host is oversubscribed. `core`
adjudicates itself — `coret` prints a verdict per line and the `.ini` fires on the first `FAIL`, so
no `.expected` on the host has to keep step with the image's layout; that is the pattern to copy
for anything else needing a *user* program under a real kernel.

### Writing a standalone SIMH test

Verification is under **SIMH** ([../doc/Simh_Simulator.md](Simh_Simulator.md)) via
`test/*.ini`: `b6sim` runs a user `a.out` with no kernel underneath and cannot exercise any of
this. Each test is a standalone BESM-6 program that links kernel objects against a hand-built
environment, plus a `.ini` that loads it, runs it, and asserts on the machine state afterwards.
`test/mmutest` is the model to copy. **Run every MMU test with `set mmu cache`**: the БРЗ
write-back hazards are invisible without it.

**`besm6.o` cannot go into a standalone test** — its `0500` vector reaches into the C kernel and
its `_start` seeds no stack. That is why every routine a test has to link lives in its own file
(`brz.s`, `uarea.S`, `seg.S`, `usermem.S`, `switch.s`, `syscall.c`, `sendsig.c`) and why the gates
are duplicated in the tests' own crt0s, which must **save the C register (М020)** across an
interrupt (`wtc`/`utc` arm the modifier for one instruction and `выпр` re-arms from whatever М020
holds — symptom: one lost store into the page-0 black hole, no fault) and must keep a gate's temp
cells in `.text`, reachable by a **bare 12-bit address**. A forged `uprog` **cannot use the literal
pool**, which is in the crt0's `.const` at *physical* page 0 while the program runs mapped at
virtual page 0.

What the existing tests cost to get right:

* **A forge test can link the real file under test**, and should where the decode itself is the
  subject: `usys` takes `syscall.c`, `usig` takes `sendsig.c`, and `ufpe` takes `trap.c` with
  stubs for the eight things it calls (`psignal`, `grow`, `panic`, `printf`, …), so a wrong arm
  fails the test instead of being read for. The one thing that has to be built for it is **`u`**:
  `u_stack` is the *last* member of `struct user` and the trap frame plus the C frames grow up out
  of the object, so a test that links a file reading `u.u_stack` must **reserve `u` in assembly**
  with a page of slack above it — a C `struct user u;` would have the frame overwrite the next bss
  object — and point the crt0's `ustkbase` cell at `u.u_stack` from `main()`, only C knowing that
  offset.
* **A round trip proves nothing about addressing** — a driver that put the data in the wrong place
  passes, having been consistently wrong twice. Leave *two different* patterns and read the region
  back whole. Likewise a forged map's adjacent virtual pages may be physically adjacent too, so
  `umem` asserts its two pages are **not** neighbours before trusting the page-crossing leg.
* **Write the bite test, then verify it bites**, and know which test proves which hazard — dropping
  `uload`'s post-copy drain passes `uswtch` and still fails `mmutest`. **When a hazard is a race,
  say so** instead of pretending a green test covers it: `main()`'s `drainbrz()` can be deleted and
  `boot` still passes, and the argument for it is a measurement written down at the call site.
* **`step N`, not `go`** — a broken switch or a lost gate *hangs* rather than failing, so every
  `.ini` uses `step 50000000`. Read a bite test on ACC, never on the halt PC. **A user program
  reports back through a deliberate data-protection fault, not `стоп`**, which in user mode
  re-dispatches as э63 and check-halts. A `.ini` reads a kernel variable with the bare
  `if <addr> <cond>`, and the word comes back **50 bits wide** (mask `&07777777777777777`).
* **A typed line is driven with `expect`/`send`**, `test/multi.ini` with `test/ttyhost.c` being the
  worked example — copy those two rather than re-deriving. Injected input arrives through
  `sim_poll_kbd()` ahead of the real keyboard, so a plain pipe into `besm6` is no substitute; a
  match halts the simulator, so each action must end with its own `step N`; all rules are armed at
  once, so **never end such a file on a rule a bare prompt satisfies** or a stalled stage falls
  through to it and reports PASS. A second terminal is a mux line and needs a client, and with two
  lines only one may be talking at a time.
* **`make` is not enough before `ctest`: use `make test` or the top-level `make run`.** `boot.ini`
  is *generated* from `unix.nm` by `genboot.cmake`, and that generation hangs off `build_tests`, so
  a bare `make; ctest` runs the **previous** kernel's addresses and the failure looks nothing like
  the cause. If a generated-`.ini` test fails right after a kernel edit, check the `.ini` first.
  `deposit phymem` before `go` is the memory-pressure knob, and the `user mem =` banner line is its
  receipt — `expect` it too, or a deposit that missed leaves the test on a full machine.

---

## Gotchas worth not re-deriving

* **The machine's second, slower clock is deliberately unused.** `fast_clk()` raises `GRP_TIMER`
  (ГРП bit 40) 250 times a second and `GRP_SLOW_CLK` (ГРП bit 10) every fourth tick, 62.5 Hz.
  Rejected as the Unix tick: **62.5 is not an integer and `HZ` must be**, so the change *creates* an
  error class on a machine with no calendar to correct against, and **`033 031` cannot forge bit
  10**, so `test/uclock` could no longer deliver one tick at a chosen instant. **The trap:** arming
  bit 10 while leaving `extintr()`'s `GRP_TIMER` arm in place leaves the kernel on the 250 Hz bit —
  and every test still passes, so anyone revisiting this must write the ratio test *first*.
* **The spl cookie is a PSW word, not a small integer**: never compare one against a level, never
  synthesize one, and never `splx(0)`, which would clear БлП/БлЗ and drop the kernel into its own
  user's address space. `splx()` uses `__besm6_setpsw()` and writes the whole mode word back,
  because the intrinsics with an immediate first argument demand a compile-time constant and
  `__besm6_maskpsw()` therefore cannot take a run-time level.
* **v7 packs flags into spare low bits of addresses, and this machine has none.** A BESM-6 address
  is a word index and is odd half the time, and a `char *` is **fat** — marker in bit 48, byte
  offset in bits 47–45 — so both halves of the class bite: `setregs()`'s `(*rp & 1) == 0` read bit 0
  of a signal disposition as "ignored", so every handler at an odd address survived `exec`;
  `iomove()`'s `(int)cp & (NBPW - 1)` masked the *word* address; `exece()`'s
  `(nc + NBPW - 1) & ~(NBPW - 1)` is not a rounding operation at all, `NBPW` being 6; and `core()`'s
  `u.u_base = 0` gave a base out of phase with the kernel buffer. The rule: **a flag in a pointer
  has to be a separate field, and a mask on `(int)ptr` is almost always wrong.** The spelling that
  fixes the pointer half is **`(caddr_t)(int *)w`**, which the compiler *converts* (ORing the marker
  in) where `(caddr_t)w` merely copies; `usermem.S`'s header carries the rest.
* **A stale toolchain silently reintroduces two fixed bugs**, both in the external c-compiler: the
  `b$` pointer helpers once kept working values in static `.bss`, so a clock tick landing inside one
  ran a handler whose helper overwrote them; and a shift by a *constant* was folded against a 32-bit
  width, so `1u << 36` gave `020`. `kernel/`'s link does not depend on `libruntime.a`, so after
  reinstalling it you must `rm build/kernel/unix` to force a relink. No test reliably bites either.
* **`DIRSIZ` moves `u_upt`.** `struct user` holds `u_dbuf[DIRSIZ]` and a `struct direct` ahead of
  the shadow page table, whose word offset [uarea.S](../kernel/uarea.S), [seg.S](../kernel/seg.S) and `test/mmutest.c`
  hardcode as `UPT` (b6as has no `offsetof()`). mmutest's check 13 exists for exactly this, which
  makes the MMU tests load-bearing for a filesystem change.
* **The SIMH disk container is not a flat block file**: each word is eight little-endian bytes with
  a two-bit tag above the 48, and eight service words are interleaved per zone (`b6fsutil -S`
  converts; `cmd/mkfs/README.md` §2 is the account). `dev/md.c` maintains two of a half-zone's four
  — its own address, and the volume's mark and number per drive in `mdvol[]`, primed by `mdopen()`
  reading block 0 — and leaves the userid and the address checksum as whatever the last read on that
  controller left there. Nothing reads either.
* **A drum zone that has never been written is a READ ERROR, not garbage.** SIMH's `besm6_drum.c`
  fails the short `fread` and raises the same `drum_fail` an *unattached* drum raises, which
  `dev/mb.c`'s `EXT_IOERR` poll cannot tell apart. A hole *inside* the container reads back as
  zeros, the file being sparse; only a read past the highest zone ever written fails — and with
  `-n` drums that start at zero length, that is where the first grown image lands.
* **The superblock's two totals are maintained, and something checks them.** `s_tfree` and
  `s_tinode` are kept by `alloc()`, `free()`, `ialloc()` and `ifree()` (`alloc.c`); v7 kept neither.
  A new path that hands out or reclaims a block or an i-number **without going through those four**
  silently drifts the totals, and every writing test ends with a host-side `b6fsutil -c` that faults
  it. Note the asymmetry `ifree()` needs: it counts *before* its two early returns. Nothing in the
  kernel *acts* on either total, so `sbcheck()` deliberately does not police them.

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new), as far as
  `r15` has reached each way — 392–516 words of the 1024-word page. In exchange the trap path costs
  *nothing* and `copyin` needs no window.
* **The dead tail of the live u-area belongs to whoever ran before.** `uload()` writes only the live
  part, so the words above `u_stkdepth` are the previously resumed process's kernel stack, and both
  `core()`, which dumps the whole `USIZE` page ([sig.c](../kernel/sig.c)), and `ptrace`'s u-area window can
  show them. The dump size is part of the core file's layout, so this is accepted, not fixed.
* **A USER stack overflow wraps rather than faults, and the kernel cannot see it.** The user stack
  is the top four pages and grows up; `grow()` extends it on a fault one page above, and past page
  31 there is nowhere to extend to. But the process never gets that far: a user address is 15 bits
  and the process owns all 32 pages, so a store past `077777` **wraps mod 2^15 onto word 0** — the
  program's own const image, mapped and writable. No ГРП cause is raised, `trap()` is never
  entered, and the program corrupts itself instead. There is no guard page to spare (32 pages
  *are* the address space) and no test can be written on this side of the boundary; a program that
  recurses on input has to bound itself, which is what `/bin/sh`'s `deepchk()` does since task C29
  ([../cmd/sh/README.md](../cmd/sh/README.md)).
* **Kernel-stack frames above `076000` are not saved.** The overflow page is where a deep path's
  interrupt frames live, and interrupt handlers never sleep, so the measured workload loses nothing
  — the deepest `resume()` in a boot → `/etc/rc` → shell → `ls /bin` run had `r15 = 075302`. A path
  that slept deeper than 884 words would silently lose those frames.
* **The u-area invariant is a footgun.** It has bitten twice, and a seventh site added later and
  forgotten will still be a very confusing bug. The whole rule is one block comment at `xswap()` in
  [text.c](../kernel/text.c).
* **`copyin`/`copyout` toggle БлП per word** (~2× a plain copy) and are **word-only on purpose** —
  `usermem.S`'s header names the three callers that pass a pointer built from an `int`, whose
  byte-offset field reads as byte #5 and which the `aax #077777` mask is what saves. The byte
  offsets are peeled a level up, in `copyinb`/`copyoutb` ([ucopy.c](../kernel/ucopy.c)) — align the
  **destination**, move the middle, peel the tail — so an unaligned `read`/`write` costs at most ten
  byte operations per block instead of 3072. Only the middle knows about phase: in phase it is
  `copyin`/`copyout`, out of phase a **funnel shift written in C**. `nioshift` means "out of phase",
  not "still to do", and must stay non-zero, because a bulk path never taken looks exactly like one
  that is.
* **`off_t` stays a byte count.** Word offsets would delete a `b$div` at every block crossing — but
  `BSIZE` is 3072 and not a power of two either way, the divide is one per block against 3072 bytes
  moved, and the change is user-visible in `read`/`write`/`lseek`. Do not re-open this without a
  measurement that contradicts it.
* **`time` is never seeded from a wall clock.** This machine has no clock-calendar a program can
  read, so the epoch starts at 0 and `iinit()` takes the superblock's `s_time`. `TIMEZONE` and
  `DSTFLAG` are therefore **0** rather than v7's US Eastern: an offset on top of an invented epoch
  says nothing, and zero makes `ftime()` agree with `b6sim`, which lets `lib/test/timet` be one file
  for both harnesses.
* **The tick is four times v7's, and two things are scaled for it by hand.** `HZ` is 250 because the
  interval timer free-runs at 250 Hz and cannot be programmed, so a tick is exact but is not the
  sixtieth every v7 constant assumes; `p_cpu` accrues one tick in four (`CPUTICK`,
  [clock.c](../kernel/clock.c)) because its decay is per *second* and cannot move, and `CLOCKS_PER_SEC` in
  `<time.h>` is a hand-copy of `HZ` that `lib/libc/gen/clock.c` `_Static_assert`s. One consequence
  is **left unfixed**: `acct(2)`'s `compress()` ([acct.c](../kernel/acct.c)) has a 13-bit mantissa, so a CPU
  time past 8191 ticks loses low bits. Nothing calls `acct(2)`; the fix, when something does, is to
  divide by `HZ/60` on the way in.
* **`profil(2)` is refused and `addupc()` does not exist.** The gate is real — v7's four arguments,
  `EINVAL` for any scale but the 0 or 1 v7 itself reads as "profiling off" ([sys4.c](../kernel/sys4.c)) — but
  there is nothing behind it, and accepting the call while recording nothing was the one outcome
  worse than failing. The userland half is missing and is not coming (no `monitor()`/`mcount()`, no
  `cc -p`, `prof(1)` not ported), and `b6sim` profiles with no kernel help. The kernel half would
  not be a transliteration in any case: v7's `addupc` indexes 16-bit shorts by a *byte* offset where
  a buffer here is **words**, and it would store into the user's buffer while the kernel is
  unmapped, from the clock interrupt, where it must not fault.
* **The terminal path is eight bits wide, and so is the shell.** `dev/sc.c` and `dev/tty.c` pass
  every byte whole in both directions, so the console carries UTF-8 — `/etc/motd` opens in Cyrillic
  and `test/multi`'s transcript asserts it. v7's **delays are gone**: bit 0200 of a queued byte was
  a delay count and cannot also be data, so `ttyoutput()` computes columns only and
  `TIMEOUT`/`ttrstrt()` have no producer. And **`0377` is refused on input**, being the raw queue's
  own delimiter and `CBRK` both — no UTF-8 byte is ever `0377`, so nothing is lost, but without the
  guard `t_delct` goes negative and the line wedges. This **couples the kernel to the simulator**: a
  `raw8` Consul line in `besm6_tty.c` must synthesise no parity and truncate nothing.
* **The erase character rubs out, because the terminal is a screen and not paper.** `CERASE` is `^?`
  (`sys/tty.h`) and a screen prints DEL as nothing, so `ttyinput()` echoes `"\b \b"` — `partab[]`
  classes 2, 0, 2 and net −1 on `t_col`. **The edit is still `canon()`'s**; `t_echoct` is what the
  two halves agree on, the columns *this layer* echoed on the current line, so an erase at the head
  of a line cannot rub out the shell's prompt. Cooked mode only; `getty` reads `RAW` with `ECHO` off
  and rubs out for itself. Two cases the count cannot get right, both **display only**: a tab, and
  `\` before the erase character.
* **The tail of an image grown by `expand()` reads back as zeros.** v7 promised nothing there and
  nothing reads it, but this machine cannot leave those blocks unwritten at all, so `xswap()` writes
  zeros — a contract stronger than v7's, asserted by `test/uswap` leg 0.
* **`dev/mb.c`'s `drainbrz()` cannot be made to bite**, structurally: the БРЗ is eight lines evicted
  by age, and between the last store that fills a swap page and the `033` lie far more than eight
  kernel stores. The drain stays — a future caller need not leave eight stores behind it — but no
  test covers it. `dev/md.c` is where the same hazard *did* bite.
* **`sy_nrarg` is read nowhere** and is vestigial: exactly one argument arrives in a register on this
  machine, for any `narg >= 1`.
* **`ptrace`'s single-step, request 9, is refused with `EIO`.** v7 sets the PDP-11 T-bit; this
  machine has only a pair of debug registers ([../doc/Memory_Mapping.md](Memory_Mapping.md)
  §13), and a breakpoint register is not a single-step: stepping with **ИБП** (`M[034]`) means
  decoding the instruction at the resume PC and arming its successor, and a conditional branch has
  two successors against one register. So request 9 costs an instruction decoder, and nothing is
  ported that would use it. The refusal is `procxmt()`'s ordinary one ([sig.c](../kernel/sig.c)), so the child
  stays stopped and the parent can continue it with request 7; `trap.c`'s `GRP_BREAKPOINT` arm
  stays, turning a match nothing armed into `SIGTRAP` rather than a panic.
* **There is no read-only user page.** РЗ closes a page to reads as well as writes, so a closed text
  page would take the program's own constant pool with it; `estabur()`'s `xrw` argument, and `sep`,
  are accepted and ignored. A **pure** (`NMAGIC`) binary's text is therefore *shared* but still
  writable by every sharer, and `XWRIT` in `struct text` is the only thing that keeps a modified
  text from being silently discarded.
