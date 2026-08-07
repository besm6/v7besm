# The BESM-6 Unix v7 kernel

This is Seventh Edition Unix running on a machine it was never meant for. The BESM-6 has no bytes:
the addressable unit is a 48-bit word, and an address is a word index. Its memory management is
eight write-only registers that cannot be read back. It has one vector for every internal fault
and reports the cause in a register instead. It has a write-back cache the program must drain by
hand, and it has no read-only page — a page is open to reads and writes together or it is closed
to both.

Almost all of v7 survives that. The file names are v7's, the algorithms are v7's, `sleep()` and
`wakeup()` and the buffer cache and the inode layer would be recognisable to anyone who has read
Lions. What was rewritten is everything that touched an address — and on this machine that turns
out to be the trap path, the context switch, the memory model, both mass-storage drivers, the
terminal, and about a dozen places where v7 packed a flag into a spare bit of a pointer and got
away with it.

Sources derive from Robert Nordier's v7/x86 port; the upstream copyright is in
[COPYRIGHT](../COPYRIGHT). This file is an account of how the kernel works. The maintainer's
version of the same ground — the rules to obey while editing, and the consequences the port
accepted knowingly — is [Besm6_Kernel_Reference.md](../doc/Besm6_Kernel_Reference.md).

## The machine underneath

Enough of it to read the rest.

The word is 48 bits and is the smallest thing that has an address, so `sizeof(int)` is 6 and a
pointer is a word index. Six characters pack into a word, big end first, which means a `char *`
cannot be a plain address: it is **fat**, carrying a marker in bit 48 and a byte offset in bits
47–45. The consequence runs through the whole port. `(caddr_t)some_int` is a bit copy and not a
conversion — the marker stays clear and the zero offset field reads as byte #5, a word's *last* —
so a kernel address destined for a `caddr_t` is spelled `(caddr_t)(int *)w`, which the compiler
actually converts.

Numbers are octal, and bits are numbered right to left from 1. There is no carry flag and no
condition code a system call can return through, so a failed call is −1 in the accumulator with
`errno` in r14. Arguments go in direct order with the *last* one in the accumulator and r14
holding the negative argument count.

The processor's mode lives in a handful of bits: **БлП** blocks address translation, **БлЗ**
blocks protection checking, **БлПр** blocks interrupts. The hardware forces all three on when it
vectors. There is no I/O address space and no channel program: a device is reached with
`033 «увв»`, which names a register through the instruction's effective address and carries data
in the accumulator, and it answers by raising a bit in **ГРП**, the interrupt register.

## Where the kernel lives

**The kernel runs unmapped.** БлП and БлЗ stay set the whole time it is in control, so a kernel
address *is* a physical address, and the kernel occupies the first 32 physical pages — everything
below `0100000`, which is exactly the reach of an unmapped access.

That one decision pays for itself at every trap and charges for itself at every context switch. It
pays because **РП always holds the current process's map**: the kernel programs no map of its own,
so entry and exit touch no mapping register and a trap switches nothing. It charges because the
u-area — `struct user` and the per-process kernel stack — must then live at a fixed *physical*
address rather than a fixed virtual one, and so has to be copied in and out when processes change.

Two areas are therefore fixed physical memory rather than bss, and both are absolute symbols in
[besm6.S](besm6.S) that C declares `extern`: the **u-area, two pages at `074000`**, and
**`buffers[NBUF][BSIZEW]` at `054000`**. The buffers are not bss because the drum and disk
controllers transfer whole zones to a *physical* address; a buffer the kernel could only name
through a translation would be no use to them. The kernel image must end below the buffers, and
that ceiling is derived rather than written down twice: `KEND == BUFBASE == UBASE − NBUF*BSIZEW`,
so raising `NBUF` lowers the ceiling with it and the two cannot silently disagree.

```text
PHYSICAL, pages 0..31 — the kernel, addressed with БлП = 1 (no translation)

   0        const   (interrupt vectors 0500/0501, extracodes 0550-0577, literal pool)
            text    (fetched unmapped: РП is irrelevant to it, always)
            data + bss
   ...      must all end below 054000 = KEND
   054000   BUFFERS ------ buffers[NBUF][BSIZEW], 16 * 512 = 8192 words -----------
              a fixed PHYSICAL area: the controllers transfer to a physical address
   074000   U AREA, saved half ---- USIZE words: the CEILING on a switch's copy ---
              struct user     (135 words)   `u = 074000', an absolute symbol
              kernel stack    (890 words, grows UP past 075777 into...)
   076000   U AREA, overflow ------ 1024 words, saved by NOTHING ------------------
              the stack may run here but must not SLEEP here
   0100000  end of the unmapped reach; everything above is the page pool

