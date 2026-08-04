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
make test                  # the fast tests (label `kernel' less `weekly')
make weekly                # the slow ones -- the tests that boot the kernel
make clean
```

The Makefile is a thin wrapper over the top-level CMake `build/` tree, and cross-compiles with
the **in-tree** tool targets, so a rebuilt `b6as` relinks the kernel with no `make install` in
between. The link takes `-lruntime` **alone** — the kernel defines its own `printf` in
[prf.c](prf.c) and calls no library routine.

`make` finishes by printing `b6size -w unix`: the image **must end below `054000`** (`KEND` in
[../include/sys/param.h](../include/sys/param.h)), because supervisor instruction fetch is never
mapped and the top of the unmapped space is spoken for — see the map below. The kernel is
archived into one link-pulled `libunix.a` so unused code is dropped; `besm6.o` must come **first**
in `OBJ` so its const contribution pins the interrupt/extracode vectors at their fixed addresses.
Header dependencies are **deliberately coarse**: `b6cc` and `b6cpp` implement no `-M` family,
so nothing can compute them, and [CMakeLists.txt](CMakeLists.txt) settles for
`file(GLOB KHDRS …/include/sys/*.h)` — every object depends on every system header. Adding an
`#include` to a source or a header therefore needs no bookkeeping at all; adding a **new header
file** needs a re-configure, the glob being evaluated at configure time. (The hand-maintained
`###` dependency list this paragraph used to describe went with the Makefile it sat at the foot
of, in `da35740`; `Makefile` here is now a nine-line wrapper over the top-level `build/` tree.)

Include blocks in these sources are **sorted, and carry no `// clang-format off`**. They used
to: v7's `sys/` headers required the caller to include them in dependency order, which a
formatter that sorts alphabetically destroys. Every header under `../include/sys/` now includes
what it uses, so the order is nobody's business — see [../include/README.md](../include/README.md),
and `sys/dir.h`'s head comment for the rule.

## The tests

These cover the image the build produces ([../root.manifest](../root.manifest) →
`root3072.disk`), each going one step past the last, which is what keeps the diagnosis apart.

| test | asserts |
|---|---|
| `fstest` | the superblock and root inode read through the real `md` driver, buffer cache and `sbcheck()`, strictly below the boot path |
| `boot` | process 1 leaves the kernel, execs `/etc/init`, which forks `/bin/sh`, and the shell **prompts** |
| `console` | a typed dialogue with that shell: erase, kill, a line longer than a clist block, `>/dev/tty`, `pwd`, `ls /bin`, and `^D` round through `/etc/rc` to the next prompt. **`DISABLED` — it fails about one run in three, always the same way, and nothing the guest does explains it; TODO.md task 35 owns it and `test/CMakeLists.txt` carries the measurement** |
| `session` | the shell **writes** — files, an inode past its direct blocks, `sync` — and the host then fscks the container and diffs what was written |
| `files` | the file-management set rearranges a tree and then re-permissions it; the modes and owners are diffed on the host, out of `b6fsutil -v -v` |
| `utils` | the clock moved and read back, an alarm delivered, a background process killed, a script branching on `test`, the first pipeline this image has run, and a Cyrillic word carried through `exece()` and through filename generation |
| `libtest` | the [../lib/test/](../lib/test/) programs run off `/usr/test`, each matching **the same `.expected` file `b6sim` is held to** (`memt` and `shellt` run here only) — and `iomove()`'s own counters say the bulk copy path carried it |
| `login` | multi-user: `/etc/ttys`, a getty per line, a password through `crypt(3)`, `/etc/utmp`, and `multiple()` respawning the getty a logout killed — all on Consul 1 |
| `multi` | **two people logged in at once**, one per Consul: root on `/dev/console` and guest on `/dev/tty1`, each shell naming its own terminal, guest's file read by root's shell, and two different non-zero owners on two terminal nodes at the sync |
| `swap` | the same kernel on a machine of **31 pages** (`phymem` deposited before the boot), running more processes than fit: `sched()`/`newproc()` swap through the drum, two processes share one text, and the counters say so |

`boot` attaches the pristine disk read-only — an assertion in itself — and the others each convert
their own copy at their own volume number, so no test ever writes a build artifact. The rest of
[test/](test/) exercises one kernel component at a time against a hand-built environment; see
"Writing a standalone SIMH test" below.

**Everything in that table but `boot` is `weekly` and does not run on every edit.** They all
hold the `simh_boot` resource lock — a typed dialogue drops characters under load, and
`login.ini`/`multi.ini` bind fixed ports besides — so they run strictly one at a time, about
seventy seconds of serial wall clock, which was the critical path of the whole suite.
`make test` here (and the top-level `make run`) now excludes the label; `make weekly` selects
it. **It is opt-in and not a pre-commit gate**: run it when this half of the suite is what you
want, not as acceptance criteria for a change. `boot`
stays in the daily suite because it costs a second and answers the question that matters most
after a kernel edit: does the thing still reach a shell prompt. The lock's own rationale, and
the measurement behind it, are in [test/CMakeLists.txt](test/CMakeLists.txt).

**`multi` is the one test with a host program of its own**, [test/ttyhost.c](test/ttyhost.c), and
the reason is worth knowing before the next test wants a second terminal. `attach tty25 console`
binds Consul 1 to the simulator's own stdin/stdout, which is what lets bare `expect`/`send` drive
it; Consul 2 is a mux line, and `vt_puts()` (`besm6_tty.c`) discards everything the guest prints to
it while nothing is connected. Two things had to be fixed in the simulator rather than worked
around: `vt_getc()` tested only `TMXR_VALID`, so a character injected by `send TTY:n,"…"` — which
SCP tags `SCPE_KFLAG` — was dequeued and dropped, making `send` a silent no-op on every BESM-6 mux
line; and `attach tty26 4199` does **not** bind line 26, `tty_attach()` passing the string to
`tmxr_attach()` so that the connection goes to the lowest free line, which is line 1. Even with
both fixed, a *listener* is wrong for a test: the accept happens in a poll that runs at most once
per second of host time, i.e. after `merge()` has forked the getty and that getty has prompted into
the void. So `ttyhost` binds the port before the simulator starts and `multi.ini` has the simulator
**dial out** (`Line=26,Connect=…`), which connects while the .ini is being parsed.

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

**Only the first of those two pages is copied**, and only as far as it is live — everything below
`r15`, since the stack grows up and the words above it are frames that have returned. `USIZE` is
the ceiling, not the amount; the count travels in the page as `u_stkdepth` and the contract is
written once, in [uarea.S](uarea.S). Measured here: 516 words at the shell's first prompt, 392
after the whole libc suite. The page above the saved one is stack **overflow**: the stack may grow
into it and run there correctly, but it is in no process image and no context switch saves it, so a
process that reaches `sleep()` or `swtch()` with `r15` above `076000` loses those frames
([TODO.md](TODO.md) task 31). That rule is written once, at `UBASE` in
[../include/sys/param.h](../include/sys/param.h).

**Mapping is enabled only inside a few short assembly brackets** — to touch a user page
(`copyin`/`copyout`/`fubyte`/…), and to reach a physical page above `0100000` (`copyseg`/`clearseg`,
and the u-area save/restore itself).

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
              struct user     (~142 words)   `u = 074000`, an absolute symbol
              kernel stack    (883 words, grows UP past 075777 into...)
              a switch copies as far as r15 has reached, ~half of it in practice
   076000   U AREA, overflow ------ 1024 words, saved by NOTHING -------------
              the stack may run here but must not SLEEP here (TODO.md task 31)
   0100000  end of the unmapped reach; everything above is the page pool

РП — the current process's map, 32 pages, loaded by sureg()

   page  0..     user text (physical page != 0), data, bss, break growing up
   page 28..31   user stack, base 070000, grows UP to the 0100000 ceiling
   unallocated pages: РП = 0 (non-executable) and РЗ bit set (no data access)

   The user gets all 32 pages. The u-area is not in this map — it is physical.
```

**`NBUF` and `NMOUNT` are one setting in two names, and between them they set the ceiling.** Every
mounted filesystem holds a buffer for its superblock for as long as it is mounted — `smount()`
takes it with `geteblk()` and only `sumount()` gives it back, and `iinit()` does the same for the
root — so `NMOUNT` of the `NBUF` buffers can be out of the cache at once. `geteblk()`/`getblk()`
*sleep* on an empty free list, so a cache sized under the mount table does not run slowly, it
stops. The pair was 10 and 2 and is now **16 and 8**: eight buffers left with every slot in use,
which is what the old pair left. Paying for it moved `KEND` down 3072 words, from `062000` to
`054000`, and the image is at `051245` — about **1300 words of headroom**, which makes `NBUF` and
not the next page of code the thing most likely to run the kernel into its own buffers.

### The kernel-variable table

**A user program cannot find a kernel variable by name any way but asking**, because there is no
kernel image on the root filesystem: `root.manifest` names no `/unix`, and [unix.ini](unix.ini)
has the *simulator* load one off the build host. So `nlist(3)` has nothing to open and is
deliberately absent from this libc; [kctl.c](kctl.c) carries a small table of the variables the
kernel publishes and `kctl(2)` reads it
([../doc/Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md) §2.5,
[../include/sys/kctl.h](../include/sys/kctl.h)). It costs about **390 words** of the headroom
above — thirty-three four-word rows and the handler — and it cannot fall out of step with the image
it is part of, every address being a link-time relocation of the real declaration rather than a
number written down.

**`KCTL_PSINFO` shares the file but not the table.** `ps` wanted three columns that live in the
u-area and so cannot be a row: a digest computed at the moment of asking has no address to
relocate. It is a fourth *operation* instead, dispatched beside `KCTL_LIST` — which likewise
names nothing — and the walk lives at the foot of [kctl.c](kctl.c). The kernel holds one record
on its stack and `copyout`s per slot, so it costs **no bss at all**; that mattered, the
alternative being to carry `p_comm` and a tick counter in `struct proc`, which is 750 words of
bss and grows with `NPROC`. What it buys is that `ps` opens no memory device and needs no
privilege — `/dev/kmem` and `/dev/mem` keep mode 0640, and `pstat -u` is the only reader left.

Three things about it belong here rather than in the interface header. **`ks_addr` is a `void *`
because nothing else compiles**: a static initializer on this compiler folds an address constant
from `&lvalue`, an array name and *address ± constant*, and from no cast at all, so `(int)proc`
and `(int *)proc` are both rejected and only a `void`-pointee field takes a bare object name
([../doc/Besm6_Data_Representation.md](../doc/Besm6_Data_Representation.md) §7 is the general
rule, discovered here). A `void *` is fat, so every read of the field goes through `ptrword()`.
**The `u`-area and `buffers` are not in the table** and must not be: they are absolute symbols
(besm6.S), `UBASE` and `BUFBASE` are in `<sys/param.h>`, and `lib/test/memt.c` reaches the u-area
with no lookup at all. And **the counts are not in the table either** — `NPROC`, `NTEXT`,
`MSGBUFS` and the rest are `<sys/param.h>` constants a user program already sees, so there is no
`nproc` variable to export and there must not be one. That last is the largest simplification
against RetroBSD's version of this mechanism, whose table has to carry `_nproc` and `_hz`
because its userland cannot see the constants.

**The table serves the value readers completely and the pointer-chasers only halfway.**
`KCTL_GET` copies a variable out, so a `dmesg` or an `iostat` opens no device; `ps` and `pstat`
resolve `p_textp` into an index in `text[]` and `u_ttyp` into one in `sc[]`, which is arithmetic
against a base address, and they read on through `/dev/kmem` — and through `/dev/mem` for a
u-area at `p_addr`, which is above `KREACH`. The u-area invariant below applies to them in full:
for the running process the truth is at `074000` and only `uhome` says which process that is.

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
| `uflush()`/`uload()` | save/restore the **live part** of the u-area's saved half across a context switch | steals virtual page 1 for the process's u home and virtual page 2 for the live u-area (the descriptor is derived from `UBASE`, not spelled, so it cannot drift from the geometry); both live in quartet 0, so one `mod 020` steals them and one puts them back. `uflush` measures `r15`, copies that far and records the count in the home at `u_stkdepth`; `uload` reads it back through the window before copying |

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
the next `resume()` would flush a dead process's u-area into core `malloc()` may already have
handed out.

`resume()` ([switch.s](switch.s)): if `paddr != uhome`, `uflush(uhome)`, then `uload(paddr)`, then
`uhome = paddr`. Only then restore r1–r7, r13, r15 from the label — which, being at `074000+n` in
*every* process, now names the incoming process's saved state. That constant is the whole trick.

**A flush also freezes a length.** Since task 30 `uflush()` copies only as far as `r15` has
reached, so it must be called from a frame at least as deep as every label armed in the page it is
saving — otherwise the frames in between are never written and the `resume()` that lands in one of
them returns onto a stack that does not exist. Every caller obeys it and two do so exactly:
`newproc()`'s `save(u.u_ssav)` and `uflush(a1)` run from the same frame with the same `r15`. That
is what `SLACK` in [uarea.S](uarea.S) is for, and the second clause at `xswap()` is where the rule
is written down.

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
* **A forged map's adjacent virtual pages may be physically adjacent too**, and then a test that
  crosses the boundary between them proves nothing — the copy comes out right even if the mapping is
  ignored entirely. `mmutest`'s two data pages are virtual 2–3 over physical 17–18, so `umem` puts
  its page-crossing case at the *text/data* boundary instead, virtual 1 → 2 over physical 21 → 17,
  and asserts the two pages are **not** neighbours before trusting the leg. Same argument as the
  round-trip bullet above, one level down: ask what about the machine the test would still need.
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
* **A rule armed after its data has gone by never fires**, and the run then stalls somewhere later
  rather than failing where the mistake is. With one stream that costs nothing — the simulator is
  stopped at every match, so the next rule is always armed before the guest can answer. With **two**
  (`multi.ini`) it is the whole design problem: while one line's `step` is running the other line is
  free to print. Sequence the two so that only one is ever talking, and make the hand-over a rule on
  the *other* line. Actions are pushed ahead of whatever is already pending (`sim_brk_setact`), so
  two rules matching at once is safe; a bare `send`, though, is issued at parse time, before `go`.
* **A second terminal is a mux line and needs a client**, `expect TTY:26,`/`send TTY:26,` rather than
  the bare forms, and the simulator dialling out to a host program rather than listening. See "The
  tests" above — the reasoning is all in `test/multi.ini`'s and `test/ttyhost.c`'s headers.
* **`send` DROPS A CHARACTER.** Under `CTEST_PARALLEL_LEVEL=8` `session` fails perhaps one run in
  four with `s: not found` — the shell really received `s` where the script sent
  `sh /etc/session`. Measured on an unmodified tree. Re-run before believing it. This bullet used
  to end "and it is not the kernel"; that half is **withdrawn** — the drop also happens on an idle
  machine, reproducibly, on the *first* character of a send issued straight out of an `expect`, and
  an input overrun in `scintr()` is one of the two candidate mechanisms. `console.ini` pays
  `send after=20000` to avoid it and its header says why. **`login.ini` pays `delay=` on top of
  that**, because a `getty` reading a name in RAW mode loses the *second* character too — one
  `read(2)` and one `write(2)` per character from user mode, where a shell in canonical mode takes
  the same stream whole. That is the first evidence favouring the overrun over the timing
  artifact; **[TODO.md](TODO.md) task 35** is still the measurement that would settle it, and
  `login.ini` is now the cheapest way to make the drop happen on demand.
* **Never end an `expect`/`send` file on a rule a bare prompt satisfies.** All the rules are armed
  at once, so when a stage stalls the run falls through to that one at the next prompt and reports
  PASS. `console.ini` was doing exactly that, and had been passing without running its last four
  stages; the drop above is what was stalling it. Its closing rule is a unique string now.
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
* **`make` is not enough before `ctest`: use `make test`, `make weekly` or the top-level `make run`
  — never a bare `ctest`.** `swap.ini` is *generated*
  from `unix.nm` by `genboot.cmake`, because the `phymem` it deposits and the three counters it
  asserts on are link-time addresses. That generation hangs off `build_tests`, which plain `make`
  does not build — so a kernel change followed by a bare `make; ctest` runs the **previous** kernel's
  addresses. The deposit then lands on whatever now lives at the old `phymem`, and the failure looks
  nothing like the cause: the banner reports a *full* machine (the squeeze silently missed) and the
  boot dies later with `exec : error 2`, as the corrupted word takes its toll. One `p_cpu` line in
  [clock.c](clock.c) moved every symbol after it and cost a stash-and-rebuild to diagnose. If a
  generated-`.ini` test fails right after a kernel edit, check the `.ini` before the kernel.
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

* **The machine has a second, slower clock, and this kernel deliberately does not use it.**
  `fast_clk()` (SIMH `besm6_cpu.c`) raises `GRP_TIMER` (ГРП bit 40) 250 times a second and, off the
  same counter, `GRP_SLOW_CLK` (ГРП **bit 10**) every fourth tick — 62.5 Hz. It is not one of
  `GRP_WIRED_BITS`, so `MOD_GRPCLR` dismisses it exactly as it does the fast one, and the historical
  OS used it (undocumented, a later addition) to prod terminal I/O. Taking it as the Unix tick was
  evaluated and **rejected**, for three reasons and one trap:
  * **There is no overhead to save.** A tick costs ~150 instructions end to end (`intrgate`,
    `extintr()`, `clock()`'s fast path). The whole boot to a shell prompt takes about **25 clock
    interrupts** — `boot` runs 1.16 s against `hello`'s 1.06 s process-startup floor, so ~0.1 s of it
    is simulation — and the heaviest test in the suite, `libtest`, takes ~3000 against a `TIMEOUT` of
    1200 s. Three quarters of nothing is nothing.
  * **62.5 is not an integer, and `HZ` must be** (`lbolt >= HZ`, `lbolt -= HZ`, `(1000*ms)/HZ` in
    `ftime()`, `CLOCKS_PER_SEC`). 62 makes `sleep`/`alarm`/`timeout` fire *early*; 63 makes `time`
    run 0.79% slow. Either way the tick stops being exact, which it is today — the change *creates*
    an error class, on a machine with no calendar to correct against, and drops `ftime()`'s
    resolution from 4 ms to 16 ms.
  * **`033 031` cannot forge bit 10.** The interrupt-imitation port is `GRP |= (ACC & BITS(24)) << 24`
    — bits 25–48 only. `test/uclock` delivers exactly one tick at a chosen instant by forging bit 40;
    on the slow clock it could not, and the one test of the tick path would have to wait on wall
    clock instead.
  * **The trap:** arming bit 10 while leaving `extintr()`'s `GRP_TIMER` arm in place leaves the kernel
    running on the 250 Hz bit, with the slow bit merely also dismissed — and **every test still
    passes**. Anyone revisiting this must write the ratio test (arm only the slow bit, poll bit 40
    unarmed, assert `fast == 4*slow ± 1`) *first*.

  What the slow clock would actually have bought — v7's `p_cpu` equilibrium — was taken instead by
  dividing in software, in the one place that wanted 60 Hz semantics: see `CPUTICK` in
  [clock.c](clock.c).
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
  `(caddr_t)(int *)w` is the fat pointer to byte #0 of word `w`. `<` and `-` between two `char *`
  both order and subtract correctly — each goes through `b$pdiff` — though `<` did not before the
  compiler's fix of 2026-06-17, which is why the older code here bounds its loops with `int` counts.
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
  maintaining them — and, since task C4c, converting a **second** drive's container is what made
  the per-controller buffer visible: `from_simh()` reports the volume number it finds in zone 0,
  which on a two-drive machine was only right because nothing wrote block 0. The superblock lives
  at block 0 now, so `dev/md.c` maintains the mark per *drive* (`mdvol[]`) instead;
  `cmd/mkfs/README.md` §2 is the account.
* **A DRUM ZONE THAT HAS NEVER BEEN WRITTEN IS A READ ERROR, not garbage.** SIMH's `besm6_drum.c`
  fails the short `fread` and raises the same `drum_fail` an *unattached* drum raises, which
  `dev/mb.c`'s `EXT_IOERR` poll cannot tell apart. A **hole inside** the container reads back as
  zeros with no error, because the file is sparse; only a read **past the highest zone ever written**
  fails — and with `-n` drums that start at zero length, that is exactly where the first grown image
  lands.
* **`s_isize` is the first data block, not a count of i-list blocks**, and **the free list must be
  built descending** — `alloc()` pops the superblock cache from the top, so an ascending build lays
  every file backwards across the platter while passing every self-consistency check.
* **The superblock's two totals are maintained, and something now checks them.** `s_tfree` and
  `s_tinode` are kept by `alloc()`, `free()`, `ialloc()` and `ifree()` (`alloc.c`) — v7 kept
  neither; RetroBSD's `sys/kernel/ufs_alloc.c` is the model. So a new path that hands out or
  reclaims a block or an i-number **without going through those four** silently drifts the totals,
  and it will not stay silent: `kernel/test/fsck` has the machine fsck the root it is running on
  and fails on a `COUNT WRONG IN SUPERBLK`, and every writing test ends with a host-side
  `b6fsutil -c` that faults the same thing. Note the asymmetry `ifree()` needs — it counts *before*
  its two early returns, because the i-node is free whether or not the `NICINOD` cache had room for
  its number. Nothing in the kernel *acts* on either total, so `sbcheck()` deliberately does not
  police them: a wrong one is a filesystem to check, not one to refuse to mount.
* **The v7 shell has no comment character, and a `:` line is still PARSED.** A backquote, an
  apostrophe, a parenthesis, a `$`, a `;` or a redirection inside what looks like a comment is a
  syntax error or a command run. This cost two round trips on `test/session.sh`; `../etc/rc` says it
  at length, and it binds anything written for the image.

## Known consequences, accepted

* **A context switch copies the u-area twice** (out to the old home, in from the new): as far as
  `r15` has reached each way, measured at 392–516 words of the 1024-word page (task 30). This is
  the cost of an unmapped kernel; in exchange the trap path costs *nothing* and `copyin` needs no
  window.
* **The dead tail of the live u-area belongs to whoever ran before.** `uload()` writes only the
  live part, so the words above `u_stkdepth` are the previously resumed process's kernel stack.
  `core()` dumps the whole `USIZE` page ([sig.c](sig.c)) and `ptrace`'s u-area window reads the
  same words, so both can show another process's dead frames. The dump size is part of the core
  file's layout and every offset past it would move, so this is accepted rather than fixed.
* **Kernel-stack frames above `076000` are not saved.** The overflow page is where a deep path's
  interrupt frames live, and interrupt handlers never sleep, so the measured workload loses nothing:
  at peak the stack reaches `076100`–`076177`, while the deepest `resume()` in a boot → `/etc/rc` →
  shell → `ls /bin` run had `r15 = 075302`, 318 words below the boundary. A path that sleeps deeper
  than 884 words would silently lose those frames. Task 31 is the one-line detector.
* **The u-area invariant is a footgun.** It has bitten twice, and a seventh site added later and
  forgotten will still be a very confusing bug. The whole rule lives in one block comment at
  `xswap()` in [text.c](text.c).
* **`copyin`/`copyout` toggle БлП per word** (~2× a plain copy), and they are **word-only on
  purpose** — `usermem.S`'s header names the three callers that pass a pointer built from an `int`,
  whose byte-offset field reads as byte #5 and which the `aax #077777` mask is what saves. The byte
  offsets are peeled a level up, in `copyinb`/`copyoutb` ([ucopy.c](ucopy.c)), so an unaligned
  `read`/`write` costs at most ten byte operations per 3072-byte block instead of 3072 — **but only
  when the two pointers are IN PHASE**, on the same byte of their respective words. Out of phase
  the transfer is still byte-by-byte, because every word of it straddles two on the other side and
  nothing short of a machine-language funnel shift (`asx`/`yta`, used nowhere in this kernel) will
  do. Measured under `libtest`: 87% of the bytes were already going through the bulk path, the
  in-phase remainder fell from 70,231 bytes to 871, and 94,805 bytes — 7.6% of the traffic — are
  still moved one at a time. `nioshift` is what says so on every run, and that is the open half.
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
* **The tick is four times v7's, and two things are scaled for it by hand.** `HZ` is 250 because the
  interval timer free-runs at 250 Hz and cannot be programmed, so a tick is exact but is not the
  sixtieth every v7 constant assumes. `p_cpu` accrues one tick in four (`CPUTICK`, [clock.c](clock.c))
  because its decay is per *second* and cannot move. (v7's other scaled-by-hand constant was
  `ttyoutput()`'s delay table; there are no delays any more — see below.)
  `CLOCKS_PER_SEC` in `<time.h>` is a hand-copy of `HZ` that `lib/libc/gen/clock.c` `_Static_assert`s.
  One consequence is **left unfixed**: `acct(2)`'s `compress()` ([acct.c](acct.c)) has a 13-bit
  mantissa, so a CPU time past 8191 ticks loses low bits — ~33 s here against v7's ~136 s. Nothing on
  the image calls `acct(2)` (`acctp` is set only by `sysacct()`), so it is recorded rather than
  scaled; the fix, when something does, is to divide by `HZ/60` on the way into `compress()`.
* **The terminal path is eight bits wide, and so, since task C11, is the shell.** `dev/sc.c` and `dev/tty.c` pass
  every byte whole in both directions, so the console carries UTF-8 — `/etc/motd` opens in Cyrillic
  and `kernel/test/multi`'s transcript asserts it. Two things follow. v7's **delays are gone**: bit
  0200 of a queued byte was a delay count and cannot also be data, and nothing on this machine has a
  carriage to wait for, so `ttyoutput()` computes columns only and `TIMEOUT`/`ttrstrt()` have no
  producer left. And **`0377` is refused on input**, being the raw queue's own delimiter and `CBRK`
  both — no UTF-8 byte is ever `0377`, so nothing is lost, but without the guard `t_delct` goes
  negative and the line wedges. The limit that used to sit above the kernel is gone: `/bin/sh`
  marked a quoted character with bit 0200 and `trim()` stripped it from every word, so `cat`
  carried Cyrillic and `echo` mangled it; task C11 moved that mark out of the character
  (`cmd/sh/defs.h`), and `kernel/test/utils` drives a Cyrillic argument through `exece()` and
  through filename generation. This also **couples the kernel to the simulator** — a `raw8` Consul line in `besm6_tty.c` must
  synthesise no parity and truncate nothing (plain `raw` keeps the authentic 7-bits-plus-parity
  contract and will not do), and against an older one the symptom is garbage on
  input from the first character typed.
* **The tail of an image grown by `expand()` reads back as zeros.** v7 promised nothing there and
  nothing reads it, but this machine cannot leave those blocks unwritten at all, so `xswap()` writes
  zeros — a contract stronger than v7's, asserted by `test/uswap` leg 0.
* **`dev/mb.c`'s `drainbrz()` cannot be made to bite, and `test/uswap` says so.** Deleting it leaves
  the suite green, structurally: the БРЗ is eight lines evicted by age, and between the last store
  that fills a swap page and the `033` lie far more than eight kernel stores. `dev/md.c` is where the
  same hazard *did* bite, because its sector header is stored two instructions before the exchange.
  The drain stays — a future caller need not leave eight stores behind it — but no test covers it.
* **`dev/md.c` maintains two of a half-zone's four service words, not all four.** The eight words at
  `030 + 8*ctlr` are a hardware-fixed address per *controller*; the driver keeps the half-zone's own
  address, and — since the superblock moved to block 0 — the volume's mark and number, per drive in
  `mdvol[]`. The **userid and the address checksum** are still whatever the last read of any drive on
  that controller left there. Nothing reads either: `from_simh()` checks the mark and the address,
  and SIMH computes neither. A drive written before anything has read it gets the old behaviour, the
  buffer's residue, which no path reaches. `cmd/mkfs/README.md` §2 is the account.
* **`sy_nrarg` is read nowhere** and is vestigial: exactly one argument arrives in a register on this
  machine, for any `narg >= 1`.
* **There is no read-only user page.** РЗ closes a page to reads as well as writes, so a closed text
  page would take the program's own constant pool with it. `estabur()`'s `xrw` argument, and `sep`,
  are accepted and ignored. A **pure** (`NMAGIC`) binary's text is therefore *shared* but still
  writable by every sharer, and `XWRIT` in `struct text` is the only thing that keeps a modified text
  from being silently discarded.
