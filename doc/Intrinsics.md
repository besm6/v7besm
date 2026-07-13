# BESM-6 Compiler Intrinsics

This is a **proposal**: the set of compiler intrinsics (builtins) that the BESM-6 C compiler
should grow so that the Unix kernel — and the runtime library — can be written in C instead of
assembly. It specifies each intrinsic's name, signature, semantics and hazards. It deliberately
says nothing about *how* to implement them inside the compiler; that is a separate design.

Everything below is **octal** unless marked otherwise, and bits are numbered **right-to-left
from 1** (bit 1 = LSB, bit 48 = MSB), as everywhere else in this project.

Companion reading, in the order it becomes relevant:
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) (what the instructions do),
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) (how a C scalar sits in a word),
[Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) (the ω-mode / `NTR 3` contract),
[Besm6_Peripherals.md](Besm6_Peripherals.md) (the `033`/`002` address map),
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) (the C ABI).

---

## Table of contents

1. [Why intrinsics](#1-why-intrinsics)
2. [Ground rules](#2-ground-rules)
3. [Tier 1 — privileged: reaching the hardware](#3-tier-1--privileged-reaching-the-hardware)
4. [Tier 2 — the AU mode register R](#4-tier-2--the-au-mode-register-r)
5. [Tier 3 — bit manipulation](#5-tier-3--bit-manipulation)
6. [Tier 4 — double-length results and the Y register](#6-tier-4--double-length-results-and-the-y-register)
7. [Tier 5 — floating point and the exponent](#7-tier-5--floating-point-and-the-exponent)
8. [Tier 6 — extracodes](#8-tier-6--extracodes)
9. [Deliberately excluded](#9-deliberately-excluded)
10. [Worked examples](#10-worked-examples)
11. [The header `<besm6.h>`](#11-the-header-besm6h)
12. [Open questions](#12-open-questions)
13. [Summary table](#13-summary-table)

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
`anx` (highest set bit) — that the kernel's bitmaps and page tables want, and a `Y` register
carrying the low half of every double-length result that C cannot see.

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

### 2.2 Every intrinsic returns with `R = 7`

Compiled C code runs under a standing contract on the AU mode register `R`: **`NTR 3`**
(normalization *and* rounding suppressed — the integer-arithmetic configuration) with **logical ω**
layered on top, which reads as `R = 7`. The compiler relies on it: a `uza`/`u1a` following any
value test only means "is A zero?" while ω is logical. See
[Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) § *ω mode and the AU mode register R*.

Several of the instructions below leave a *different* ω:

| Instruction | ω it leaves |
|-------------|-------------|
| `arx` (013), `a*x` (017), `e+x`/`e-x` (024/025), `e+n`/`e-n` (034/035) | multiplicative |
| `avx` (014), `amx` (007) | additive |

**An intrinsic wrapping one of these must restore logical ω before it yields its value**, by
appending a no-op logical operation — an `aox` of word `0`, which ORs zero into A without changing
it. This is exactly what `b$uadd`, `b$usub` and `b$pdiff` already do for the same reason. An
ω-setting op rewrites only bits 5–3 and preserves the `NTR 3` suppression bits, so nothing else
needs restoring.

The **only** exceptions are `__besm6_ntr` and `__besm6_xtr` (§4), whose entire purpose is to change
`R`. They break the invariant on purpose, and it is the caller's job to restore it.

### 2.3 Constant versus computed arguments

Some arguments become an *immediate field of the instruction word* and must therefore be
compile-time constants; the compiler must diagnose a non-constant. Others may be computed, at the
cost of materialising them into an index register first.

| Argument | Must be constant? |
|----------|-------------------|
| `__besm6_ntr(r)` — the new `R` | **yes** (`EA[6:1]`) |
| `__besm6_asn_y(a, n, …)` — the shift count | **yes** (`EA[7:1]`, so `n` ∈ [−64, 63]) |
| `__besm6_scale(x, n)` — the exponent delta | **yes** (`EA[7:1]`, so `n` ∈ [−64, 63]) |
| `__besm6_extracode(op, …)` — the opcode | **yes** (it *is* the opcode) |
| `__besm6_ext(addr, …)` / `__besm6_mod(addr, …)` — the register address | **no** — see below |

`ext` and `mod` are Format-1 instructions, whose offset field is 12 bits (`07777` maximum). Every
address in the map fits: `033` reaches `04177`, `002` reaches `0237`. So a constant address is
always an immediate, and the Format-1 `S` bit (bit 19, which would add `070000`) is never needed.

A **computed** address must nonetheless be supported, because the hardware genuinely uses one:
`002 0100`–`0137` (РУУ mode bits) encodes its *data in the address*, and tape-transport control
(`033 0100`–`0137`) selects the unit as `addr − 0100`. The compiler materialises the value into an
index register and emits `<reg> ext 0` — the effective address is `M[reg] + offset`, so this works
unchanged.

### 2.4 There is no bare `__besm6_yta()`

The Y register (RMR — регистр младших разрядов) holds the low half of every double-length result.
It is also **destroyed by almost every instruction**: `Y = 0` after `aax`, `aox`, `arx`, `avx`,
`apx`, `aux`, `acx`, `e±x`, `e±n`, `asx`, `asn`; `Y = A` after `aex` and after *both* conditional
branches `uza`/`u1a`; undefined after `a/x`. Its only reader, `yta` (031), behaves differently
depending on the current ω.

A standalone `__besm6_yta()` would therefore be unusable: any spill, reload, or reordering the
compiler inserts between the producing intrinsic and the `yta` silently destroys the value, and
nothing in C's semantics forbids that. Y is instead exposed only through **fused** intrinsics that
produce both halves at once and take the second one through an out-parameter (§6). In those, the
producing instruction and its `yta` are a single indivisible unit.

### 2.5 What is *not* an intrinsic: absolute machine addresses

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

### 2.6 There is no atomic instruction

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

## 4. Tier 2 — the AU mode register R

`R` is six bits: overflow suppression (bit 6), the ω mode (bits 5–3), rounding suppression (bit 2),
normalization suppression (bit 1). C cannot see it, yet it decides what arithmetic *means*.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `besm6_word __besm6_rte(unsigned mask)` | `030` `rte` счрж | `result := (R & mask & 0177) << 41` |
| `void __besm6_xtr(besm6_word w)` | `027` `xtr` рж | `R := w[47:42]` |
| `void __besm6_ntr(unsigned r)` | `037` `ntr` ржа | `R := r` — `r` must be a constant, 0–077 |

`__besm6_rte` and `__besm6_xtr` are a **symmetric save/restore pair**, and deliberately so: `rte`
deposits `R` into bits 47–42 of the accumulator, which is precisely the field `xtr` reads it back
from. No shifting is needed at either end.

```c
besm6_word saved = __besm6_rte(077);    /* capture all six bits of R */
__besm6_ntr(0);                         /* full FP mode: normalize + round */
... floating-point work ...
__besm6_xtr(saved);                     /* put R back */
```

> **These three break the `R = 7` contract of §2.2 on purpose, and the compiler will not fix it
> for you.** Between an `__besm6_ntr(0)` and the matching restore, ordinary C arithmetic in the same
> function is compiling against an `R` it does not expect: integer adds may normalize, and a
> comparison may test the wrong ω. Keep such regions short, straight-line, and free of anything but
> the operations you intended. This is the sharpest edge in the whole proposal.

---

## 5. Tier 3 — bit manipulation

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
natural tool for building the page-register word of §10.4 and for laying out device control words.

### `__besm6_acx` — population count (чед)

`popcount(a)` added to `x` with end-around carry. Pass `x = 0` for a plain population count; the
header wraps that as `b6_popcount`.

### `__besm6_anx` — highest set bit (нед)

Returns the *position* of `a`'s highest set bit, **numbered from the MSB**: bit 48 → 1, bit 47 → 2,
…, bit 1 → 48. That position is then added to `x` with end-around carry. **If `a` is zero the
result is just `x`** — there is no distinguished "not found" value, so the caller must test for zero
first. With `x = 0` this is a count-leading-zeros plus one; the header wraps it as `b6_clz`.

`anx` also leaves the remainder of `a` — everything below the found bit, shifted up to the MSB — in
Y; see `__besm6_anx_y` in §6.

### `__besm6_arx` — cyclic add (слц)

The raw end-around-carry add, useful for checksums. Note it leaves **multiplicative** ω, so per
§2.2 the intrinsic appends a logical no-op before yielding.

---

## 6. Tier 4 — double-length results and the Y register

These are the **only** sanctioned way to read Y (§2.4). Each takes the second result through an
out-parameter, and the producing instruction together with its `yta` forms one indivisible unit:
nothing may be scheduled between them.

| Intrinsic | Ops | Semantics |
|-----------|-----|-----------|
| `besm6_word __besm6_asn_y(besm6_word a, int n, besm6_word *out)` | `036`+`031` | shift `a` by constant `n`; `*out` = the bits shifted out |
| `besm6_word __besm6_asx_y(besm6_word a, besm6_word s, besm6_word *out)` | `026`+`031` | same, shift count from the exponent field of `s` |
| `besm6_word __besm6_anx_y(besm6_word a, besm6_word x, besm6_word *rem)` | `023`+`031` | as `__besm6_anx`; `*rem` = the remainder below the found bit |
| `double __besm6_mul_y(double a, double b, double *lo)` | `017`+`031` | full product: result = high half, `*lo` = low half |

### The shifts

`asn`/`asx` are really a **96-bit `[A:Y]` shift**. `n > 0` shifts right, and the bits leaving A's
LSB accumulate in Y from the MSB down; `n < 0` shifts left by `−n`, and the bits leaving A's MSB
accumulate in Y from the LSB up. Y is cleared before the shift begins.

Plain C `<<` and `>>` on an `unsigned` already compile to `asn`/`asx` — so a non-Y shift intrinsic
would earn nothing and is not proposed. **Only the Y-capturing form is new**, and it is what
multi-word shifts and byte insert/extract are built from.

`__besm6_asx_y` takes its count the way the hardware does — from bits 48–42 of `s`, minus 64. That
is not a convenience; it is exactly the encoding a fat pointer already carries (see
[Besm6_Data_Representation.md](Besm6_Data_Representation.md) §7), so a fat pointer can be fed to it
directly.

### The multiply

`a*x` produces an 81-bit product mantissa: A receives the upper 41 bits, `Y[40:1]` the lower 40.
`__besm6_mul_y` returns the first and stores the second, correctly weighted — the `yta` carries the
exponent adjustment that gives the low half a weight of 2⁻⁴⁰ relative to the high half. `yta` in
multiplicative ω does `E += N − 64` where `N = EA[7:1]`, so the adjustment wanted is `N − 64 = −40`,
i.e. **`yta 030`** (octal 030 = 24 decimal). The pair is an exact product: `result + *lo` loses
nothing.

This is the primitive an extended-precision `libm` needs, and the reason it must be an intrinsic is
that `a * b` in C discards Y before you can look at it.

> Note that `a/x` (016 дел) leaves Y **undefined** — there is no division counterpart, and none is
> proposed. Note also that `a/x` traps if the divisor is not normalized, which includes zero *and*
> every denormal.

---

## 7. Tier 5 — floating point and the exponent

The BESM-6 float is not IEEE 754 and has no NaN, no infinity and no denormals. These four
intrinsics expose the operations `libm` would otherwise have to synthesise.

| Intrinsic | Op | Semantics |
|-----------|----|-----------|
| `double __besm6_scale(double x, int n)` | `034`/`035` `e+n`/`e-n` слпа/вчпа | `x × 2ⁿ`; `n` constant, ∈ [−64, 63] |
| `double __besm6_scalex(double x, double s)` | `024`/`025` `e+x`/`e-x` слп/вчп | `x × 2^(exponent of s − 64)` |
| `double __besm6_avx(double a, double x)` | `014` `avx` знак | negate `a` if `x` is negative (bit 41 of `x` set) |
| `double __besm6_amx(double a, double x)` | `007` `amx` вчаб | `|a| − |x|` |

`__besm6_scale` is the fast path for `ldexp`/`scalbn`: a single instruction that adds to the
exponent field without touching the mantissa. The compiler picks `e+n` or `e-n` from the sign of
`n`; both take the amount from `EA[7:1]` as `N − 64`, so the two are one operation with a negated
argument, and one intrinsic covers both. `__besm6_scalex` is the same with a computed amount, taken
from the exponent field of a second operand.

`__besm6_avx` is a `copysign` primitive; `__besm6_amx` a magnitude comparison that costs one
instruction instead of two `fabs` calls and a subtract. Both leave **additive** ω and so, per §2.2,
append a logical no-op.

---

## 8. Tier 6 — extracodes

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

## 9. Deliberately excluded

| Not proposed | Why |
|--------------|-----|
| `aax` (011), `aox` (015), `aex` (012) | C already has `&`, `\|`, `^`. |
| `a+x`, `a-x`, `x-a`, `a*x`, `a/x` | C already has `+`, `-`, `*`, `/`. |
| `asn`/`asx` without Y | C's `<<` and `>>` on `unsigned` already compile to exactly these. Only the Y-capturing forms (§6) add anything. |
| `xta` (010), `atx` (000) | Loads and stores. Absolute machine addresses are `volatile unsigned *` — the machine is word-addressed, so a pointer *is* a word index (§2.5). |
| `ati`, `ita`, `mtj`, `j+m`, `vtm`, `utm` | The index registers belong to the compiler. |
| `utc` (022), `wtc` (023) | The `C` address-modification register is compiler-internal — it is how *every* 15-bit symbolic global is reached. |
| `xts`, `stx`, `its`, `sti`; stack-mode addressing | The stack belongs to the compiler. |
| `uza`, `u1a`, `uj`, `vjm`, `vzm`, `v1m`, `vlm` | Control flow belongs to the compiler. |
| A bare `__besm6_yta()` (031) | Y is destroyed by almost every instruction; a reader the compiler is free to schedule is a reader that silently returns garbage. Use the fused forms of §6. |
| `ij` (Format 2 `032`, выпр) | Return-from-interrupt is a control transfer that restores the whole machine state, including the privilege level. It cannot be a C function call. Stays in [kernel/besm6.S](../kernel/besm6.S). |
| The trap frame, `save`/`resume`, `nofault` | `save`/`resume` is a two-return protocol (`save` returns 0 directly and nonzero when resumed); `nofault` is a recovery-PC discipline shared between an inline sequence and the trap dispatcher. Neither has a C shape. See [Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md). |
| Format-1 `032` (interrupt control) | Semantics undocumented in our references — see §12. Reachable as raw `$32` in assembly. |
| `046`, `047` | Always illegal, in both modes. |

---

## 10. Worked examples

These are what the proposal buys. All four are assembly today.

### 10.1 `spl` — the interrupt priority level

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

### 10.2 The interrupt dispatcher

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

### 10.3 Reading a drum page

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
derives field by field.

The `U` suffix on `GRP_DRUM1_FREE` is not optional — bit 46 does not fit in a signed literal.

### 10.4 Packing the page registers

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

## 11. The header `<besm6.h>`

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

/* Tier 2 -- the AU mode register */
besm6_word __besm6_rte(unsigned mask);
void       __besm6_xtr(besm6_word w);
void       __besm6_ntr(unsigned r);     /* r must be a constant */

/* Tier 3 -- bit manipulation */
besm6_word __besm6_apx(besm6_word a, besm6_word mask);
besm6_word __besm6_aux(besm6_word a, besm6_word mask);
besm6_word __besm6_acx(besm6_word a, besm6_word x);
besm6_word __besm6_anx(besm6_word a, besm6_word x);
besm6_word __besm6_arx(besm6_word a, besm6_word x);

/* Tier 4 -- double-length results */
besm6_word __besm6_asn_y(besm6_word a, int n, besm6_word *out);  /* n constant */
besm6_word __besm6_asx_y(besm6_word a, besm6_word s, besm6_word *out);
besm6_word __besm6_anx_y(besm6_word a, besm6_word x, besm6_word *rem);
double     __besm6_mul_y(double a, double b, double *lo);

/* Tier 5 -- floating point */
double __besm6_scale(double x, int n);  /* n constant */
double __besm6_scalex(double x, double s);
double __besm6_avx(double a, double x);
double __besm6_amx(double a, double x);

/* Tier 6 -- extracodes */
besm6_word __besm6_extracode(int op, unsigned ea, besm6_word acc);  /* op constant */

/* ------------------------------------------------------------------ */
/* Conveniences */

#define b6_popcount(a)   __besm6_acx((a), 0)
#define b6_highbit(a)    __besm6_anx((a), 0)     /* 1 = bit 48 … 48 = bit 1; 0 -> 0 */

#define b6_grp_read()    __besm6_mod(0237, 0)
#define b6_grp_mask(m)   ((void) __besm6_mod(036, (m)))
#define b6_grp_clear(m)  ((void) __besm6_mod(037, ~(besm6_word)(m)))

#define b6_rau_save()    __besm6_rte(077)
#define b6_rau_restore(s) __besm6_xtr(s)

#endif /* _BESM6_H */
```

The kernel additionally wants the ГРП/ПРП bit names and the per-device control-word field
definitions from [Besm6_Peripherals.md](Besm6_Peripherals.md). Those are hardware constants, not
intrinsics, and belong in the kernel's own include tree.

---

## 12. Open questions

1. **What does Format-1 opcode `032` do?** [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §8
   lists it as "interrupt control, kernel mode only" and gives no operand semantics; it has no
   mnemonic in [cmd/as/tables.c](../cmd/as/tables.c) and must be written `$32`. If it turns out to
   control the interrupt-enable flag, the kernel needs it and it becomes a Tier-1 intrinsic. This
   is a documentation gap worth closing before the trap handler is written.
2. **The extracode encoding.** [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §6 lists the
   extracodes as "opcodes 050–077, **020, 021**" — but 020 and 021 are already `apx` and `aux` in
   Format 1. The parenthetical "(0200)"/"(0210)" suggests a different encoding. This must be
   resolved before `__besm6_extracode`'s valid range can be specified.
3. **Normalization test.** §2 of the instruction-set document says a normalized number has bits 41
   and 40 different; §6 and §7 both say the test is on bits **42 and 41**, and the emulator code
   they quote (`(X ^ (X << 1)) & BIT41`) agrees with 42/41. The two statements cannot both be right.
   Nothing in this proposal depends on the answer, but `__besm6_mul_y` and Tier 5 do.

Not open: the Format-1 `S` bit (bit 19, `+070000`) is **never** needed for an `ext` or `mod`
address. The highest address in the map is `033 4177`, which fits in the 12-bit offset field.

---

## 13. Summary table

| Intrinsic | Op | ω left by the instruction | Constant arg |
|-----------|----|---------------------------|--------------|
| `__besm6_ext` | `033` увв | logical on read; unchanged on write | — |
| `__besm6_mod` | `002` рег | logical on read; unchanged on write | — |
| `__besm6_stop` | `033` fmt 2 стоп | — | — |
| `__besm6_rte` | `030` счрж | logical | — |
| `__besm6_xtr` | `027` рж | **as set** | — |
| `__besm6_ntr` | `037` ржа | **as set** | `r` |
| `__besm6_apx` | `020` сбр | logical | — |
| `__besm6_aux` | `021` рзб | logical | — |
| `__besm6_acx` | `022` чед | logical | — |
| `__besm6_anx` | `023` нед | logical | — |
| `__besm6_arx` | `013` слц | multiplicative → **fix up** | — |
| `__besm6_asn_y` | `036`+`031` | logical | `n` |
| `__besm6_asx_y` | `026`+`031` | logical | — |
| `__besm6_anx_y` | `023`+`031` | logical | — |
| `__besm6_mul_y` | `017`+`031` | multiplicative → **fix up** | — |
| `__besm6_scale` | `034`/`035` слпа/вчпа | multiplicative → **fix up** | `n` |
| `__besm6_scalex` | `024`/`025` слп/вчп | multiplicative → **fix up** | — |
| `__besm6_avx` | `014` знак | additive → **fix up** | — |
| `__besm6_amx` | `007` вчаб | additive → **fix up** | — |
| `__besm6_extracode` | `$50`–`$77` | logical | `op` |

"Fix up" means the intrinsic must append a no-op logical operation to restore `R = 7` before
yielding its value (§2.2).

Of these, only `ext` and `mod` are absent from the compiler's back-end instruction table; every
other mnemonic named above is already there, and all of them — plus `ij` and `stop` — are already
assembled by [cmd/as/tables.c](../cmd/as/tables.c).

---

## See also

* [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — the opcodes, the registers, the ω mode.
* [Besm6_Peripherals.md](Besm6_Peripherals.md) — the `033`/`002` address map and the ГРП/ПРП bits.
* [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — why the word type is `unsigned`.
* [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) — the `NTR 3` / logical-ω contract.
* [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the C ABI these plug into.
* [Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md) — what stays in assembly regardless.
