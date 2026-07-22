# BESM-6 Compiler Intrinsics

The twelve intrinsics of [`<besm6.h>`](https://github.com/besm6/c-compiler/blob/main/libc/besm6/include/besm6.h) give C direct access to the
machine operations it cannot otherwise express: the two supervisor instructions that reach every
peripheral, the three that read and write the mode word PSW, the bit-manipulation instructions
that have no C equivalent, the halt, and the extracode trap. Each one compiles into **a single
inline machine instruction** — never a call, never a library routine.

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
  priority *level* itself is БлПр, in the mode word, and that is one `vtm` — see §3.3 and §6.1.)
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

The header declares the twelve intrinsics **and nothing else**: readable wrappers — a `popcount()`,
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

**The three PSW intrinsics are the deliberate exception: they are typed `int`.** What they carry
is not a machine word but a 15-bit address-field value. PSW is read and written through
`ita`/`ati`/`vtm`, every one of which is a 15-bit path, so the whole mode word fits a signed `int`
with thirty-odd bits to spare and nothing is lost.

### 2.3 Constant versus computed arguments

Three arguments become an *immediate field of the instruction word* and must therefore be
compile-time constants; the compiler diagnoses anything else:

- `__besm6_extracode(op, …)` — `op` *is* the opcode.
- `__besm6_stop(code)` — `code` is the halt instruction's own 15-bit address field (`0`…`077777`).
- `__besm6_maskpsw(mask)` — `mask` is the mode write's own 15-bit address field (§3.3).

A constant *expression* is fine, to any depth — all three are folded in the compiler's front end by
the language's own recursive constant evaluator, before the check and independently of the
optimizer flags. So `__besm6_extracode(SYSCALL + 7, 4, n)` works, and so does a mask assembled out
of the named PSW bits:

```c
#define PSW_KERNEL (PSW_MMAP_DISABLE | PSW_PROT_DISABLE)   /* sys/besm6dev.h */

__besm6_maskpsw(PSW_KERNEL | PSW_INTR_DISABLE);            /* three terms, two levels */
```

There is no nesting limit to keep in mind. (There was, until the fold moved to the front end: the
back end's TAC-level folding collapsed one level only, so the two-term form was accepted while the
three-term form was rejected as "not a constant". The rule looked arbitrary at the call site, which
is exactly what a compile-time constraint must not do.)

Every other argument may be constant or computed. In particular the register address of
`__besm6_ext`/`__besm6_mod` may be either, and the hardware genuinely uses both: a constant address
becomes the instruction's own offset field, while a computed one arrives in the C address-modifier
register (`002 0100`–`0137`, the РУУ mode bits, encodes its *data in the address*, and
tape-transport control selects the unit as `addr − 0100`). Neither costs an index register, and the
common computed shapes — a variable, or a variable plus a constant — cost one instruction, the same
as a constant: see §8.

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
| `int __besm6_getpsw(void)` | `042` `ita` счи | `A := M[021]` — read the mode word |
| `void __besm6_setpsw(int psw)` | `040` `ati` уи | `A := psw; M[021] := A[15:1]` |
| `void __besm6_maskpsw(int mask)` | `024` `vtm` уиа, reg 0 | `PSW.{БлП,БлЗ,БлПр} := mask` |

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
| Madlen | `,xta,` / `,ext, 2073` | `6 ,xta, 1` / `6 ,wtc,` / `,ext,` |
| Bemsh | `сч` / `увв 2073` | `сч 1(6)` / `мод (6)` / `увв` |
| b6as | `xta` / `ext 2073` | `6 xta 1` / `6 wtc` / `ext` |

A zero accumulator needs no literal at all: the `xta` is left with an empty address field, so it
reads memory word 0, which on this machine always reads as zero.

**A computed address costs no index register and, in the shapes device code actually writes, no
extra instruction either.** It arrives in the C address-modifier register — `wtc` reads it straight
out of the frame slot or global it already lives in, and the `ext` beside it needs no operand at
all. §8 has the three addressing modes and the peephole rule that collapses them.

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

### 3.3 `getpsw` / `setpsw` / `maskpsw` — the mode word

The register file that holds the index registers does not stop at `M[017]`: it continues into the
machine's own control registers, and **PSW, at `021`, is the one a kernel needs from C**. It
carries three bits that no C construct can otherwise reach:

| Bit | Name | Meaning |
|-----|------|---------|
| `02000` | БлПр | interrupts blocked — the global interrupt flag, and hence this kernel's **interrupt priority level** (§6.1) |
| `01` | БлП | address mapping off |
| `02` | БлЗ | protection register off |

