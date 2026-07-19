# BESM-6 Compiler Intrinsics

The nine intrinsics of [`<besm6.h>`](https://github.com/besm6/c-compiler/blob/main/libc/besm6/include/besm6.h) give C direct access to the
machine operations it cannot otherwise express: the two supervisor instructions that reach every
peripheral, the bit-manipulation instructions that have no C equivalent, the halt, and the
extracode trap. Each one compiles into **a single inline machine instruction** — never a call,
never a library routine.

They are what lets an operating system be written in C on this machine.

Everything below is **octal** unless marked otherwise, and bits are numbered **right-to-left from
1** (bit 1 = LSB, bit 48 = MSB), as everywhere else in this project.

Companion reading, in the order it becomes relevant:
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) (what the instructions do),
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) (how a C scalar sits in a word),
[Besm6_Peripherals.md](Besm6_Peripherals.md) (the `033`/`002` address map),
[Memory_Mapping.md](Memory_Mapping.md) (the page and protection registers `002` reaches),
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) (the C ABI).

---

## Table of contents

1. [Why intrinsics](#1-why-intrinsics)
2. [Using them](#2-using-them)
3. [Tier 1 — privileged: reaching the hardware](#3-tier-1--privileged-reaching-the-hardware)
4. [Tier 2 — bit manipulation](#4-tier-2--bit-manipulation)
5. [Tier 3 — extracodes](#5-tier-3--extracodes)
6. [Worked examples](#6-worked-examples)
7. [Diagnostics](#7-diagnostics)
8. [How they are lowered](#8-how-they-are-lowered)
9. [How they are tested](#9-how-they-are-tested)
10. [Summary table](#10-summary-table)

---

## 1. Why intrinsics

**The BESM-6 has no I/O address space, no memory-mapped device registers and no channel
programs.** Every peripheral in the machine is reached by exactly two supervisor instructions,
which name a register through the *effective address* and pass data through the *accumulator*:

| Opcode | Latin / Cyrillic | Reaches |
|--------|------------------|---------|
| `033` | `ext` / `увв` | the peripherals — drums, disks, tape, printer, punches, card equipment, terminals |
| `002` | `mod` / `рег` | CPU-internal registers — the cache БРЗ, the page registers РП, the protection register РЗ, the interrupt register ГРП and its mask МГРП, the mode bits РУУ |

There is no way to express either one in C. Without them a kernel obtains *every* machine
operation by calling out-of-line assembly; with them, most of that assembly disappears:

- Arming the interrupt sources a kernel can service is one write of МГРП — `002 036`. (The
  priority *level* itself is БлПр, in the mode word, which needs a read-modify-write — see §6.1.)
- The interrupt dispatcher is a read of ГРП (`002 0237`) followed by a selective clear (`002 037`).
- An address-space switch is a write of the page registers РП (`002 020`–`027`).
- Every device driver becomes a sequence of `033` control words.

Beyond that irreducible pair, the machine has bit-manipulation instructions with **no C equivalent
at all** — `apx`/`aux` (the BESM-6's PEXT/PDEP, twenty years early), `acx` (population count),
`anx` (highest set bit) — that a kernel's bitmaps, page tables and interrupt dispatch want.

---

## 2. Using them

### 2.1 The header

```c
#include <besm6.h>
```

Nothing else is needed: the header ships with the C compiler and installs into
`share/besm6/include/`, which is exactly where [cmd/cc/cc.c](../cmd/cc/cc.c)'s
`besm6_include_dir()` points `b6cpp` — under `~/.local` if that exists, else `/usr/local`. So
`b6cc -c prog.c` picks it up with no `-I` of its own.

The header declares the nine intrinsics **and nothing else**: readable wrappers — a `popcount()`,
an `spl()`, the ГРП bit names, the control-word field definitions of a particular device — are the
caller's own business. Each is one `#define`, and what it should be called depends on the program.
The kernel's own hardware constants belong in `include/`, not here.

### 2.2 The word type is `unsigned`, never `int`

A BESM-6 word is 48 bits, but a signed `int` on this compiler is only **41** of them: bits 48–42
are always zero, bit 41 is the sign, bits 40–1 are the magnitude (see
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) §5). An `unsigned` uses all 48.

This is not a stylistic preference. **ГРП uses all 48 bits** — bit 48 is `GRP_PRN1_SYNC` — so an
intrinsic typed `int` could not carry a ГРП value at all, and the top seven bits of every device
control word would be silently lost. Every intrinsic that carries a machine word is therefore
typed `unsigned`, spelled out; the header defines no typedef alias for the machine word.

The same goes for your own constants: a bit above 40 does not fit a signed literal, so
`01000000000000000U` needs its `U`.

### 2.3 Constant versus computed arguments

Two arguments become an *immediate field of the instruction word* and must therefore be
compile-time constants; the compiler diagnoses anything else:

- `__besm6_extracode(op, …)` — `op` *is* the opcode.
- `__besm6_stop(code)` — `code` is the halt instruction's own 15-bit address field (`0`…`077777`).

A constant *expression* is fine — it is folded before the check, so `__besm6_extracode(SYSCALL + 7,
4, n)` works.

Every other argument may be constant or computed. In particular the register address of
`__besm6_ext`/`__besm6_mod` may be either, and the hardware genuinely uses both: a constant address
becomes the instruction's own offset field, while a computed one is materialised into an index
register (`002 0100`–`0137`, the РУУ mode bits, encodes its *data in the address*, and
tape-transport control selects the unit as `addr − 0100`).

### 2.4 What is *not* an intrinsic: absolute machine addresses

The BESM-6 is word-addressed, so **a C pointer is a word index** — no scaling, no fat pointer for
word-sized types. Absolute machine locations therefore need no intrinsic; a `volatile unsigned *`
reaches them directly:

```c
#define DRUM1_SERVICE  ((volatile unsigned *) 010)   /* 8 service words, 010-017 */
#define PANEL_SWITCH   ((volatile unsigned *) 01)    /* front-panel switch registers 1-7 */
```

This covers the fixed low-memory buffers where mass-storage exchanges deposit their 8 service words
(`010` drum 1, `020` drum 2, `030` disk 3, `040` disk 4, `050`/`060` tape) and the operator's front
panel. Note that memory word `0` always reads as zero and stores to it are discarded, and words 0–7
are reserved.

### 2.5 There is no atomic instruction

The ISA has no test-and-set, no compare-and-swap, no load-linked, and no intrinsic can give you
one. Mutual exclusion on the BESM-6 is **interrupt masking** — which is fine for a uniprocessor
kernel, and is exactly what `spl*` is for.

---

## 3. Tier 1 — privileged: reaching the hardware

This is the irreducible core. Without it a kernel cannot boot, cannot take an interrupt, and cannot
touch a device.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `unsigned __besm6_ext(unsigned addr, unsigned acc)` | `033` `ext` увв | `A := acc; ext addr; result := A` |
| `unsigned __besm6_mod(unsigned addr, unsigned acc)` | `002` `mod` рег | `A := acc; mod addr; result := A` |
| `void __besm6_stop(unsigned code)` | `033` fmt 2 `stop` стоп | halt the processor (resumable) |

### 3.1 `ext` and `mod`

**One intrinsic per opcode, returning the accumulator.** This mirrors the hardware exactly: the
accumulator is both the input and the output, and the *direction* of the transfer lives in the
address, not in the instruction. One bit of the address means "read" — `04000` for `033`, `0200`
for `002` — and on a read the arithmetic unit switches to logical mode, because what arrives in A
is a bit pattern, not a number.

`addr` is the **verbatim address from the map in
[Besm6_Peripherals.md](Besm6_Peripherals.md)**, read bit included. That document says "`033 4031`
reads the device-ready register"; you write:

```c
unsigned ready = __besm6_ext(04031, 0);
```

and the two mean the same. On a read address the hardware ignores the incoming accumulator, so pass
`0`. On a write address the result is the unchanged accumulator, so discard it.

Both intrinsics are **side-effecting and never eliminable**, even when the result is unused: they
are the machine's only I/O.

The generated code, in all three dialects. A constant address becomes the instruction's own address
field — rendered in **decimal**, like every numeric address field in every BESM-6 assembler, so the
`04031` above comes out as `2073`:

| Dialect | constant address | computed address |
|---------|------------------|------------------|
| Madlen | `,xta,` / `,ext, 2073` | `6 ,xta,` / `,ati, 14` / `6 ,xta, 1` / `14 ,ext,` |
| Bemsh | `сч` / `увв 2073` | `сч (6)` / `уи 14` / `сч 1(6)` / `увв (14)` |
| b6as | `xta` / `ext 2073` | `6 xta` / `ati 14` / `6 xta 1` / `14 ext` |

A zero accumulator needs no literal at all: the `xta` is left with an empty address field, so it
reads memory word 0, which on this machine always reads as zero.

Three rules from the peripherals document are worth restating here, because they routinely surprise:

- **`002 037` clears ГРП by writing a mask in which a *zero* bit clears.** The operation is
  `GRP &= ACC | GRP_WIRED_BITS`. To dismiss one interrupt you write the complement of its bit, not
  the bit.
- **Wired bits cannot be cleared that way at all.** The "device free" and "exchange done" bits of
  the mass-storage channels are live wires, not flip-flops; they go down only when the device is
  itself given a new command.
- **ПРП has no interrupt line.** A pending peripheral interrupt is delivered by raising `GRP_SLAVE`
  (ГРП bit 37); the handler must then read both halves of ПРП (`033 4030` and `033 4034`) to find
  out which device it was.

### 3.2 `stop` — the halt

`__besm6_stop` is the halt instruction (Format 2, opcode `033` — a *different* opcode 033 from
`ext`, which is Format 1). It is legal in user mode, and it is **not** `_Noreturn`: the halt is
*resumable*. The machine stops, the operator reads the halt reason off the console, presses
*continue*, and execution carries on at the next instruction. So a call to it is an ordinary void
call, and the code after it is reachable — the compiler does not treat it as a terminator, and the
function keeps its epilogue:

```c
void bye(void) { __besm6_stop(0377); }
```

```
      bye:   ,name,             bye:                bye    старт 1
    b/ret:   ,subp,                 its 13                 счим 13
             ,its, 13            13 vjm b$save0            пв _save0(13)
             ,call, b/save0         stop 5                 стоп 5
             ,33, 255               uj b$ret               пб _ret
             ,uj, b/ret                                    финиш
             ,end,
```

The halt reason rides in the instruction's own 15-bit address field, which is why it must be a
compile-time constant in `0`…`077777`. It identifies the halt site on the console and in a trace or
dump. Both of our simulators, however, ignore it: `dubna` and `b6sim` alike execute opcode `0330` as
"the run is over" and end the simulation without displaying the address or offering to resume. So
under a simulator nothing after a `__besm6_stop` runs, even though the code for it is emitted.

One assembler quirk, visible in the Madlen column above: **Madlen has no halt mnemonic.** `,stop,`
is rejected outright ("ошибка в коп"), so the halt is emitted as Madlen's raw octal machine code —
and there the digit count picks the instruction format. `,33,` is the Format-2 opcode 033, the halt;
`,033,` would be the Format-1 033, which is `ext` and faults as privileged. Bemsh (`стоп`) and
`b6as` (`stop`) both name it normally.

---

## 4. Tier 2 — bit manipulation

These have no C equivalent, and a kernel wants all of them.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `unsigned __besm6_apx(unsigned a, unsigned mask)` | `020` `apx` сбр | gather the bits of `a` selected by `mask` |
| `unsigned __besm6_aux(unsigned a, unsigned mask)` | `021` `aux` рзб | scatter the top bits of `a` into `mask`'s positions |
| `unsigned __besm6_acx(unsigned a, unsigned x)` | `022` `acx` чед | `popcount(a) ⊞ x` |
| `unsigned __besm6_anx(unsigned a, unsigned x)` | `023` `anx` нед | (position of `a`'s highest set bit) `⊞ x` |
| `unsigned __besm6_arx(unsigned a, unsigned x)` | `013` `arx` слц | `a ⊞ x` — 48-bit add with end-around carry |

`⊞` throughout means **end-around-carry addition**: a 48-bit unsigned add in which a carry out of
bit 48 is added back into bit 1 (one's-complement addition). It is the machine's own integer add,
and it is *not* C's `+` on `unsigned`, which wraps mod 2⁴⁸. Pass `x = 0` to get the plain value.

Each is a single A-op-X instruction — the second operand comes from memory, the accumulator is both
the other input and the result — so each lowers exactly like a C binary operator:

```c
unsigned gather(unsigned a, unsigned m) { return __besm6_apx(a, m); }
unsigned pcnt(unsigned a)               { return __besm6_acx(a, 0); }
```

```
           6 ,xta,                       6 ,xta,
           6 ,apx, 1                       ,acx,
```

Note the second one: a zero constant operand needs **no literal at all**. The `,acx,` is left with
an empty address field, so it reads memory word 0 — which always reads as zero.

### `__besm6_apx` — gather (сбр)

For each bit position `i` from 1 to 48, if `mask[i]` is set, `a[i]` is collected. The collected bits
end up **aligned to the MSB**, in source order:

```
result = 0
for i = 1 .. 48:
    if mask[i]:  result >>= 1;  result[48] = a[i]
```

So `k = popcount(mask)` bits occupy result bits 48 down to 48−k+1. **This is the opposite alignment
from x86's PEXT**, which right-aligns. To right-align, follow with `>> (48 - k)`.

### `__besm6_aux` — scatter (рзб)

The exact inverse. Each 1-bit of `mask`, scanned from bit 48 down, consumes one bit from `a` taken
from `a`'s MSB downward and deposits it at that position; 0-bits of `mask` yield 0. This is the
natural tool for building the page-register word of §6.4 and for laying out device control words.

Being inverses, `__besm6_aux(__besm6_apx(a, m), m) == (a & m)` for every `a` and `m` — which is how
the round trip is tested.

### `__besm6_acx` — population count (чед)

`popcount(a)` added to `x` with end-around carry. Pass `x = 0` for a plain population count.

### `__besm6_anx` — highest set bit (нед)

Returns the *position* of `a`'s highest set bit, **numbered from the MSB**: bit 48 → 1, bit 47 → 2,
…, bit 1 → 48. That position is then added to `x` with end-around carry. **If `a` is zero the
result is just `x`** — there is no distinguished "not found" value, so the caller must test for zero
first. With `x = 0` this is a count-leading-zeros plus one.

### `__besm6_arx` — cyclic add (слц)

The raw end-around-carry add, useful for checksums. It is the one instruction of the five that
leaves **multiplicative** ω rather than logical, under which a `uza`/`u1a` tests `abs(A) < 0.5`
instead of `A ≠ 0`. The compiler handles that for you: every `arx` is trailed by a no-op `aox`
(OR in memory word 0 — A unchanged, ω back to logical), so an `arx` result may be branched on like
any other value:

```c
unsigned cyc(unsigned a, unsigned b) { return __besm6_arx(a, b); }
```

```
  6 xta            сч (6)
  6 arx 1          слц 1(6)
    aox            или
```

See [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) § *ω mode and the AU mode register R* for
the contract compiled code holds.

---

## 5. Tier 3 — extracodes

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `unsigned __besm6_extracode(int op, unsigned ea, unsigned acc)` | `050`…`077` | `M[016] := ea; A := acc;` invoke extracode `op`; result := A |

`op` must be a compile-time constant in `050`…`077` — it *is* the opcode. Extracodes execute in
**user** mode: they are the interface through which a program asks the operating system for a
privileged operation, and the Unix v7 syscall trap `$77 N` rides on exactly this mechanism (see
[Aout_Simulator.md](Aout_Simulator.md)). A kernel does not *call* extracodes — it implements them — but this intrinsic is what lets libc's syscall leaves
(`write.s` = `$77 4`, `exit.s` = `$77 1`, `read.s`) be written in C.

Because the opcode *is* the mnemonic, each dialect spells it differently, and the effective address
is an ordinary Format-1 address field beside it:

```c
void bye(unsigned code)                            { __besm6_extracode(077, 1, code); }
unsigned trap(unsigned ea, unsigned acc)           { return __besm6_extracode(070, ea, acc); }
```

| Dialect | `bye` (constant EA) | `trap` (computed EA) |
|---------|--------------------|----------------------|
| Madlen | `6 ,xta,` / `,*77, 1` | `6 ,xta,` / `,ati, 14` / `6 ,xta, 1` / `14 ,*70,` |
| Bemsh | `сч (6)` / `э77 1` | `сч (6)` / `уи 14` / `сч 1(6)` / `э70 (14)` |
| b6as | `6 xta` / `$77 1` | `6 xta` / `ati 14` / `6 xta 1` / `14 $70` |

(`b6as` names no mnemonic for opcodes 050–077, so it writes the raw octal opcode `$70`.)

**ABI consequence:** an extracode sets `M[016]` — that is, `r14` — from the effective address. `r14`
is the argument-count register on entry to a function and, in `b6sim`'s syscall ABI, the place
`errno` comes back in. It is caller-saved (only `r1`–`r7` must be preserved), so this is legal, but
any code around the intrinsic must treat `r14` as clobbered.

---

## 6. Worked examples

### 6.1 `spl` — the interrupt priority level

The machine has **no priority hierarchy**: an interrupt is delivered when БлПр is clear *and*
`ГРП & МГРП` is non-zero. So there are two levels, not eight, and two registers that could each
serve as the mask. They are not interchangeable, and the choice matters:

* **БлПр (ПСВ bit `02000`) is the priority.** The hardware already treats it as one — a trap or an
  extracode forces БлПр on at the vector and `выпр` restores it from СПСВ, so returning through a
  gate re-establishes the level by itself, exactly as the PDP-11's `rtt` does when it reloads the
  priority field of PS.
* **МГРП is the source enable.** `grp & mgrp` in the dispatcher means "sources this kernel is
  listening to right now", which is what that mask was always for. It is *not* constant: boot arms
  the sources that are always live, and a driver arms its own completion bit for the length of one
  exchange (`mgrpon()`/`mgrpoff()`, below). It is still not the priority — `setipl()` never
  writes it.

Putting the level in МГРП instead looks equivalent and is not: the gates hold БлПр from the vector,
so an `spl0()` that only opened МГРП would leave БлПр blocking everything, and **no interrupt could
be taken in kernel mode at all**. That is invisible for as long as every interrupt arrives in user
mode, and becomes load-bearing the moment an idle loop has to spin waiting for one.

БлПр lives in the mode word, so it is a **read-modify-write** of ПСВ — not a `vtm`, which writes the
whole register and would clobber БлП/БлЗ/ПОП/ПОК along with it. That is the one piece here the
intrinsics do *not* reach; see [`kernel/psw.s`](../kernel/psw.s). What C does express is arming the
source mask:

```c
#define MOD_MGRP  036               /* 002 036 -- write МГРП */
#define IRQ_ON    (GRP_SLAVE | GRP_TIMER)

unsigned mgrp;                      /* shadow: МГРП cannot be read back */

void intrinit(void)                 /* called once from main(), before the first spl0() */
{
    mgrp = IRQ_ON;
    __besm6_mod(MOD_MGRP, mgrp);
}

static int setipl(int s)            /* the level itself is БлПр, via psw.s */
{
    int old = curipl;
    curipl = s;
    if (s) cli(); else sti();
    return old;
}

void mgrpon(unsigned bits)          /* arm a device's bits for one exchange */
{
    int s = spl7();
    mgrp |= bits;                   /* the shadow first: МГРП is write-only, and a */
    __besm6_mod(MOD_MGRP, mgrp);    /*   driver writing its own bits would drop */
    splx(s);                        /*   every other device's */
}
```

`mgrpoff()` is the same with `mgrp &= ~bits`. The pair exists because of the mass-storage
**wired** bits (§ *Wired bits* in [Besm6_Peripherals.md](Besm6_Peripherals.md)): a "free" bit
means the channel is **idle**, so it stands whenever no transfer is running, and `002 037`
cannot lower it. Armed outside a live exchange, one such bit re-enters the dispatcher forever.
So a driver arms its bit *after* issuing the control word — issuing it is what lowers the bit —
and disarms it in the handler before `iodone()`. See §6.3 for the exchange itself.

Note `mgrp` is `unsigned`, not `int`: ГРП bit 48 exists and would not survive a 41-bit type. And it
is a shadow because МГРП, like РП and РЗ, is write-only.

This is what [`kernel/intr.c`](../kernel/intr.c) does; the reasoning above is written up there in
full.

### 6.2 The interrupt dispatcher

Read ГРП, find the highest pending unmasked bit, dismiss it:

```c
unsigned grp = __besm6_mod(0237, 0);          /* 002 0237 -- read ГРП */
unsigned pending = grp & mgrp;                /* the shadow of МГРП; see §6.1 */

if (pending) {
    /* anx numbers from the MSB: 1 = bit 48, 48 = bit 1. */
    int n = __besm6_anx(pending, 0);
    unsigned b = (unsigned)1 << (48 - n);   /* back to a bit mask */

    dispatch(n);

    /* Dismiss it.  A ZERO bit in the accumulator clears the corresponding
     * ГРП bit, so write the complement. */
    __besm6_mod(037, ~b);                       /* 002 037 -- clear ГРП */

    /* A WIRED bit survives that and goes down only when its device is given a
     * new command, so a dispatcher that loops must not assume the clear took.
     * Probing is cheaper than tabulating which bits are wired: */
    if (__besm6_mod(0237, 0) & b)
        mgrpoff(b);                             /* stop listening instead */
}
```

`__besm6_anx` is doing real work here: prioritised interrupt dispatch is *exactly* "find the highest
set bit", and without the intrinsic it is a loop.

### 6.3 Reading a drum page

This is [Besm6_Peripherals.md](Besm6_Peripherals.md) § *A worked example: reading a drum page*,
transliterated. That document builds control word `001420024` — page mode, read, memory page 2,
zone `05` — and issues it with `xta 100` / `ext 1`. In C:

```c
#define DRUM_PAGE_MODE  01000000        /* bit 19: a whole 1024-word page   */
#define DRUM_READ       00400000        /* bit 18: drum -> memory           */
#define GRP_DRUM1_FREE  01000000000000000U   /* ГРП bit 46 -- wired         */

/* Memory page in bits 17-13, zone in bits 10-3.  Bit N has the value 2^(N-1),
 * so the page field is scaled by 2^12 and the zone field by 2^2. */
unsigned cw = DRUM_PAGE_MODE | DRUM_READ | (page << 12) | (zone << 2);

__besm6_ext(01, cw);                                    /* 033 1 -- go */
while (!(__besm6_mod(0237, 0) & GRP_DRUM1_FREE))        /* poll ГРП bit 46 */
    ;
/* The 8 service words are now at 010-017, the 1024 data words at page*1024. */
```

For page 2 and zone `05` that builds `001420024` — the same control word the peripherals document
derives field by field. The `U` suffix on `GRP_DRUM1_FREE` is not optional: bit 46 does not fit in
a signed literal.

### 6.4 Packing the page registers

`002 020`–`027` each load four page numbers from one word, and the fields are interleaved rather
than adjacent — the low 5 bits of the four page numbers sit together at the bottom of the word while
their upper bits are scattered across the top. The exact layout, and the companion packing of the
protection register РЗ, are in
**[Memory_Mapping.md](Memory_Mapping.md#packing-of-the-page-registers)**.

That layout is a bit-scatter, and `aux` is a bit-scatter instruction:

```c
/* Mask of every bit position that page i contributes to, in descending order. */
static const unsigned rp_mask[4] = { /* … */ };

unsigned w = 0;
for (int i = 0; i < 4; i++)
    w |= __besm6_aux(pageno[i] << (48 - 10), rp_mask[i]);
__besm6_mod(020 + n, w);                /* load page register n */
```

`aux` consumes bits from the MSB downward, hence the `<< (48 - 10)` to left-align the 10-bit page
number. Written by hand, this is a dozen shifts and masks per page.

---

## 7. Diagnostics

Because an intrinsic is declared as an ordinary prototype, the front end checks it like any other
call: a wrong argument count is a plain `wrong number of arguments`, and each argument is coerced
against the prototype's type.

Beyond that, the compiler enforces what the *instruction encoding* requires. All of these are
hard errors:

| Source | Message |
|---|---|
| `__besm6_extracode(op, …)` with a non-constant `op` | `__besm6_extracode: the opcode must be a compile-time constant` |
| `__besm6_extracode(0100, …)` | `__besm6_extracode: opcode 100 is not an extracode (050..077)` |
| `__besm6_stop(x)` with a non-constant `x` | `intrinsic __besm6_stop takes one argument: a constant halt code in 0..077777` |
| `__besm6_stop(0100000)` | `intrinsic __besm6_stop: halt code 100000 does not fit the 15-bit address field` |

The extracode opcode is checked in the front end ([semantic/expressions.c](https://github.com/besm6/c-compiler/blob/main/semantic/expressions.c)),
early enough that the argument reaches the back end already folded to a literal whatever the
optimizer does; the halt code is checked at instruction selection.

One thing is deliberately *not* an error: an `ext`/`mod` address too large for the 12-bit Format-1
offset field (above `07777`). No address in the peripherals map is that large, but rather than
truncate such a constant the compiler quietly falls back to the computed-address path through r14 —
so `__besm6_ext(010000, 0)` is correct, just one instruction longer.

---

## 8. How they are lowered

**An intrinsic *is* a call in the IR** — LLVM's design. The intrinsics are declared as ordinary
prototypes, so `typecheck_expr`'s `EXPR_CALL` case already checks their arity and coerces their
arguments; they reach the back end as a plain `TAC_INSTRUCTION_FUN_CALL` whose `fun_name` begins
with `__besm6_`. Only instruction selection knows better: `codegen_intrinsic`
([backend/besm6/intrinsics.c](https://github.com/besm6/c-compiler/blob/main/backend/besm6/intrinsics.c)) intercepts the call at the top of
`instr.c`'s FUN_CALL case and emits machine instructions inline instead of a `,call,`.

`tac/`, `optimize/`, `ast/` and `translator/` are untouched by the whole feature, and
`optimize/dead_store.c` already treats a FUN_CALL as never-dead — which is exactly the
"side-effecting and never eliminable" contract Tier 1 needs. `semantic/` has exactly one change: the
constant-folding of the extracode opcode (§7).

**Every `__besm6_` name must be intercepted.** All nine collide under Madlen's 8-character
truncation — they share the prefix, and `__besm6_apx` and `__besm6_arx` both sanitize to the same
symbol — so an intrinsic left to fall through would not fail to link. It would silently *alias*
whichever other one the assembler saw first. Hence `codegen_intrinsic`'s bottom line is a
`fatal_error` ("`%s is not a <besm6.h> intrinsic`"), not a `return false`.

**Tier 2** is the inline binop shape: `A = a; A op= x; dst = A`, three instructions or two when the
`x` operand is a zero constant (an empty address field reads memory word 0). Only `arx` needs the
correcting no-op `,aox,` — the other four already leave logical ω, verified case by case against
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md). The correction is not cosmetic: peephole
rules #27 and #28 drop the store/reload of a boolean, so a branch on an `arx` result consumes
the accumulator the `arx` itself left, with nothing in between to reset ω.

**Tier 1 and Tier 3** share their addressing. A constant address that fits the 12-bit Format-1
offset field becomes the instruction's own address; anything else is materialised into
`REG_SCRATCH` — **r14** — and the instruction reads `EA = M[14] + 0`. The accumulator is loaded
**last**, because materialising the address clobbers A. That ordering does double duty: the `ati`
between the two loads leaves A unknown to the peephole, which is what stops rule #27 from dropping
the accumulator load as a redundant reload — and so what makes `__besm6_ext(a, a)` work.

**The r14 clobber.** An extracode sets `M[016]` from the effective address, so r14 does not survive
one. Nothing can be live in it across the trap: r14 is caller-saved, and its ABI role is the
argument count, which a caller loads immediately before the `,call,`. In the computed-address case
the extracode merely rewrites the scratch register with the address that was already in it.

**Three new instruction kinds** carry this in the backend IR ([besm6.asdl](https://github.com/besm6/c-compiler/blob/main/backend/besm6/besm6.asdl),
[besm.h](https://github.com/besm6/c-compiler/blob/main/backend/besm6/besm.h)): `BESM_IO_EXT`, `BESM_IO_MOD` and `BESM_IO_EXTRACODE`. The
privileged `mod` could not reuse the `BESM_MOD_*` prefix — that is already the C-register address
modification group, `BESM_MOD_UTC`/`BESM_MOD_WTC`, an entirely different instruction. The extracode
is `BESM_SHAPE_SPECIAL` with its opcode in the `opcode` field, because its mnemonic *is* its opcode
and every dialect writes that differently.

**Two peephole obligations** come with those kinds, and both are the sort that miscompiles silently
if missed (see [Peephole_Rewrites.md](https://github.com/besm6/c-compiler/blob/main/docs/Peephole_Rewrites.md) §5.10):

- `BESM_IO_EXT`/`BESM_IO_MOD`/`BESM_IO_EXTRACODE` are **basic-block boundaries**. `ext`/`mod`
  rewrite the AU mode register R (a read address switches it to logical), which is the very
  register rules #29(a)/(b) track; the extracode runs arbitrary monitor code before returning.
- `BESM_BRANCH_STOP` is deliberately **not** a terminator, so rule #31(b) does not open an
  unreachable run at it. The halt is resumable.

**Per-dialect spelling** is where the three emitters diverge:

| Dialect | `ext` | `mod` | halt | extracode |
|---------|-------|-------|------|-----------|
| Madlen | `,ext,` | `,mod,` | `,33,` (raw octal — no mnemonic) | `,*74,` |
| Bemsh | `увв` | `рег` | `стоп` | `э74` |
| b6as | `ext` | `mod` | `stop` | `$74` |

Every numeric address field is **decimal** in all three, so the octal addresses of the peripherals
map come out converted: `__besm6_ext(04031, …)` emits `,ext, 2073`, `__besm6_mod(0237, 0)` emits
`,mod, 159`, and `__besm6_stop(0377)` emits `,33, 255`.

---

## 9. How they are tested

- **Typing, arity, and the folded opcode** — [semantic/test/intrinsics_tests.cpp](https://github.com/besm6/c-compiler/blob/main/semantic/test/intrinsics_tests.cpp).
  That the word type is `unsigned`, that `__besm6_stop` is *not* `_Noreturn`, and the negatives: a
  non-constant extracode opcode, an out-of-range one, a wrong argument count.
- **Instruction selection** — [backend/besm6/test/intrinsics_tests.cpp](https://github.com/besm6/c-compiler/blob/main/backend/besm6/test/intrinsics_tests.cpp),
  golden assembly for all three dialects. These also pin that no `,call,` and no `,subp,` survives
  for an intrinsic — the alias hazard above is invisible at link time, so it has to be caught here.
  Two of them pin the ω contract specifically: a branch on an `arx` result (the correcting `aox`
  lands between the `arx` and the `uza`) and a branch on an `ext` result (no correction needed, and
  none emitted).
- **Execution** — the same file, on all three paths: `dubna` for Madlen and Bemsh, `b6sim` for the
  Unix path. Tier 2 is checked against the values the hardware actually produces (`popcount(0377)`
  = 8, `anx(1)` = 48, the apx/aux round trip, the end-around carry); the halt is checked to stop the
  run; and the extracode is checked by trapping into the monitor's "finish" (`074`) and into
  `b6sim`'s `SYS_exit` (`$77 1`), whose status the simulator takes from the accumulator.
- **Tier 1 has no run test, and cannot have one.** `ext` and `mod` are kernel-mode instructions, and
  both simulators throw `Illegal instruction` on them from user mode. What is verified mechanically
  is that the assemblers accept what we emit: `b6as` assembles and `b6ld` links both forms
  (`UnixAssembleIntrinsicIo`), with no `__besm6_ext` symbol left to resolve, and the two Dubna
  translators were checked by hand (`,ext, 2073` and `увв 2073` each assemble to opcode 033).

---

## 10. Summary table

| Intrinsic | Op | Cyrillic | Constant arg | Notes |
|-----------|----|----------|--------------|-------|
| `__besm6_ext` | `033` | увв | — | privileged; never eliminable |
| `__besm6_mod` | `002` | рег | — | privileged; never eliminable |
| `__besm6_stop` | `033` fmt 2 | стоп | `code` | resumable — not `_Noreturn` |
| `__besm6_apx` | `020` | сбр | — | gathers MSB-aligned, unlike PEXT |
| `__besm6_aux` | `021` | рзб | — | inverse of `apx` |
| `__besm6_acx` | `022` | чед | — | `⊞`, not `+` |
| `__besm6_anx` | `023` | нед | — | position from the MSB; `a == 0` → `x` |
| `__besm6_arx` | `013` | слц | — | `⊞`, not `+`; ω corrected by the compiler |
| `__besm6_extracode` | `050`–`077` | — | `op` | user mode; clobbers r14 |

---

## See also

- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — the opcodes, the registers, the ω mode.
- [Besm6_Peripherals.md](Besm6_Peripherals.md) — the `033`/`002` address map and the ГРП/ПРП bits.
- [Memory_Mapping.md](Memory_Mapping.md) — what `__besm6_mod(020…033, …)` actually programs: the
  page registers РП, the protection register РЗ, and the address translation they drive.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — why the word type is `unsigned`.
- [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) — the `NTR 3` / logical-ω contract that
  compiled code holds.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the C ABI these plug into.
- [Peephole_Rewrites.md](https://github.com/besm6/c-compiler/blob/main/docs/Peephole_Rewrites.md) — the block-boundary and memory-operand obligations
  a new instruction kind carries.
- [Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md) — what stays in assembly regardless.

---

*Copyright © 2026 Serge Vakulenko.*