РП — the current process's map, 32 pages of 1 Kword, loaded by sureg()

   page  0..     user text (physical page != 0), data, bss, break growing up
   page 28..31   user stack, base 070000, grows UP to the 0100000 ceiling
   unallocated pages: РП = 0 (non-executable) and РЗ bit set (no data access)

   The user gets all 32 pages.  The u-area is not in this map — it is physical.
```

`NBUF` and `NMOUNT` are one setting under two names. Every mounted filesystem holds a buffer for
its superblock until it is unmounted, and the root holds one from `iinit()` onward, so `NMOUNT` of
the `NBUF` buffers can be out of the cache at once. `getblk()` and `geteblk()` *sleep* on an empty
free list, so a cache sized under the mount table does not run slowly — it stops. The pair is 16
and 8, which leaves about 1300 words of headroom under `KEND`, and makes `NBUF` rather than the
next page of code the thing most likely to run the kernel into its own buffers.

One v7 unit is gone entirely. The "click" has no meaning on a word-addressed machine, so every
size and address in the kernel — `p_addr`, `p_size`, `x_size`, `u_tsize`, `USIZE`, the coremap —
is counted in **48-bit words**, and where the hardware wants a page the value is a word address
that is a multiple of `PGSZ`. `ctob`, `btoc` and `ctod` are replaced by `btow`, `wtob`, `pground`
and `wtodb`. Only the swapmap still counts disk blocks.

## Cold start

The const section of [besm6.S](besm6.S) is laid out by absolute address, because the hardware's
vectors are absolute: `0500` for an internal fault, `0501` for an external interrupt, and one word
each at `0550`–`0577` for extracodes э50–э77. A literal pool fills the gap below them. This only
holds if nothing is merged in ahead of that segment, which is why `besm6.o` must come first in the
link and why the linker's `-T` must not be used.

`_start` has two halves separated by the whole of initialisation. The first is two instructions:
seed r15 at the base of the kernel stack and call `main()`.

`main()` ([main.c](main.c)) brings the machine up in an order where most of the steps are load-
bearing. `startup()` clears bss, publishes the memory size and frees core and swap into their
maps. Then process 0 is hand-crafted — and with it `uhome`, the cell that records whose u-area is
currently live, seeded to proc[0]'s image so that the first context switch cannot try to load a
home nobody has ever saved. `intrinit()` arms ГРП *before* anything can call `spl0()` and open the
interrupt level. `clkstart()` programs nothing, there being no timer to program. Then the buffer
cache, and `iinit()`, which reads the root superblock and — unlike v7 — checks it with `sbcheck()`
*before* installing it in the mount table and before setting the system clock from its `s_time`. A
garbage root used to run the system on a garbage date and was noticed only when something tried to
allocate.

`newproc()` then splits the two processes that the system is built on. Process 0 falls into
`sched()` and becomes the swapper, never returning. Process 1 gets the icode — nine words of
assembly that `exec` `/etc/init` — copied into its image, and then does something that reads oddly
and is exact: it copies the icode **to the address it was linked at**. Source and destination are
the same number, and only which side of БлП they are read and written on tells them apart. That is
what makes the icode's own labels correct in the user's address space too.

The copy is followed by a `drainbrz()`, and this is the first appearance of the machine's sharpest
hazard. The stores that `copyout()` just made were made *mapped*, and they are sitting in the БРЗ
write cache; the instruction path does not consult that cache. Measured with a breakpoint on the
next instruction: of the nine icode words, two had reached memory and seven were still in the
cache, with the user's page reading zero where they belonged.

Then `main()` returns, into the second half of `_start` — which shuts the interrupt door, points
r15 at the user stack base `070000`, loads the icode's entry into the return register, sets the
mode word to user with mapping and protection on, and leaves through `выпр`. It is the only entry
into user mode in the whole kernel that does not go out through the common interrupt epilogue.

## The four doors

Four vectors reach the kernel: the syscall extracode э77 at `0577`, the unimplemented extracodes
э50–э76 at `0550`–`0576`, the fault vector `0500`, and the external-interrupt vector `0501`. All
four gates are the same fifteen or so instructions, and the differences between them are worth
more than the similarities.

Each gate spills five registers the C world cannot see into fixed cells, switches to the kernel
stack, and fills a 21-word frame ([../include/sys/reg.h](../include/sys/reg.h)) describing the
interrupted context. The fill is where the doors are reconciled: an extracode saves its return
address in one register and a fault in another, so the syscall gate's fill reads ERET where the
fault gate's reads IRET, and everything downstream sees one frame shape. Dubna's monitor
reconciled the same two registers on the way *out*, with two extra instructions; doing it in the
fill costs nothing.

Only then does the gate open the interrupt level. The hardware forces БлПр on at the vector, and
БлПр *is* this kernel's interrupt priority, so without those three instructions a system call
would run to completion with the clock stopped and every device completion held off. They sit
after the fill and not one instruction earlier, because until the frame is complete the mode word,
the return register and the five spill cells are the only copy of the interrupted context there
is.

The external-interrupt gate is the one that differs. Its stack switch is *conditional*, since an
interrupt can arrive while the kernel is already running on the kernel stack, and it therefore
publishes its frame base in a separate cell rather than through `u.u_ar0`. On a clock tick nested
inside a system call, `u.u_ar0` still names the *syscall's* frame, whose mode word says user — and
`clock()` would charge the tick to user time.

The shared epilogue shuts the door as its first act, before anything else, and that is a
precondition it enforces rather than inherits: everything below that point reloads single
registers that the hardware overwrites the instant an interrupt is taken.

**Faults.** There is no trap kind to switch on. The machine has one internal-interrupt vector and
reports the cause as a bit in ГРП, so `trap()` ([trap.c](trap.c)) reads ГРП live and dispatches on
the bit — an enumeration in between would name the same five things twice. Five bits arrive here:
a data-protection violation, an instruction fetch from a closed page, a privileged instruction in
user mode, a word that is not an instruction, and an address-break match. Each arm dismisses its
own bit *first*, because a bit left standing could fire afterwards as a spurious external
interrupt.

Before any of that, the PC is backed up. The machine takes delivery of the next word of the
instruction stream before vectoring and saves *that* as the return address, so the framed return
is the faulting word plus one. Only the word needs correcting: the mode word already records which
half of it faulted, and `выпр` reloads that. Everything downstream — the stack-growth retry,
signal delivery, `ptrace` — then sees a frame describing the instruction that actually faulted.

Data protection is the interesting arm, because it is where this kernel takes a fault **on
purpose**. If the page reported is the one just above the stack, `grow()` extends the stack and
the trap returns to re-execute the instruction. Everything else becomes a signal. A fault taken in
supervisor mode never reaches `trap()` at all: the gate branches to `ktrap()`, which dumps
registers and panics, and which can be branched to rather than called precisely because it never
returns.

**System calls.** `syscall()` ([syscall.c](syscall.c)) needs no PC fix-up — the extracode gate
saved the address after the instruction by construction. It latches the call number out of r14
immediately, because that same frame slot is where `errno` goes on the way out, and it
**range-checks** rather than masks: the user can index-modify an extracode's effective address to
any 15-bit value, and a mask would fold an out-of-range number onto a real system call instead of
dispatching `nosys`.

Then it inverts the argument layout. The BESM-6 convention passes argument *k* of *n* at
`r15 − (n−k)` with the last one in the accumulator — descending in memory, and one of them not in
memory at all — while the thirty-odd callees read `u.u_ap` as an ascending array in prototype
order. Reconciling the two views is this function's real work, and it reads the words with
`fuword()`, since they are in user space.

Standing in for a called function, the gate also owes the caller the callee's stack cleanup, and
it pops those words **before** the dispatch rather than after: `exec()` reseeds r15 on success and
`sendsig()` builds a frame on the user stack, so a pop afterwards would corrupt the first and come
too late for the second.

## Signals

Delivering a signal to a user handler means building a frame the handler can return through, in
the user's own stack, from the kernel.

`sendsig()` ([sendsig.c](sendsig.c)) grows the stack for the frame's last word, copies all 21
words of the interrupted context out to the user stack, and plants one more word above them: a
single `$77 SYS_sigret` — assembled in [besm6.S](besm6.S) rather than written down as an opcode,
and forced to word alignment so the extracode lands in the left half. Then it drains the БРЗ,
because that word is one the handler *executes* and the store that planted it was made mapped. The
21 frame words below it need no such care: `sigret()` reads them back as data, and a data read
does consult the cache.

The handler's entry registers are then forged — the signal number in the accumulator, the
trampoline word as the return address, and R set to the value the calling convention expects at
function entry, which is *not* what `exec` leaves, because there the first instruction of crt0
sets it and a handler has no crt0 in front of it.

`sigret()` is a system call and not a few instructions in libc, and it has to be. Which half of a
word to resume at lives in the saved mode word, and only `выпр` reloads it — so a signal taken on
a right-half instruction, which every fault can be, can only be resumed by the kernel. On the way
back in, exactly two bits of the user's forged frame are honoured; the rest of the mode word is
taken from the kernel's.

## The address space

The MMU is eight write-only page registers and a four-register protection set, over 32 virtual
pages of 1 Kword. Nothing can be read back, so **`u.u_upt[8]` is the only copy of the mapping
there is** — the hardware registers are a write-only cache of it, reloaded in twelve instructions
by `sureg()` ([utab.c](utab.c)).

The two hardware packings are complementary by design, and that is what makes the shadow table
cheap. Page descriptors occupy accumulator bits 1–20 and 29–48; the protection byte occupies bits
21–28. So one word carries a quartet of page numbers *and*, in the even words, the protection byte
of eight pages, and neither write needs a shift. A descriptor is not packed but *scattered*, and
`aux` is a scatter instruction: a page number left-aligned to bit 48 deposits into exactly the
right positions, and `apx` gathers it back.

`sureg()` walks text, then data, then the stack, out of the process's image. Text may live in a
different core run when it is shared; data and stack are contiguous with the u page at the head of
the image. The stack sits at virtual page 28 and grows *up* toward `0100000`, so there is a hole
in the virtual space between the end of data and `070000`, while the image itself stays one
physical run.

**Text is left open to data**, and that is not an oversight. This machine has no read-only page —
the protection register closes a page to reads as well as writes — and a closed text page would
take the program's own constant pool with it. So "pure" in this port means *shared*, not
protected: what a pure binary buys is one copy of the text in core, which under memory pressure is
the difference between four processes running and none. `estabur()`'s read/write argument, and
`sep`, are accepted and ignored.

Because the stack grows up and its physical pages are the *tail* of the image, growing it appends
a page at the top of the virtual space and at the end of the image simultaneously. Every page
already in the stack keeps exactly the address it had. That is why `grow()` has no shuffle in it
at all — nothing has to move.

Writing the map from the kernel is safe for the reason everything else here is safe: the kernel
runs unmapped, so changing the map changes nothing about how the kernel addresses its own memory.

## Reaching across the boundary

The routines that let the kernel touch a page it cannot address directly divide into two kinds,
and the difference between them is where the window comes from.

`copyin`, `copyout` and the single-word and single-byte fetches ([usermem.S](usermem.S)) need no
window at all: the user's map is already loaded. They simply toggle БлП per word — read the user's
word mapped, store it to the kernel buffer unmapped, or the reverse. That asymmetry is the whole
trick, and it is a property of the hardware: supervisor *instruction* fetch is never mapped, but
supervisor *data* fetch obeys БлП. There is no fault-recovery path behind any of them, either;
each validates its whole range up front with `useracc()`, so v7's `nofault` machinery disappears.

They are word-only on purpose, masking the pointer down to its word field, which is why byte phase
is peeled one level up in `copyinb`/`copyoutb` ([ucopy.c](ucopy.c)): align the destination, move
the middle, peel the tail. Only the middle knows about phase — in phase it is `copyin`/`copyout`,
out of phase a funnel shift written in C. An unaligned `read` or `write` therefore costs about ten
byte operations per block rather than 3072.

The other kind — `copyseg`, `clearseg`, `copyphys` ([seg.S](seg.S)) and the u-area save and
restore ([uarea.S](uarea.S)) — must reach a *physical* page above `0100000`, which no unmapped
address can name. Each steals virtual pages 1 and 2 as windows, saves the quartet it is
overwriting, and puts it back. While mapping is on, the kernel's own data is unreachable — virtual
`074000` then names the user's page 30 — so these routines run entirely out of index registers.

**Never virtual page 0.** A store to virtual address 0 is dropped and a load returns 0, and the
test is on the *virtual* address, before translation, whatever page 0 happens to be mapped to. A
window there would silently lose word 0 of whatever it was copying. The same black hole is why a
user image starts at word 8 rather than word 0, with the a.out header's hole occupying the words
below it, and why the base of a segment read is never spelled `(caddr_t)0`.

### The write cache

БРЗ is eight write-back lines, and draining it is `drainbrz()` ([brz.s](brz.s)) — nine consecutive
stores to low physical addresses, the first of which only arms the eviction counter. It is the one
routine in this kernel that *has to* be assembly: the nine stores must be consecutive, any
ordinary store between them resets the counter, and the compiler spills the destination pointer
through a frame slot.

The rule behind every call site is one sentence: **stores made unmapped are tagged physical and
survive a map change; stores made mapped are tagged virtual and do not.** From it follow three
obligations. Drain before every write to the mapping registers, or a mapped store still in the
cache lands in the wrong space — a hazard invisible under a default simulator and fatal with the
cache modelled. Drain before user code *fetches* a word the kernel wrote through the map, because
the instruction path does not look in the cache while a data load does. And drain before a device
reads memory, because a write exchange transfers out of memory and not out of the cache. A read
needs nothing.

There is nothing to invalidate, on the other hand: writing a mapping register refills the
translation in the same instruction, so a stale translation is not a state this machine can be in.
v7's `invd()` is deleted rather than stubbed.

## The context switch

`resume()` ([switch.s](switch.s)) switches the **u-area**. It never writes the map — the address
space is reinstalled by `sureg()` at each landing site, and every surviving v7 comment that says
otherwise is wrong.

`save()` records nine words: r1–r7, the return address, and r15. `resume()` parks its own two
arguments in static cells before it does anything else, because those arguments are on the stack
it is about to overwrite, and it holds the interrupt level across both copies, because an
interrupt landing between them would build a frame on a kernel stack that has just been flushed
and is about to be replaced.

Then, if the incoming image is not the one whose u-area is live, it flushes the outgoing one to
its home and loads the incoming one, and only then reloads the nine registers from the label —
which, being at a fixed physical address in *every* process, now names the incoming process's
saved state. That constant is the whole trick.

`uflush` and `uload` ([uarea.S](uarea.S)) are the same bracket in opposite directions, windowing
the process's home at virtual page 1 and the live u-area at page 2. **Only the live part moves.**
The stack grows up, so everything above r15 is frames that have already returned; `uflush`
measures r15, copies that far, and records the count *inside the page it is describing*, where
`uload` reads it back through the window. `USIZE` is the ceiling, not the amount — in practice
about 400 to 500 words of the 1024 travel each way. The page above the saved one is stack
overflow: the stack may grow into it and run there correctly, but nothing saves it, so a process
must not sleep with r15 up there.

### The u-area invariant

This is the price of an unmapped kernel, and the sharpest edge in the design. The live u-area is a
fixed physical page; it is *not* part of the current process's image, so the copy sitting in the
image at `p_addr` is stale between context switches. `uhome` names the image the live one belongs
to, and `resume()` keeps the two in step.

Which means anything else that reads or frees the current process's image has to say so — swapping
it out, copying it to build a child, moving it to a new address, freeing it at exit. There is a
second clause, too: because a flush also freezes a *length*, it must be called from a frame at
least as deep as every label armed in the page it is saving, or the frames in between are never
written and the `resume()` that lands in one of them returns onto a stack that does not exist.

The whole rule — all five sites, both clauses, and why the test belongs inside `xswap()` rather
than at its call sites — is written once, in the block comment at `xswap()` in [text.c](text.c).
It has bitten twice, both times at a site the list did not have. Add to it there.

## A process's life

**Fork.** `newproc()` ([slp.c](slp.c)) copies the parent's image to build the child's, and the
parent's u page in that image is stale — the live one is elsewhere — so it flushes first, or the
child inherits a stale label and never returns from the `save()` just above. The window for that
flush is exact in two senses: it is after the `save()`, and inside the bracket where `u.u_procp`
already names the child, because the environment partially simulated there is precisely what the
child is meant to inherit. It is also the pair with the least room in the kernel — `save()` and
`uflush()` are called from the *same frame*, each with one argument, which this ABI passes in the
accumulator with nothing pushed, so the r15 the child will resume on and the r15 the flush
measures are the same word. Move either call into a helper and the child's stack is truncated
below its own label.

**Exec.** `exece()` ([sys1.c](sys1.c)) stages the argument list in *swap blocks* before it touches
the new image, which is why the drums must be attached to exec anything at all: they are
`swapdev`, and with no drum every exec fails with EIO. Walking the caller's strings has to be done
with a real `char *` — the fat pointer's byte offset is the thing being walked, and stepping the
underlying integer would read one byte in six.

The block is copied back to a fixed base, `070000`, which is where the stack begins and grows up
from. So `argc` is always at that absolute address, which is how a crt0 finds it with no register
hand-off, and the program's own stack growth can never walk back over its own arguments. The
`argv` entries stored there must be fat pointers: a plain word address asks for a shift of −64,
and the user's first dereference would read zero.

`getxfile()` reads the header, checks every size against zero directly — v7 caught an over-large
header by letting a sum overflow a 16-bit field, and with every field a whole word here that
comparison could never fire — and then builds a trial map before it commits anything. Impure
binaries fold const and text into data, at which point the shared-text machinery skips itself on
its first line. The data segment is read in *where it will live*, and the base for that read is
never word 0, for the black-hole reason above.

**Shared text.** A pure binary's const and text are one region, read once and mapped into every
process running the same inode. `xalloc()` reads it in, `xccdec()` writes it back to swap when the
last sharer in core lets go, and the swap image and the inode survive as the sticky-text cache.
Since text is writable — there being no read-only page — `XWRIT` is the only thing that keeps a
modified text from being silently discarded.

**Swapping.** `sched()` is process 0: pick the process that has been out longest, try to bring it
in, and if there is no core, choose a victim — preferring the largest process asleep at a
sleepable priority, otherwise the oldest — and put it out. `expand()` moves an image to a larger
run, copying from the *second* page, because page 0 of the image is the stale u home and the live
copy is authoritative; instead it moves the home itself and lets the next switch reach it.

`xswap()` does one thing v7 never had to. A swap slot here **must be written in full before it is
read**: `expand()` raises the size and swaps out only the old one, and on the PDP-11 the tail came
back as whatever was on the platter. On this machine a drum zone that has never been written is
not garbage, it is an **I/O error** — the container grows only as far as the highest zone ever
written — and the read panics. So `xswap()` zero-fills the tail of the slot before it lets go of
the core, which is a contract stronger than v7's and one that something depends on.

## Time and priority

The interval timer free-runs at 250 Hz from reset and cannot be programmed, so `HZ` is 250 and
`clkstart()` only dismisses whatever tick was already pending. An exact tick, but not the sixtieth
that every v7 constant assumes — and one of those constants had to be rescaled by hand.

`p_cpu` rises on a tick and decays by 8/10 once per *second*, so the value a process settles at is
the fixed point of that feedback loop, which is four times its tick rate. At v7's 60 Hz a CPU hog
settled at 240, comfortably inside the 8-bit field. At 250 Hz the fixed point is 1000, and 255 is
reached at a quarter of the CPU: every process busier than that pins the counter and `setpri()`
cannot tell any two of them apart. So [clock.c](clock.c) accrues `p_cpu` one tick in four, which
puts the effective rate at 63/s and the fixed point at 252 — back inside v7's band. The test is a
mask and not a modulo, because a modulo by anything but a power of two is an out-of-line call
here.

The rest of a tick is v7's: run the callout list, charge the time to user or system, and once a
second bump the clock, age every process, fire the alarms and re-price everything above the user
priority. The one thing worth knowing about `dk_time` is that it is a **histogram** and not four
counters — the low bits of the subscript are the set of busy devices and the high bits the CPU
state, so a reader that samples only the four CPU states silently drops every tick taken while a
disk was busy.

The interrupt priority model has exactly **two levels**, not v7's eight. БлПр is the priority;
ГРП's mask register is the per-source enable, and the two do different jobs. An spl cookie is
therefore a whole mode word, not a small integer: never compare one against a level, never
synthesise one, and never `splx(0)`, which would clear БлП and БлЗ and drop the kernel into its
own user's address space.

## The filesystem

The inode layer, pathname lookup and the buffer cache are v7's, with the units changed underneath
them. A block is `BSIZE` = 3072 bytes, which is `BSIZEW` = 512 **words**, and the cache moves word
counts. `NADDR` is 8 — six direct, one indirect, one double — and the triple indirect is gone as
unreachable: a drive is 2000 blocks, and with 512 addresses to a block the double already reaches
262,144 of them. Dropping it is what frees the slots to make an inode 16 words.

Where the port bites is that **3072 is not a power of two**. There is no `BSHIFT` and no `BMASK`,
and there cannot be; v7's pair described a 512-*byte* block and, carried into this port unchanged,
made every byte-offset-to-block conversion silently wrong by a factor of six. So a block crossing
in `readi`/`writei` is a real divide and remainder — one per block, which is noise beside the read
it precedes. The directory scan recovers the shift and mask a different way, by working in **entry
numbers** rather than byte offsets: a directory entry is four words, so entries tile a block
exactly and both the block number and the slot within it are a shift and a mask again.

`DIRSIZ` is 18, and it is not a free parameter: `struct user` holds a name buffer and a directory
entry ahead of the shadow page table, whose word offset the assembly brackets hard-code. Changing
it moves `u_upt`, which is why an MMU test is load-bearing for a filesystem change.

`off_t` stays a **byte** count. Word offsets would delete a divide at every block crossing, but
3072 is not a power of two either way, the divide is one per 3072 bytes moved, and the change
would be visible in `read`, `write` and `lseek`.

Two things are new. `sbcheck()` ([alloc.c](alloc.c)) refuses a superblock this kernel cannot use —
magic, then the geometry triple, then the sizes — because without it a garbage block mounts
silently and the first symptom is a repair that turns garbage into a plausible-looking full
filesystem. And the superblock's two totals, `s_tfree` and `s_tinode`, are *maintained*, where v7
kept neither: a new path that hands out or reclaims a block or an i-number without going through
the four routines that keep them will drift them silently. Nothing in the kernel acts on either,
so `sbcheck()` deliberately does not police them — a wrong total is a filesystem to check, not one
to refuse.

`iomove()` ([rdwri.c](rdwri.c)) is the worked example of the whole class of bug this port had to
find. v7 asked whether the count and the pointer were both word-aligned with a low-bit mask, which
is exactly right on a PDP-11 where the word is two bytes and an address is a byte address. Neither
half survives here: `NBPW` is 6 and a mask is not a remainder, so the test accepted 24 and 26
alike and rejected 6 and 18; and a `caddr_t` is fat, so masking the integer tested two bits of the
*word address* and could not see the byte offset at all. The guard passed on unaligned buffers
about one time in eight, and the word-only copy behind it then wrote the data up to five bytes
early with the tail never written. The test is gone; the byte-level routines decide for
themselves, one word at a time.

Clists have the same shape of story. v7 keeps two `char *` cursors into a character block and
rounds them to the block base with a mask; on a fat pointer that masks five bits of the *word*
address and says nothing about which of the word's six characters the cursor names. So a cursor
here is a block pointer plus an index, and the boundary test is an equality.

## The devices

A driver on this machine is short, because there is nothing to program. One instruction names a
controller register through its effective address and carries the whole command in the
accumulator, and the device answers by raising a bit in ГРП. What replaces the complexity is a set
of rules about *when* the instruction may be issued.

**The disk** ([dev/md.c](dev/md.c)) takes four instructions: select the group, select the drive,
send the control word — at which point nothing has happened yet — and send the track address,
which is what actually transfers. The order is not the documentation's, and the reason is the ГРП
bit: it is *wired*, meaning it stands whenever the drive is idle rather than latching on
completion, so it may only be armed around a live exchange. Both selects raise it and only the
control word lowers it, so issuing the control word first would leave it standing at the moment of
the arm and the driver would take a completion for a transfer still running.

A write must also supply the sector header, because the exchange moves the zone's service words
along with the data in both directions. Two of the four are the driver's — the sector's own
address, and the volume's mark and number — and the second cannot come from the controller,
because the service-word buffer belongs to the *controller* and not the drive. So the driver keeps
each drive's own label in a table, primed by reading it at open. Without that, a write to one pack
puts back whatever the last read of another pack left there.

The question that decides everything else is whether an exchange was **refused before it began or
attempted and abandoned**, because that is what says whether an interrupt is still on its way. An
unattached drive, or a write to a read-only one, is refused: fail it on the spot, and do not arm,
because arming is exactly the hang the error poll exists to prevent. A checksum failure is
attempted: consume the completion first, then retry.

**The drum** ([dev/mb.c](dev/mb.c)) is the paging store, and the two controllers are presented as
one linear space of 1024 blocks, so a block number alone says which drum. A whole aligned zone
goes in one exchange; anything else is chained sector exchanges, because swap space is handed out
in blocks and nothing rounds an exec's argument staging to a zone boundary. Both drivers drain the
write cache before a write exchange and neither does on a read.

**The terminal** ([dev/tty.c](dev/tty.c), [dev/sc.c](dev/sc.c)) is eight bits wide in both
directions, and that decision propagates. The authentic Consul character set has no lowercase
Latin, which a Unix console cannot do without, so the line is configured raw and the bytes go out
untranslated — ASCII below `0200`, UTF-8 above it, and `/etc/motd` opening in Cyrillic. It costs
the parity bit, and it couples this driver to the simulator, which must synthesise no parity and
truncate nothing.

Two things follow from bit 8 being data. v7's output **delays are gone**: a queued byte above
`0177` used to be a delay count, and it cannot also be a character, so the output side computes
columns and nothing else — there is no carriage on this machine to wait for anyway. And `0377` is
**refused on input**, being the raw queue's own line delimiter and the break character both; no
UTF-8 byte is ever `0377`, so nothing is lost, but without the guard the delimiter count goes
negative and the line wedges.

The erase character rubs out, because the terminal is a screen and not paper. v7 echoed the erase
byte and let the paper keep the record; a screen prints it as nothing, so from the first erase the
line the eye reads and the line the canonicaliser will hand the program drift apart. Here the echo
is `"\b \b"` — back up, blank the column, back up again. The editing half is still the
canonicaliser's, and it is UTF-8-aware, backing over continuation bytes to a lead byte; the two
halves agree through a count of the columns *this layer* echoed on the current line, which is what
keeps an erase at the head of a line off the shell's prompt.

## Asking the kernel about itself

There is no kernel image on the root filesystem for `nlist(3)` to read, so `ps`, `vmstat` and
`pstat` cannot find a kernel variable by name the way v7's did. `kctl(2)` ([kctl.c](kctl.c)) is
the answer: a table of the variables the kernel publishes, where every address is a link-time
relocation of the real declaration pulled in through the real header. Renaming or deleting one of
those variables stops this file compiling, which is a promise a `/unix` on the disk could never
have made. It is not the kernel's symbol table — every row names the program that reads it, and a
row nobody reads is a promise kept for nothing.

Three things stay out of it: the two absolute symbols, whose addresses are already constants in
[../include/sys/param.h](../include/sys/param.h); the table sizes, which are constants a user
program already sees; and anything computed, a digest having no address to relocate.

Which is why `ps`'s three u-area columns are an *operation* rather than a row. It walks the
process table in the kernel, with one record on the stack and a copy-out per slot — no bss at all,
against 750 words of it had those columns gone into `struct proc` — and what it buys is that `ps`
opens no memory device and needs no privilege. The subtle part is that a u-area is in one of three
places: live at the fixed physical page if it belongs to the current process, at the head of the
image if the process is in core, and on the paging store if it is not. Getting that wrong prints
numbers a context switch old.

## How it is proven

Everything above is verified on the real simulator, not on the user-level one — `b6sim` runs an
a.out with no kernel underneath and cannot exercise any of this.

Most of the coverage is standalone BESM-6 programs that link the **real** kernel routines against
a hand-built environment and assert on machine state afterwards. That is why the assembly brackets
and the two dispatchers live in their own files rather than in [besm6.S](besm6.S): a test can link
`switch.s` or `syscall.c` on its own, and cannot link `besm6.o`, whose vector reaches into the
whole C kernel. Between them they cover the MMU and the mapped windows, the byte-phase copies, all
four gates, the timer tick, the context switch, the scheduler, a real image through the real drum,
and both mass-storage drivers.

Four more go up the ladder to the whole system, each a step past the last. `fstest` is still a
standalone program, but it reads a real filesystem image through the real driver, buffer cache and
superblock check — strictly below the boot path, everything `iinit()` does minus `iinit()` itself.
The other three boot the image. `boot` gets process 1 out of the kernel and into `/etc/init`,
which forks a shell, which **prompts**; it attaches the disk read-only, which is an assertion in
itself, since that whole path writes nothing. `multi` types `^D` at that prompt and goes the rest
of the way — `/etc/rc`, a getty per line, `crypt(3)`, and two people logged in at once on the two
Consuls. `core` runs one user program under the real kernel, covering the two places the kernel
builds a user address out of an integer.

## Further reading

[Besm6_Kernel_Reference.md](../doc/Besm6_Kernel_Reference.md) is the maintainer's companion to
this article: the rules to obey while editing, and the consequences the port accepted knowingly.

The machine itself is in [Memory_Mapping.md](../doc/Memory_Mapping.md) — read it before touching
memory management — [Besm6_Peripherals.md](../doc/Besm6_Peripherals.md) before touching
[dev/](dev/), [Intrinsics.md](../doc/Intrinsics.md) for how C reaches `002 «рег»` and `033 «увв»`,
and [Besm6_Instruction_Set.md](../doc/Besm6_Instruction_Set.md) and
[Besm6_Calling_Conventions.md](../doc/Besm6_Calling_Conventions.md) underneath both.

The two subsystems with a document of their own are the gates
([Unix_Context_Switch.md](../doc/Unix_Context_Switch.md), and
[Dubna_Context_Switch.md](../doc/Dubna_Context_Switch.md) for how the machine's own operating
system did it) and the machine assist
([Kernel_Assembly_Routines.md](../doc/Kernel_Assembly_Routines.md)). The system-call surface,
including the two calls that are not v7's, is
[Unix_V7_System_Calls.md](../doc/Unix_V7_System_Calls.md).