Unlike РП and РЗ, which are write-only, **PSW can be read back** — which is the only reason a
getter can exist at all. See [Memory_Mapping.md](Memory_Mapping.md) for what БлП and БлЗ override.

`ita`/`ati` name PSW through their address field exactly as they name an index register, so the
pair is the general path: read into the accumulator, modify, write back.

```c
__besm6_setpsw(__besm6_getpsw() | 02000);       /* block interrupts, keep everything else */
```

**`__besm6_maskpsw` is the one-instruction form, and it is a genuine oddity of the machine.**
`024 vtm` with a **register field of 0** is not an index-register load: `M[0]` always reads 0, so
the register half of the instruction is a no-op and in supervisor mode the hardware spends it on
PSW instead, writing БлП, БлЗ and БлПр **all three at once** straight out of the address field. It
is a *masked* write — ПоП, ПоК and the write-watch bit are not in the mask and do not move — and
it disturbs neither the accumulator nor ω, unlike the read-modify-write above. In user mode it has
no effect at all. ([Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §024 has the simh source
for the mask.)

That makes blocking and enabling one instruction each, and this kernel writes them exactly so —
inline, one apiece in `spl1()` and `spl0()` ([kernel/intr.c](../kernel/intr.c)), with `PSW_KERNEL` the two standing
bits named in [`sys/besm6dev.h`](../include/sys/besm6dev.h):

```c
__besm6_maskpsw(PSW_KERNEL | PSW_INTR_DISABLE);   /* БлП|БлЗ|БлПр — interrupts off */
__besm6_maskpsw(PSW_KERNEL);                      /* БлП|БлЗ      — interrupts on  */
```

There used to be a `kernel/psw.s` holding a `cli`, an `sti` and a `getpsw`, one instruction and one
`uj` apiece. It is gone: these three intrinsics say the same thing without the call.

Writing БлП and БлЗ as well is not a hazard, it is the point: a kernel that runs unmapped with
protection off holds `БлП = БлЗ = 1` as a standing invariant, so `02003`/`3` put back exactly what
is already there and re-assert the invariant on the way past. The corollary is that
`__besm6_maskpsw` must not be called from inside a *mapped* bracket — it would slam БлП back on
and pull the mapping out from under the copy. Such a bracket has to bank the whole word with
`__besm6_getpsw`/`__besm6_setpsw`, precisely because it must preserve a БлПр it does not know;
that is why [uarea.S](../kernel/uarea.S), [seg.S](../kernel/seg.S) and
[usermem.S](../kernel/usermem.S) issue their own `vtm 02002`/`vtm 02003` and bank PSW with
`ita`/`ati`.

The mask rides in the instruction's own 15-bit address field, so it must be a compile-time
constant (§2.3, §7) — a constant *expression* to any depth, which is what lets a kernel build it
out of named bits (`PSW_KERNEL | PSW_INTR_DISABLE`) rather than write `02003` and hope.

The generated code, and here **the Madlen column is the interesting one**:

| Dialect | `getpsw` | `setpsw` | `maskpsw(02003)` |
|---------|----------|----------|------------------|
| Madlen | `,ita, 17` | `,xta, …` / `,ati, 17` | `,24, 1027` |
| Bemsh | `счи 17` | `сч …` / `уи 17` | `уиа 1027` |
| b6as | `ita 17` | `xta …` / `ati 17` | `vtm 1027` |

**Madlen will not spell the mode write.** `,vtm,` with a zero modifier — omitted or written out as
`0 ,vtm,` — is rejected with *ошибка в модификаторе*; the 1972 autocode accepts only `1`–`15` there,
for `,utm,` as well, since writing `M[0]` is meaningless in ordinary code and it has no idea the
hardware repurposes the encoding. So the mode write goes out as raw octal machine code, the same
escape the halt takes in §3.2: `,24,` is the Format-2 opcode 024 with a zero register field. `b6as`
names it normally, so the kernel's own sources read `vtm 02003`.

**Neither user-level simulator models the register file past the index registers.** `dubna` and
`b6sim` both compute `M[Aex & 017]` for `ita`/`ati`, so under them `ita 021` reads index register
`M[1]` and `ati 021` would *clobber* it — an ABI-preserved register — and both give the register-0
`vtm` a private trace-toggle meaning instead of a PSW write. These intrinsics are for the real
machine and for **SIMH**, which is where this kernel runs; like `ext`/`mod` they are verified by
assembling, not by running under `b6sim` (§9).

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
| Madlen | `6 ,xta,` / `,*77, 1` | `6 ,xta, 1` / `6 ,wtc,` / `,*70,` |
| Bemsh | `сч (6)` / `э77 1` | `сч 1(6)` / `мод (6)` / `э70` |
| b6as | `6 xta` / `$77 1` | `6 xta 1` / `6 wtc` / `$70` |

(`b6as` names no mnemonic for opcodes 050–077, so it writes the raw octal opcode `$70`.)

**ABI consequence:** an extracode sets `M[016]` — that is, `r14` — from the effective address. `r14`
is the argument-count register on entry to a function and, in `b6sim`'s syscall ABI, the place
`errno` comes back in. It is caller-saved (only `r1`–`r7` must be preserved), so this is legal, but
any code around the intrinsic must treat `r14` as clobbered. That is the *hardware* writing it: the
lowering itself no longer goes anywhere near r14 (§8).

---

## 6. Worked examples

### 6.1 `spl` — the interrupt priority level

The machine has **no priority hierarchy**: an interrupt is delivered when БлПр is clear *and*
`ГРП & МГРП` is non-zero. So there are two levels, not eight, and two registers that could each
serve as the mask. They are not interchangeable, and the choice matters:

* **БлПр (PSW bit `02000`) is the priority.** The hardware already treats it as one — a trap or an
  extracode forces БлПр on at the vector and `выпр` restores it from SPSW, so returning through a
  gate re-establishes the level by itself, exactly as the PDP-11's `rtt` does when it reloads the
  priority field of PS.
* **МГРП is the source enable.** `grp & mgrp` in the dispatcher means "sources this kernel is
  listening to right now", which is what that mask was always for. It is *not* constant: boot arms
  the sources that are always live, and a driver arms its own completion bit for the length of one
  exchange (`mgrpon()`/`mgrpoff()`, below). It is still not the priority — no `spl*` ever
  writes it.

Putting the level in МГРП instead looks equivalent and is not: the gates hold БлПр from the vector,
so an `spl0()` that only opened МГРП would leave БлПр blocking everything, and **no interrupt could
be taken in kernel mode at all**. That is invisible for as long as every interrupt arrives in user
mode, and becomes load-bearing the moment an idle loop has to spin waiting for one.

БлПр lives in the mode word, and writing it is a single `vtm` — with the register field 0, `уиа`
writes БлП, БлЗ and БлПр into PSW from its address field, and only those three
([Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §024). **C reaches that too, through
`__besm6_maskpsw`** (§3.3), one inline instruction with no call around it — which is what let
`kernel/psw.s` and its `cli`/`sti`/`getpsw` be retired outright. Arming the source mask is C as
well, so the whole of this is now one file:

```c
#define MOD_MGRP  036               /* 002 036 -- write МГРП */
#define IRQ_ON    (GRP_SLAVE | GRP_TIMER)

unsigned mgrp;                      /* shadow: МГРП cannot be read back */

void intrinit(void)                 /* called once from main(), before the first spl0() */
{
    mgrp = IRQ_ON;
    __besm6_mod(MOD_MGRP, mgrp);
}

void spl0(void)                     /* the level itself is БлПр, one `vtm' either way */
{
    curipl = 0;
    __besm6_maskpsw(PSW_KERNEL);
}

int spl1(void)
{
    int old = curipl;
    curipl = 1;
    __besm6_maskpsw(PSW_KERNEL | PSW_INTR_DISABLE);
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

The mask is an immediate field of the instruction and cannot be arithmetic, so each level needs its
own constant (§2.3). That is why `spl0()` and `spl1()`, which know theirs, are branch-free, while
`splx(s)` — whose level is a run-time value — has to select between the two instructions with a
`uza`. What the intrinsic buys over the out-of-line `cli`/`sti` it replaced is the **call**, on
every `spl*` in the kernel.

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
| `__besm6_stop(x)` with a non-constant `x` | `__besm6_stop: the halt code must be a compile-time constant` |
| `__besm6_stop(0100000)` | `__besm6_stop: halt code 100000 does not fit the 15-bit address field` |
| `__besm6_maskpsw(x)` with a non-constant `x` | `__besm6_maskpsw: the mask must be a compile-time constant` |
| `__besm6_maskpsw(0100000)` | `__besm6_maskpsw: mask 100000 does not fit the 15-bit address field` |

All three immediate arguments are evaluated, range-checked and folded to a literal in the **front
end** ([semantic/expressions.c](https://github.com/besm6/c-compiler/blob/main/semantic/expressions.c),
`fold_immediate_arg0`), so they reach the back end as constants whatever the optimizer does. That is
where the language's recursive constant-expression evaluator lives, which is why nesting depth does
not matter (§2.3). Instruction selection re-tests each one, but only as a backstop.

One thing is deliberately *not* an error: an `ext`/`mod` address too large for the 12-bit Format-1
offset field (above `07777`). No address in the peripherals map is that large, but rather than
truncate such a constant the compiler puts it in the C register with a `utc` — Format 2, whose own
address field is 15 bits — so `__besm6_ext(010000, 0)` is correct, just one instruction longer.

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

**Every `__besm6_` name must be intercepted.** All twelve collide under Madlen's 8-character
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

**Tier 1 and Tier 3** share their addressing. All three name their operand through the effective
address, `EA = (addr + M[reg] + C) mod 0100000`, and `emit_io_op` reaches it three ways — never
through an index register:

| Address | Setup | Instruction |
|---------|-------|-------------|
| constant ≤ `07777` | — | `ext N` |
| constant ≤ `077777` | `utc N` (Format 2, 15-bit field) | `ext` |
| anything else | `xts` the accumulator operand, then `15 wtc` | `ext` |

The third row is the general case, and it is the interesting one. The address is computed into A as
usual; then XTS (003) writes A to `mem[M[15]]`, bumps the stack pointer and loads the accumulator
operand — all in one instruction — and the `wtc` that follows runs in **stack mode** (`V = 0`,
`M = 017`), so it decrements the pointer and loads C from the word just pushed. Push and pop are
adjacent and balanced, both leave ω logical, and nothing but A and the stack is touched.

That costs exactly what an index register would have cost, and it folds where an index register
could not. The peephole's rule #32 ([Peephole_Rewrites.md](https://github.com/besm6/c-compiler/blob/main/docs/Peephole_Rewrites.md) §5.10)
rewrites the two shapes device code actually writes:

```
unsigned io(unsigned addr, unsigned acc)      unsigned tape(unsigned unit)
{ return __besm6_ext(addr, acc); }            { return __besm6_ext(unit + 0100, 0); }

  6 xta 1                                         xta
  6 wtc                                         6 wtc
    ext                                           ext 64
```

An address already in memory needs no round-trip at all — `wtc` reads memory itself, and being
Format 2 it reaches a global directly (`wtc dev`, no `utc` escape). A constant *added* to the
address belongs in the instruction's own offset field, which `EA = addr + C` adds back for free;
that fold is exact despite the 48-bit C-level addition, because truncation to 15 bits commutes with
addition. So the two common computed forms cost one instruction, the same as a constant — which is
why a driver may write `__besm6_ext(ctlr + EXT_DRUM1, cw)` instead of branching on `ctlr` to reach
two constant addresses ([mb.c](../kernel/dev/mb.c), [md.c](../kernel/dev/md.c)).

**Write the variable first.** The displacement fold matches the `xts =N` + `call b$uadd` pair that
an *unsigned* addend produces, and that pair is only emitted when the constant is the **right**
operand. `ctlr + EXT_DRUM1` folds to three instructions; `EXT_DRUM1 + ctlr` puts the constant in
the accumulator and the variable on the stack instead, which the matcher cannot fuse, and costs the
`b$uadd` call plus the stack round-trip — seven instructions for the same address:

```
__besm6_ext(ctlr + 01, cw)          __besm6_ext(01 + ctlr, cw)

  6 xta 1                             xta #1
  6 wtc                             6 xts
    ext 1                          13 vjm b$uadd
                                    6 xts 1
                                   15 wtc
                                      ext
```

Both are correct; only one is cheap. Check the disassembly when it matters.

**The r14 clobber.** An extracode sets `M[016]` — that is, r14 — from the effective address, so r14
does not survive one. Nothing can be live in it across the trap: r14 is caller-saved, and its ABI
role is the argument count, which a caller loads immediately before the `,call,`. The lowering
itself no longer goes anywhere near it.

**The PSW trio needs no new machinery at all.** `ita`, `ati` and `vtm` are instruction kinds the
backend already had, already modelled correctly by the peephole: the accumulator is marked unknown
after all three (conservative and right — `ita` clobbers A, `ati`/`vtm` do not), all three are
R-independent, and none is a basic-block boundary, nor should become one, since `ita` leaves ω
logical — the mode compiled code runs in — while `ati` and `vtm` keep it. The frame machinery is
likewise untouched: it gates on `reg == REG_AUTO`, and these carry `reg == 0`, so `ita 17` can
never be mistaken for a reference to frame slot 17. The only backend change outside
`intrinsics.c` is the Madlen emitter's raw-octal spelling of the register-0 `vtm` (§3.3).

**Three new instruction kinds** carry this in the backend IR ([besm6.asdl](https://github.com/besm6/c-compiler/blob/main/backend/besm6/besm6.asdl),
[besm.h](https://github.com/besm6/c-compiler/blob/main/backend/besm6/besm.h)): `BESM_IO_EXT`, `BESM_IO_MOD` and `BESM_IO_EXTRACODE`. The
privileged `mod` could not reuse the `BESM_MOD_*` prefix — that is already the C-register address
modification group, `BESM_MOD_UTC`/`BESM_MOD_WTC`, an entirely different instruction. The extracode
is `BESM_SHAPE_SPECIAL` with its opcode in the `opcode` field, because its mnemonic *is* its opcode
and every dialect writes that differently.

**Two peephole obligations** come with those kinds, and both are the sort that miscompiles silently
if missed (see [Peephole_Rewrites.md](https://github.com/besm6/c-compiler/blob/main/docs/Peephole_Rewrites.md) §5.11):

- `BESM_IO_EXT`/`BESM_IO_MOD`/`BESM_IO_EXTRACODE` are **basic-block boundaries**. `ext`/`mod`
  rewrite the AU mode register R (a read address switches it to logical), which is the very
  register rules #29(a)/(b) track; the extracode runs arbitrary monitor code before returning.
- `BESM_BRANCH_STOP` is deliberately **not** a terminator, so rule #31(b) does not open an
  unreachable run at it. The halt is resumable.

**Per-dialect spelling** is where the three emitters diverge:

| Dialect | `ext` | `mod` | halt | extracode | mode write |
|---------|-------|-------|------|-----------|------------|
| Madlen | `,ext,` | `,mod,` | `,33,` (raw octal — no mnemonic) | `,*74,` | `,24,` (raw octal — modifier 0 rejected) |
| Bemsh | `увв` | `рег` | `стоп` | `э74` | `уиа` |
| b6as | `ext` | `mod` | `stop` | `$74` | `vtm` |

Every numeric address field is **decimal** in all three, so the octal addresses of the peripherals
map come out converted: `__besm6_ext(04031, …)` emits `,ext, 2073`, `__besm6_mod(0237, 0)` emits
`,mod, 159`, `__besm6_stop(0377)` emits `,33, 255`, and `__besm6_maskpsw(02003)` emits `,24, 1027`.
Note the two raw-octal cells, and that they are the same phenomenon: Madlen declines to *name* both
of the Format-2 forms this header needs, so both are written as machine code. `b6as` — the
assembler the kernel is actually built with — names every one of them.

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
- **The PSW trio likewise, for a different reason.** These are not privileged instructions — they
  assemble and execute — but neither user-level simulator implements what they *mean*: `dubna` and
  `b6sim` both compute `M[Aex & 017]`, so `ita 021` reads index register M[1] and `ati 021` would
  clobber it, and both repurpose the register-0 `vtm` as an instruction-trace toggle. Running one
  would not test the intrinsic; it would test the simulator's private meaning for the encoding. So
  the coverage is golden assembly in all three dialects plus `UnixAssembleIntrinsicPsw` (b6as +
  b6ld). The instructions themselves are exercised for real on SIMH, where this kernel's four gates
  have been running them since the boot came up, and now the `spl*` routines
  ([intr.c](../kernel/intr.c)) with them — every `spl*` the kernel takes is a `vtm` from
  `__besm6_maskpsw`, and `kernel/test/`'s `uclock`, `usys` and `utrap` assert on the resulting
  level.

---

## 10. Summary table

| Intrinsic | Op | Cyrillic | Constant arg | Notes |
|-----------|----|----------|--------------|-------|
| `__besm6_ext` | `033` | увв | — | privileged; never eliminable |
| `__besm6_mod` | `002` | рег | — | privileged; never eliminable |
| `__besm6_stop` | `033` fmt 2 | стоп | `code` | resumable — not `_Noreturn` |
| `__besm6_getpsw` | `042` | счи | — | PSW is `M[021]`; the only readable machine register |
| `__besm6_setpsw` | `040` | уи | — | the general mode-word write |
| `__besm6_maskpsw` | `024` reg 0 | уиа | `mask` | БлП/БлЗ/БлПр at once; Madlen writes it `,24,` |
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
