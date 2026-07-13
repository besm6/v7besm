# BESM-6 Compiler Intrinsics

This is a **proposal**: the set of compiler intrinsics (builtins) that the BESM-6 C compiler
should grow so that the Unix kernel can drive the hardware from C instead of assembly. It
specifies each intrinsic's name, signature and semantics. It deliberately says nothing about *how*
to implement them inside the compiler; that is a separate design.

Everything below is **octal** unless marked otherwise, and bits are numbered **right-to-left
from 1** (bit 1 = LSB, bit 48 = MSB), as everywhere else in this project.

Companion reading, in the order it becomes relevant:
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) (what the instructions do),
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) (how a C scalar sits in a word),
[Besm6_Peripherals.md](Besm6_Peripherals.md) (the `033`/`002` address map),
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) (the C ABI).

---

## Table of contents

1. [Why intrinsics](#1-why-intrinsics)
2. [Ground rules](#2-ground-rules)
3. [Tier 1 — privileged: reaching the hardware](#3-tier-1--privileged-reaching-the-hardware)
4. [Tier 2 — bit manipulation](#4-tier-2--bit-manipulation)
5. [Tier 3 — extracodes](#5-tier-3--extracodes)
6. [Worked examples](#6-worked-examples)
7. [The header `<besm6.h>`](#7-the-header-besm6h)
8. [Summary table](#8-summary-table)

---

## 1. Why intrinsics

**The BESM-6 has no I/O address space, no memory-mapped device registers and no channel
programs.** Every peripheral in the machine is reached by exactly two supervisor instructions,
which name a register through the *effective address* and pass data through the *accumulator*:

| Opcode | Latin / Cyrillic | Reaches |
|--------|------------------|---------|
| `033` | `ext` / `увв` | the peripherals — drums, disks, tape, printer, punches, card equipment, terminals |
| `002` | `mod` / `рег` | CPU-internal registers — the cache БРЗ, the page registers РП, the protection register РЗ, the interrupt register ГРП and its mask МГРП, the mode bits РУУ |

There is no way to express either one in C. Today the kernel obtains *every* machine operation by
calling out-of-line assembly in [kernel/besm6.S](../kernel/besm6.S), where the thirty-odd routines
are still `//TODO` stubs, and the 93 device-I/O call sites in [kernel/dev/](../kernel/dev/) are all
x86 `inb`/`outb` inherited from the v7/x86 port.

With these two instructions available as intrinsics, most of that assembly disappears:

- `spl0`…`spl7` and `splx` are nothing but writes of the МГРП interrupt mask — `002 036`.
- The interrupt dispatcher is a read of ГРП (`002 0237`) followed by a selective clear (`002 037`).
- `resume()`'s address-space switch is a write of the page registers РП (`002 020`–`027`).
- Every driver in `kernel/dev/` becomes a sequence of `033` control words.

Beyond that irreducible pair, the machine has bit-manipulation instructions with **no C equivalent
at all** — `apx`/`aux` (the BESM-6's PEXT/PDEP, twenty years early), `acx` (population count),
`anx` (highest set bit) — that the kernel's bitmaps, page tables and interrupt dispatch want.

---

## 2. Ground rules

These apply to every intrinsic in this document.

### 2.1 The word type is `unsigned`, never `int`

A BESM-6 word is 48 bits, but a signed `int` on this compiler is only **41** of them: bits 48–42
are always zero, bit 41 is the sign, bits 40–1 are the magnitude (see
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) §5). An `unsigned` uses all 48.

This is not a stylistic preference. **ГРП uses all 48 bits** — bit 48 is `GRP_PRN1_SYNC` — so an
intrinsic typed `int` could not carry a ГРП value at all, and the top seven bits of every device
control word would be silently lost. Every intrinsic that carries a machine word is therefore
typed `unsigned`.

The header defines the spelling used throughout:

```c
typedef unsigned besm6_word;    /* one 48-bit machine word */
```

### 2.2 Constant versus computed arguments

One argument becomes an *immediate field of the instruction word* and must therefore be a
compile-time constant; the compiler must diagnose a non-constant:

- `__besm6_extracode(op, …)` — `op` *is* the opcode.

The register address of `__besm6_ext` and `__besm6_mod` may be either. `ext` and `mod` are
Format-1 instructions, whose offset field is 12 bits (`07777` maximum), and every address in the
map fits: `033` reaches `04177`, `002` reaches `0237`. So a **constant** address is always an
immediate, and the Format-1 `S` bit (bit 19, which would add `070000`) is never needed.

A **computed** address must nonetheless be supported, because the hardware genuinely uses one:
`002 0100`–`0137` (РУУ mode bits) encodes its *data in the address*, and tape-transport control
(`033 0100`–`0137`) selects the unit as `addr − 0100`. The compiler materialises the value into an
index register and emits `<reg> ext 0` — the effective address is `M[reg] + offset`, so this works
unchanged.

### 2.3 What is *not* an intrinsic: absolute machine addresses

The BESM-6 is word-addressed, so **a C pointer is a word index** — no scaling, no fat pointer for
word-sized types. Absolute machine locations therefore need no intrinsic; a `volatile unsigned *`
reaches them directly:

```c
#define DRUM1_SERVICE  ((volatile besm6_word *) 010)   /* 8 service words, 010-017 */
#define PANEL_SWITCH   ((volatile besm6_word *) 01)    /* front-panel switch registers 1-7 */
```

This covers the fixed low-memory buffers where mass-storage exchanges deposit their 8 service words
(`010` drum 1, `020` drum 2, `030` disk 3, `040` disk 4, `050`/`060` tape) and the operator's front
panel. Note that memory word `0` always reads as zero and stores to it are discarded, and words 0–7
are reserved.

### 2.4 There is no atomic instruction

The ISA has no test-and-set, no compare-and-swap, no load-linked. Nothing in this proposal can give
you one. Mutual exclusion on the BESM-6 is **interrupt masking** — which is fine for a uniprocessor
v7 kernel, and is exactly what `spl*` is for.

---

## 3. Tier 1 — privileged: reaching the hardware

This is the irreducible core. Without it the kernel cannot boot, cannot take an interrupt, and
cannot touch a device.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `besm6_word __besm6_ext(unsigned addr, besm6_word acc)` | `033` `ext` увв | `A := acc; ext addr; result := A` |
| `besm6_word __besm6_mod(unsigned addr, besm6_word acc)` | `002` `mod` рег | `A := acc; mod addr; result := A` |
| `_Noreturn void __besm6_stop(unsigned code)` | `033` fmt 2 `stop` стоп | halt the processor |

**One intrinsic per opcode, returning the accumulator.** This mirrors the hardware exactly: the
accumulator is both the input and the output, and the *direction* of the transfer lives in the
address, not in the instruction. One bit of the address means "read" — `04000` for `033`, `0200`
for `002` — and on a read the arithmetic unit switches to logical mode because what arrives in A is
a bit pattern, not a number.

`addr` is the **verbatim address from the map in
[Besm6_Peripherals.md](Besm6_Peripherals.md)**, read bit included. That document says "`033 4031`
reads the device-ready register"; you write:

```c
besm6_word ready = __besm6_ext(04031, 0);
```

and the two read the same. On a read address the hardware ignores the incoming accumulator, so pass
`0`. On a write address the result is the unchanged accumulator, so discard it.

Both intrinsics are **side-effecting and never eliminable**, even when the result is unused: they
are the machine's only I/O.

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

`__besm6_stop` is the halt instruction (Format 2, opcode `033` — a different opcode 033 from `ext`,
which is Format 1). Its address field carries a halt reason that SIMH and `b6sim` both display, so
it is the natural bottom of `panic()`. It is legal in user mode.

---

## 4. Tier 2 — bit manipulation

These have no C equivalent, and the kernel wants all of them.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `besm6_word __besm6_apx(besm6_word a, besm6_word mask)` | `020` `apx` сбр | gather the bits of `a` selected by `mask` |
| `besm6_word __besm6_aux(besm6_word a, besm6_word mask)` | `021` `aux` рзб | scatter the top bits of `a` into `mask`'s positions |
| `besm6_word __besm6_acx(besm6_word a, besm6_word x)` | `022` `acx` чед | `popcount(a) ⊞ x` |
| `besm6_word __besm6_anx(besm6_word a, besm6_word x)` | `023` `anx` нед | (position of `a`'s highest set bit) `⊞ x` |
| `besm6_word __besm6_arx(besm6_word a, besm6_word x)` | `013` `arx` слц | `a ⊞ x` — 48-bit add with end-around carry |

`⊞` throughout means **end-around-carry addition**: a 48-bit unsigned add in which a carry out of
bit 48 is added back into bit 1 (one's-complement addition). It is the machine's own integer add,
and it is *not* C's `+` on `unsigned`, which wraps mod 2⁴⁸.

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

### `__besm6_acx` — population count (чед)

`popcount(a)` added to `x` with end-around carry. Pass `x = 0` for a plain population count; the
header wraps that as `b6_popcount`.

### `__besm6_anx` — highest set bit (нед)

Returns the *position* of `a`'s highest set bit, **numbered from the MSB**: bit 48 → 1, bit 47 → 2,
…, bit 1 → 48. That position is then added to `x` with end-around carry. **If `a` is zero the
result is just `x`** — there is no distinguished "not found" value, so the caller must test for zero
first. With `x = 0` this is a count-leading-zeros plus one; the header wraps it as `b6_highbit`.

### `__besm6_arx` — cyclic add (слц)

The raw end-around-carry add, useful for checksums. Note that unlike the other four it leaves
**multiplicative** ω rather than logical, so a `uza`/`u1a` placed directly after it would test
`abs(A) < 0.5` and not `A ≠ 0`; see [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) § *ω mode
and the AU mode register R* for the contract compiled code holds.

---

## 5. Tier 3 — extracodes

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `besm6_word __besm6_extracode(int op, unsigned ea, besm6_word acc)` | `$50`…`$77` | `M[016] := ea; A := acc;` invoke extracode `op`; result := A |

`op` must be a compile-time constant — it *is* the opcode. Extracodes execute in **user** mode:
they are the interface through which a user program asks the operating system for a privileged
operation, and the v7 syscall trap `$77 N` rides on exactly this mechanism (see
[Aout_Simulator.md](Aout_Simulator.md)).

The kernel does not *call* extracodes — it implements them. But this intrinsic matters anyway,
because it is what would let libc's hand-written syscall leaves (`write.s` = `$77 4`, `exit.s` =
`$77 1`, `read.s`) be written in C. They exist as assembly today precisely because the compiler has
no intrinsics.

**ABI consequence:** an extracode sets `M[016]` — that is, `r14` — from the effective address. `r14`
is the argument-count register on entry to a function and, in `b6sim`'s syscall ABI, the place
`errno` comes back in. It is caller-saved (only `r1`–`r7` must be preserved), so this is legal, but
any code around the intrinsic must treat `r14` as clobbered.

---

## 6. Worked examples

These are what the proposal buys. All four are assembly today.

### 6.1 `spl` — the interrupt priority level

The whole of `spl0`…`spl7`/`splx`, plus `cli`/`sti`, is one write of the МГРП mask:

```c
static besm6_word ipl[8] = { /* МГРП mask for each level */ };
static int cur_ipl;

int splx(int s)
{
    int old = cur_ipl;
    cur_ipl = s;
    __besm6_mod(036, ipl[s]);       /* 002 036 -- write МГРП */
    return old;
}

int spl6(void) { return splx(6); }
void cli(void) { __besm6_mod(036, 0); }
```

Note `ipl[]` is `besm6_word`, not `int`: ГРП bit 48 exists and would not survive a 41-bit type.

### 6.2 The interrupt dispatcher

Read ГРП, find the highest pending unmasked bit, dismiss it:

```c
besm6_word grp = __besm6_mod(0237, 0);          /* 002 0237 -- read ГРП */
besm6_word pending = grp & ipl[cur_ipl];

if (pending) {
    /* anx numbers from the MSB: 1 = bit 48, 48 = bit 1. */
    int n = __besm6_anx(pending, 0);
    besm6_word b = (besm6_word)1 << (48 - n);   /* back to a bit mask */

    dispatch(n);

    /* Dismiss it.  A ZERO bit in the accumulator clears the corresponding
     * ГРП bit, so write the complement -- and note that a wired bit will
     * survive this and go down only when its device is given a new command. */
    __besm6_mod(037, ~b);                       /* 002 037 -- clear ГРП */
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
besm6_word cw = DRUM_PAGE_MODE | DRUM_READ | (page << 12) | (zone << 2);

__besm6_ext(01, cw);                                    /* 033 1 -- go */
while (!(__besm6_mod(0237, 0) & GRP_DRUM1_FREE))        /* poll ГРП bit 46 */
    ;
/* The 8 service words are now at 010-017, the 1024 data words at page*1024. */
```

For page 2 and zone `05` that builds `001420024` — the same control word the peripherals document
derives field by field. The `U` suffix on `GRP_DRUM1_FREE` is not optional: bit 46 does not fit in
a signed literal.

### 6.4 Packing the page registers

`002 020`–`027` each load four 10-bit page numbers from one word, in a layout the simulator's own
comment calls unusual: page *i* takes its bits 1–5 from accumulator bits `5i+1 … 5i+5`, but its bit
6 from bit `29+i`, bit 7 from `33+i`, bit 8 from `37+i`, bit 9 from `41+i`, bit 10 from `45+i`. The
fields are interleaved, not adjacent.

That is a bit-scatter, and `aux` is a bit-scatter instruction:

```c
/* Mask of every bit position that page i contributes to, in descending order. */
static const besm6_word rp_mask[4] = { /* … */ };

besm6_word w = 0;
for (int i = 0; i < 4; i++)
    w |= __besm6_aux(pageno[i] << (48 - 10), rp_mask[i]);
__besm6_mod(020 + n, w);                /* load page register n */
```

`aux` consumes bits from the MSB downward, hence the `<< (48 - 10)` to left-align the 10-bit page
number. Written by hand, this is a dozen shifts and masks per page.

---

## 7. The header `<besm6.h>`

The intrinsics should be declared in a single header, alongside the readable wrappers built on
them. It belongs in the C compiler's `libc/besm6/include/` and installs into
`share/besm6/include/`, where [cmd/cc/cc.c](../cmd/cc/cc.c)'s `besm6_include_dir()` already looks.

```c
#ifndef _BESM6_H
#define _BESM6_H

typedef unsigned besm6_word;            /* one 48-bit machine word */

/* Tier 1 -- privileged */
besm6_word __besm6_ext(unsigned addr, besm6_word acc);
besm6_word __besm6_mod(unsigned addr, besm6_word acc);
_Noreturn void __besm6_stop(unsigned code);

/* Tier 2 -- bit manipulation */
besm6_word __besm6_apx(besm6_word a, besm6_word mask);
besm6_word __besm6_aux(besm6_word a, besm6_word mask);
besm6_word __besm6_acx(besm6_word a, besm6_word x);
besm6_word __besm6_anx(besm6_word a, besm6_word x);
besm6_word __besm6_arx(besm6_word a, besm6_word x);

/* Tier 3 -- extracodes */
besm6_word __besm6_extracode(int op, unsigned ea, besm6_word acc);  /* op constant */

/* ------------------------------------------------------------------ */
/* Conveniences */

#define b6_popcount(a)   __besm6_acx((a), 0)
#define b6_highbit(a)    __besm6_anx((a), 0)     /* 1 = bit 48 … 48 = bit 1; 0 -> 0 */

#define b6_grp_read()    __besm6_mod(0237, 0)
#define b6_grp_mask(m)   ((void) __besm6_mod(036, (m)))
#define b6_grp_clear(m)  ((void) __besm6_mod(037, ~(besm6_word)(m)))

#endif /* _BESM6_H */
```

The kernel additionally wants the ГРП/ПРП bit names and the per-device control-word field
definitions from [Besm6_Peripherals.md](Besm6_Peripherals.md). Those are hardware constants, not
intrinsics, and belong in the kernel's own include tree.

---

## 8. Summary table

| Intrinsic | Op | Cyrillic | Constant arg |
|-----------|----|----------|--------------|
| `__besm6_ext` | `033` | увв | — |
| `__besm6_mod` | `002` | рег | — |
| `__besm6_stop` | `033` fmt 2 | стоп | — |
| `__besm6_apx` | `020` | сбр | — |
| `__besm6_aux` | `021` | рзб | — |
| `__besm6_acx` | `022` | чед | — |
| `__besm6_anx` | `023` | нед | — |
| `__besm6_arx` | `013` | слц | — |
| `__besm6_extracode` | `$50`–`$77` | — | `op` |

Of these, only `ext` and `mod` are absent from the compiler's back-end instruction table; every
other mnemonic named above is already there, and all of them — plus `ij` and `stop` — are already
assembled by [cmd/as/tables.c](../cmd/as/tables.c).

---

## See also

- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — the opcodes, the registers, the ω mode.
- [Besm6_Peripherals.md](Besm6_Peripherals.md) — the `033`/`002` address map and the ГРП/ПРП bits.
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — why the word type is `unsigned`.
- [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) — the `NTR 3` / logical-ω contract that
  compiled code holds.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the C ABI these plug into.
- [Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md) — what stays in assembly regardless.
