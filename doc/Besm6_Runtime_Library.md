# BESM-6 Runtime Library (compiler-support routines)

These routines are emitted automatically by the compiler. They implement the C calling
convention and the operators that have no direct BESM-6 instruction equivalents. They are
distinct from the *user-callable standard library* (`printf`, `malloc`, `strlen`, …);
for that, see
[Standard_Include_Files.md](https://github.com/besm6/c-compiler/blob/main/docs/Standard_Include_Files.md).

For a detailed description of the calling convention, see
[Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md).
For the integer storage model, see
[Besm6_Data_Representation.md](Besm6_Data_Representation.md).

> **Where the sources live.** The runtime is part of the external
> [c-compiler](https://github.com/besm6/c-compiler/) repository, under
> [`libc/besm6/unix/`](https://github.com/besm6/c-compiler/tree/main/libc/besm6/unix) — the `.s`
> files linked from each section below, written in the syntax `b6as` assembles (see
> [Assembler_Manual.md](Assembler_Manual.md)). Helper names use `$`: the C-visible symbol for the
> multiply helper is `b$mul`.
>
> The same directory also holds the system-call leaves and start-up code (`crt0.s`, `read.s`,
> `write.s`, `exit.s`). Those are *libc*, not compiler-support routines, and are not described
> here. In particular there is no output helper in this runtime: a program reaches stdout through
> the `write` system call (`$77 4`, see [Aout_Simulator.md](Aout_Simulator.md)).

## Integer model

C integers are stored as raw words with the exponent field (bits 48–42) = 0:

- **Signed `int`**: 41-bit sign-magnitude; bit 41 = sign, bits 40:1 = magnitude.
  Range −2⁴⁰ … 2⁴⁰−1.
- **Unsigned `int`**: full 48-bit unsigned value. Range 0 … 2⁴⁸−1.

`b$save` leaves the AU mode register **R = 7** (binary `000111`):

- Bits 5–3 = `001`: **Logical** ω mode — after logical operations (XOR, AND, OR, loads),
  `UZA`/`U1A` test whether A = 0 / A ≠ 0.
- Bit 2 = 1: suppress rounding after normalization.
- Bit 1 = 1: suppress normalization after arithmetic.

After subtraction instructions (`A-X`, `X-A`, `A+X`), the hardware automatically switches
R to **Additive** ω mode (bits 5–3 = `100`): `UZA` then branches when A ≥ 0 (bit 41 = 0),
`U1A` when A < 0 (bit 41 = 1).

The arithmetic helpers (`b$mul`, `b$div`, `b$mod`, and their unsigned counterparts)
borrow the FP unit by temporarily converting operands to **INT-format** (exponent field
set to `0150B` = 104 decimal), which places the mantissa where `A*X` / `A/X` expect it.
This is a transient representation used only inside those helpers; it is never the
storage format for C values. In the sources the INT-format exponent is the constant-pool
literal `#0'64`, and the exponent-strip mask is `#037'7777'7777'7777`.

---

## Runtime Helper Convention

All arithmetic, comparison, and conversion helpers use a lightweight calling convention
distinct from the full C ABI:

- **First operand `a`**: at the stack top — r15 points one word past it, so `a` is at
  `mem[r15−1]`.
- **Second operand `b`**: in the accumulator (A).
- **Result**: left in A; r15 is not modified (the compiler adjusts the stack after the
  call).
- **Register contract**: helpers may freely modify scratch index registers (including r14)
  but must preserve r6 and r7.
- **Invocation**: `13 vjm b$xxx` — the jump leaves the return address in r13, and the helper
  returns with `13 uj`. A helper does **not** run the `its 13` / `b$save` prologue that a C
  function runs.

`b$save`, `b$save0`, and `b$ret` follow the full C ABI described below; they are separate
from the helper convention.

---

## ω mode and the AU mode register R

Every helper runs under a contract on the AU mode register `R` with **two independent
axes** (see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §4 for the bit layout):

- **NTR / suppress mode (bits 1–2): `NTR 3`** — normalization *and* rounding disabled.
  This is the integer-arithmetic configuration and it holds **at entry and on exit**.
- **ω mode (bits 3–5): unknown on entry, logical on exit.** On entry ω is whatever the
  caller's last ALU op left; on exit it must be **logical** so the caller's following
  `uza`/`u1a` test the value (A = 0?) rather than its sign or magnitude.

So the contract is: **enter with `NTR 3` / ω unknown, exit with `NTR 3` / ω = logical.**

`b$save` writes `ntr 7`, and `7 = 3 | 004` — i.e. `NTR 3` suppression **plus** the
logical-ω bit. So the register literally *reads* 7 right after `b$save` and again at every
conforming helper's exit; the part that defines the contract and survives arbitrary
ω-setting ops is the `NTR 3` suppression, with logical ω layered on top.

**Why the two axes are independent.** An ω-setting instruction (any logical / additive /
multiplicative op) rewrites **only** the ω-group bits 3–5 and *preserves* the `NTR 3`
suppress bits (this is the dubna emulator's `set_logical`/`set_additive`/
`set_multiplicative`, which mask with `RAU_MODE` = 034). Two consequences:

- A helper that never disturbs the suppress bits keeps `NTR 3` for free; it only has to
  make sure its **last** ALU op is logical so ω ends logical. Most helpers (compares,
  shifts, byte access, pointer inc/dec) get this automatically.
- A helper that drops to full FP mode (a bare `ntr` = `R := 0`) for a normalize/round/divide
  step must restore the suppress bits before returning. There are two idioms:
  - **explicit:** end with `ntr 7` *after* the final conditional branch (the FP
    comparisons and `b$utod`). `NTR` overwrites the ω flag, so it must follow any
    `uza`/`u1a` that tests the result.
  - **implicit:** end with `ntr 3` (suppress restored, ω = none) followed by a final
    logical op — a masking `aax`, or a no-op `aox` that ORs `mem[0] = 0` into A without
    changing it. The logical op sets ω = logical while leaving the suppress bits, so the
    register lands on `R = 7` (`b$div`, `b$mod`, `b$mul`, `b$dtoi`, `b$dtou`, `b$padd`).

**Exceptions** are noted at each helper below. The cases worth flagging are the unsigned
arithmetic helpers and `b$pdiff`, whose natural last op is a cyclic-add (`arx`, →
multiplicative ω) or a subtract (`a-x`, → additive ω); each appends a no-op `aox` to land
back on logical ω. `b$uneg` additionally prepends an `aox` so its leading zero test runs
in logical ω regardless of the caller's entry ω. `b$ret` is the C-ABI epilogue and is
discussed with the ABI routines.

---

## `b$save` — [b_save.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_save.s)

Called on entry to every C function that has **one or more parameters**.

The compiler emits:

```
    its 13          // push return-to-caller address (in r13) onto the stack
 13 vjm b$save      // call b$save; r13 <- address of the first instruction of the body
```

**Source walkthrough:**

```
 15 j+m 14          // r15 += r14  (r14 = -N; rewinds r15 past the N argument slots)
    its 7           // push r7 (caller's auto-variable pointer)
    its 6           // push r6 (caller's parameter pointer)
    its 5           // push r5 (caller's scratch register)
    its             // push A  (= last argument argN, passed in the accumulator)
 14 mtj 6           // r6 = r14  (set parameter pointer; r6+i addresses param[i])
 15 mtj 7           // r7 = r15  (set auto-variable pointer; r7+j addresses local[j])
    ntr 7           // R = 7: logical omega + suppress normalization + suppress rounding
 13 uj              // jump to r13 (the function body's first instruction)
```

**After `b$save`:**

- r6 = parameter pointer; `r6 + i` addresses the i-th argument.
- r7 = auto-variable pointer; `r7 + j` addresses the j-th local variable.
- The stack holds the saved r7, r6, r5 from the caller, plus argN.
- R = 7 (integer arithmetic mode).

---

## `b$save0` — [b_save0.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_save0.s)

Called on entry to every C function with **no parameters**.

The compiler emits the same `its` / `vjm` prologue as for `b$save`. Because no arguments
were pushed, `b$save0` synthesises a valid r6 without the `j+m` adjustment:

```
 15 mtj 14          // r14 = r15  (save current stack top into r14)
 14 utm -1          // r14 -= 1   (one below the current stack top)
    its 7           // push r7
    its 6           // push r6
    its 5           // push r5
    its             // push A     (no meaningful last argument; frame stays uniform)
 14 mtj 6           // r6 = r14   (parameter pointer set to the synthesised base)
 15 mtj 7           // r7 = r15
    ntr 7           // R = 7
 13 uj              // continue to function body
```

The synthesised r14/r6 value makes the frame layout identical to a one-argument call,
so `b$ret` can use the same unwind calculation regardless of parameter count.

---

## `b$ret` — [b_ret.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ret.s)

Called at every exit point of a C function. The return value must be in A before
jumping to `b$ret`.

```
  6 mtj 14          // r14 = r6  (save parameter-base index for stack unwind)
  7 mtj 15          // r15 = r7  (reset stack pointer to the saved-register block)
  7 stx -5          // locate the saved-register block relative to r7
    sti 5           // pop saved r5
    sti 6           // pop saved r6  (caller's parameter pointer)
    sti 7           // pop saved r7  (caller's auto-variable pointer)
    sti 13          // pop saved return address into r13
 14 mtj 15          // r15 = r14  (restore caller's pre-argument stack level)
 13 uj              // return to caller
```

**Actions:** restores the caller's r5, r6, r7, and r13; unwinds r15 to the level it had
before any arguments were pushed; then jumps to r13.

The `7 stx -5` stashes the return value at the bottom of the saved-register block; the
four `sti` pops then restore r5/r6/r7/r13 and the *last* pop reloads that stashed value
back into A — so the return value survives the unwind.

*ω/NTR:* `b$ret` issues no `NTR` and does no arithmetic, so the `NTR 3` suppress bits pass
through from the function body (which must have kept them). The `sti` pop-chain is logical,
so A is delivered to the caller in **ω = logical**. This is the C-ABI epilogue, not a
lightweight helper, but it honours the same exit state.

---

## Signed Integer Arithmetic

Operands are raw 48-bit words with exponent field = 0. The signed helpers convert them to
**INT-format** by ORing in the base exponent `#0'64` (which sets the bits of the 7-bit
exponent field to the value `0150B` = 104 decimal). This allows the hardware FP multiply
and divide units to operate on the integer values. After the operation, the product or
quotient exponent is corrected with `a+x #0'64`, and the exponent field is stripped with
`aax #037'7777'7777'7777` — a 42-bit mask (14 octal digits) that zeroes bits 48–42 and
leaves the 41-bit signed result (sign bit 41 + 40-bit magnitude) in A.

### `b$mul` — [b_mul.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_mul.s)

Computes `a * b` (signed, low 41-bit result).

```
    aox #0'64                   // A |= INT-exponent  ->  b in INT-format
 15 stx                         // exchange A with mem[r15-1]: A <- a (raw), stacks INT-form b
    aox #0'64                   // A |= INT-exponent  ->  a in INT-format
    ntr 2                       // R = 2: enable normalization (required for FP multiply)
 15 a*x 1                       // A <- INT-form a x INT-form b  (reads the stacked INT-form b)
    ntr 3                       // R = 3: restore suppress-normalization + suppress-rounding
    a+x #0'64                   // correct product exponent (subtract the doubled INT-exponent)
    aax #037'7777'7777'7777     // mask to 41-bit signed result (strip exponent field)
 13 uj                          // return
```

`b$mul` is **signed only**: the INT-format bridge interprets bit 48 as the operand's sign,
so it is valid only where each operand fits the 41-bit signed range. Unsigned multiply over
the full 48-bit range uses `b$umul` (see *Unsigned Integer Arithmetic* below).

*ω/NTR:* the helper enables normalization for the FP multiply (`ntr 2`), then returns the
suppress bits with `ntr 3`; the closing `aax` mask is a logical op, so the exit lands on
**`NTR 3` / ω = logical** (`R = 7`) — the implicit-restore idiom.

### `b$div` — [b_div.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_div.s)

Computes `a / b` (signed, truncated toward zero — C11 §6.5.5).

The hardware FP divide leaves the quotient as a two's-complement mantissa, so masking off
the fraction rounds toward −∞ (floors). To get C truncation, the helper divides the
**absolute values** and reapplies the sign:

1. Convert `b` to INT-format (`aox #0'64`) and record the sign word `a ^ b` (its bit 41 is
   `sign(a) ^ sign(b)`).
2. `avx` against each INT-format operand takes its absolute value and normalizes it (the
   FP divisor must be normalized).
3. `a/x` divides `|a| / |b|`; `a+x #0'64` corrects the exponent and
   `aax #037'7777'7777'7777` masks to the 41-bit result. Because the operands are
   non-negative, this truncates toward zero.
4. `avx` against the saved sign word negates the quotient when the operand signs differ.

*ω/NTR:* the divide runs in full FP mode (a bare `ntr`); the helper then sets `ntr 3` to
restore the suppress bits, and the **exponent-strip mask `aax`** — physically the last
instruction, after the sign-reapply `avx` — is itself a logical op, so it both extracts the
41-bit result *and* leaves **`NTR 3` / ω = logical** (`R = 7`). No separate `aox` is needed:
the logical ω the caller expects (otherwise the additive ω left by `avx` would invert the
next `uza`/`u1a`, e.g. printf's decimal-digit loop) is supplied by that mask.

### `b$mod` — [b_mod.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_mod.s)

Computes `a % b` (signed remainder; result takes the sign of the **dividend** — C11
§6.5.5).

Uses the same absolute-value approach as `b$div` via the identity
`|r| = |a| − (|a| ÷ |b|) · |b|`, then reapplies the sign of `a`:

Like `b$div`, it is **push-based**: the dividend `a` arrives on the stack (its raw word
doubles as the sign word), and the helper pushes the normalized moduli `|b|` and `|a|`,
addressing them by negative offset and dropping all three with a final `utm` (net pop of one
word).

1. Convert each operand to INT-format (`aox #0'64`) and `avx` it against its own pushed word
   to form the normalized modulus — `|b|` then `|a|` — leaving both on the stack.
2. `a/x` gives the truncated magnitude `|q| = |a| ÷ |b|` (the exponent-correction
   `a+x #0'64` under R = 3 drops the fraction; no separate mask is needed mid-computation).
3. `a*x` forms `|q| · |b|`; `x-a` against the still-saved `|a|` yields `|r|`.
4. `a+x #0'64` + `aax #037'7777'7777'7777` extract `|r|` as a raw integer.
5. `avx` against the raw dividend `a` reapplies its sign, then a trailing `aox` (OR with
   `mem[0] = 0`, A unchanged) restores logical ω.

*ω/NTR:* the divide and the `|q|·|b|` multiply/subtract run in full FP mode (a bare `ntr`),
bracketed back to `ntr 3` for the integer extractions. The sign-reapply `avx` leaves
additive ω, so unlike `b$div` (whose terminal *mask* `aax` doubles as the logical op) `b$mod`
appends an explicit no-op `aox`; the exit is **`NTR 3` / ω = logical** (`R = 7`).

---

## Unsigned Integer Arithmetic

The signed helpers' INT-format trick mishandles unsigned operands whose top bit (bit 48)
is set: the FP unit interprets bit 48 as the number's sign, producing incorrect results.
This affects multiply and divide (`b$umul`, `b$udiv`, `b$umod`).

Add and subtract fail for a related reason: signed `A+X`/`A-X` work only because raw
41-bit integers keep the exponent field (bits 48–42) = 0, so the additive unit adds the
mantissas directly. Full 48-bit unsigned values carry data in that field, which the
additive unit misreads as an exponent — so unsigned add/subtract need software helpers
(`b$uadd`, `b$usub`) that perform true 48-bit modular arithmetic.

Separate helpers are therefore required for the full 48-bit unsigned range.

### `b$uadd` — [b_uadd.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_uadd.s) — `a + b` (unsigned)

Receives two 48-bit unsigned values (`a` at `mem[r15−1]`, `b` in A). Returns the 48-bit
modular sum in A. Adds the two operands in 24-bit half-words with explicit carry propagation
from the low half into the high half, so the exponent-field bits participate as plain value
bits. Overflow wraps modulo 2⁴⁸.

*ω/NTR:* the helper drives the bit-48 tests with `ntr 8+3` (`R = 013`: multiplicative ω +
`NTR 3` suppression — `uza` then tests A[48]), so it does **not** depend on the entry ω. The
core sum is `arx`, which leaves *multiplicative* ω; each return path therefore ends with a
no-op `aox` (OR `mem[0] = 0`) to land on **`NTR 3` / ω = logical** (`R = 7`). (The two
single-`*large` paths already close with the bit-48-restore `aex`, a logical op.)

### `b$usub` — [b_usub.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_usub.s) — `a − b` (unsigned)

Returns the 48-bit modular difference `a − b` in A. Negates `b` (complement plus one) and
adds it to `a`, handling the bit-48 carry explicitly so the exponent-field bits participate
as plain value bits. Underflow wraps modulo 2⁴⁸.

*ω/NTR:* the entry `ntr 7` sets logical ω for the `b == 0` test (so the helper is
entry-ω-agnostic). The `b == 0` path closes with `stx` (logical); the general path tail-jumps
into `b$uadd` and inherits its restored exit. Either way: **`NTR 3` / ω = logical** (`R = 7`).

### `b$uneg` — [b_uneg.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_uneg.s) — `−b` (unsigned)

Unary negation over the full 48-bit unsigned range: returns `2⁴⁸ − b` (and `0` for `b = 0`).
Operand in A, result in A; nothing on the stack.

```
    aox                         // OR mem[0]=0: A unchanged, but omega := logical
 13 uza                         // b == 0 -> return 0 unchanged (-0u is 0u)
    aex #07777'7777'7777'7777   // A := ~b   (XOR with all ones)
    arx #01                     // A := ~b + 1  (two's complement; arx sets multiplicative omega)
    aox                         // OR mem[0]=0: A unchanged, omega back to logical
 13 uj
```

The two `aox` instructions are pure ω management, not arithmetic: ORing `mem[0] = 0` leaves A
bit-for-bit unchanged. The leading one makes the zero test work whatever ω the caller left
(the entry ω is unknown by contract); the trailing one undoes the multiplicative ω that `arx`
sets, so the exit is **`NTR 3` / ω = logical** (`R = 7`). The early-return path exits in the
logical ω of the leading `aox`.

### `b$umul` — [b_umul.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_umul.s) — `a * b` (unsigned)

Returns the low 48 bits of the unsigned product `a * b` in A. Adapted from the two-path
`u_mul_u` routine of a sibling 64-bit/52-bit machine. The `A*X` instruction natively forms
the full 81-bit product mantissa across the A:Y pair (`A` = product bits 80:41, `Y` = bits
40:1), so the low 48 bits are `(A << 40)[48:41] | (Y & mask40)`. The helper branches on
operand magnitude — `(a|b) >> 40` — because an operand `≥ 2⁴⁰` has bits in 48:41 that the FP
unit misreads as sign/exponent and must therefore be split:

- **Short way** (both operands `< 2⁴⁰`, the common case): a *single* 40×40→low-48 multiply
  followed by the repack above. No subroutine call, no splitting.
- **Long way** (some operand `≥ 2⁴⁰`): split each operand at the 40-bit mantissa boundary,
  `a = aH·2⁴⁰ + aL`, `b = bH·2⁴⁰ + bL` (with `aL,bL < 2⁴⁰`, `aH,bH < 2⁸`), and assemble

  ```
  result = (aL·bL) + 2⁴⁰ · ((aL·bH + aH·bL) mod 2⁸)        mod 2⁴⁸
  ```

  The `aH·bH·2⁸⁰` term and all but the low 8 bits of each cross product vanish mod 2⁴⁸.

The routine uses no named temporaries and no subroutine call: the four split parts and the
partial products all live on the stack, addressed by negative offset from `r15`, and the
per-product low-bit extraction (run the hardware FP multiply on the clean, sign-bit-free
operands, then repack the low 48 bits from the A:Y pair) is inlined at each of the three
multiply sites. The two cross-product addends are each `< 2⁸`, so their sum never carries
past bit 48 and a plain `ARX` add suffices.

### `b$udiv` — [b_udiv.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_udiv.s) — `a / b` (unsigned)

Receives two 48-bit unsigned values (`a` at `mem[r15−1]`, `b` in A). Returns the
unsigned quotient in A, truncated toward zero. Division by zero returns 0
(implementation-defined).

The signed `b$div` is an *exact* floor divide whenever both operands are `< 2⁴⁰` (they are
then non-negative and fit the 40-bit FP mantissa), so `b$udiv` routes on operand magnitude
to use the hardware divide where it is safe and falls back to software only for the wide
range:

- **`b == 0`** → return 0 (undefined).
- **`(a | b) < 2⁴⁰`** (the common case) → tail-jump to `b$div` for a single hardware divide.
- **`b < 2³²` (but `a ≥ 2⁴⁰`)** → base-2⁸ long division in two `b$div`/`b$mod` steps:
  `q = (aHi/b)·2⁸ + ((aHi%b)·2⁸ | aLo)/b`, with `aHi = a>>8`, `aLo = a & 0377`.
- **`b ≥ 2³²`** → a divisor-shift/subtract restoring loop (≤ 16 iterations) over the full
  48-bit residue, using `b$uge`/`b$usub` so the residue stays in the unsigned range.

### `b$umod` — [b_umod.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_umod.s) — `a % b` (unsigned)

Returns the unsigned remainder over the full 48-bit range from the identity
`r = a − (a÷b)·b`, composing the proven full-width helpers `b$udiv`, `b$umul`, and `b$usub`
(quotient, then product, then difference). It keeps the entry return address and the
intermediates in a small stack frame across the nested calls, addressing them through `r12`
(the callees clobber r14 and r13).

---

## Shift Helpers

`ASN` takes its shift count from the **7-bit exponent field of the effective address**, as
`n = field − 64`: a positive `n` shifts A right by `n`, a negative `n` shifts it left by `−n`
(see [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — *Shift semantics*). Both helpers
exploit that single instruction; the only difference is the sign of the count they build.

Standard helper convention: the value to shift, `a`, is on the stack top; the shift count `b`
is in A; the result is left in A. In both routines `sti 14` does double duty — it stores the
count into r14 *and* pops `a` off the stack into A — after which the indexed `asn` shifts `a`
by the count now held in r14.

### `b$rsh` — [b_rsh.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_rsh.s) — `a >> b`

```
    sti 14      // r14 = b, pop a from the stack into A
 14 asn 64      // effective field = 64 + b  ->  shift right by b
 13 uj
```

### `b$lsh` — [b_lsh.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_lsh.s) — `a << b`

A left shift is a *negative* count, so the helper complements `b` first:

```
    aex #0177   // A := ~b  (in the 7-bit field, ~b = -b-1)
    sti 14      // r14 = -b-1, pop a from the stack into A
 14 asn 64 + 1  // effective field = 64 + 1 + (-b-1) = 64 - b  ->  shift left by b
 13 uj
```

*ω/NTR:* `ASN` is a logical operation and neither helper touches `NTR`, so both exit on
**`NTR 3` / ω = logical** (`R = 7`) with no restoring op needed.

---

## Signed Relational and Logical Operators

Each helper receives `a` at `mem[r15−1]` and `b` in A. It returns 1 if the condition holds,
0 otherwise.

All seven routines follow the same two-branch template:

1. Perform a comparison operation (subtraction or XOR) that sets the ω mode.
2. Branch to the `true` path on the appropriate ω condition.
3. Fall-through: `xta` loads 0 from address 0 (BESM-6 guarantees `mem[0] = 0`).
4. `true` path: a local label whose `xta #01` loads 1 from the file's own constant pool;
   return.

BESM-6 has no load-immediate instruction for arbitrary integer values, which is why both
results are *loaded* — 0 from `mem[0]`, 1 from a constant-pool word — rather than built
inline.

### Branch condition details

Two instruction classes are used, each setting a different ω mode automatically:

**`AEX` (bitwise XOR, opcode 012) → Logical ω mode (bits 5–3 = `001`):**
After `AEX`, `UZA` branches when A = 0 (all bits zero); `U1A` when A ≠ 0.

**Subtraction (`A-X`, `X-A`) → Additive ω mode (bits 5–3 = `100`):**
After subtraction, `UZA` branches when A ≥ 0 (bit 41 = 0); `U1A` when A < 0 (bit 41 = 1).

`aex` with no explicit address reads from `mem[0] = 0`, so A = b XOR 0 = b; the result
tests whether b itself is zero. `15 aex` reads `mem[r15−1]` = a (via pre-decrement on
r15=017), computing A = b XOR a.

`15 x-a` computes A = mem[r15−1] − A = a − b (pre-decrement on r15, X operand minus A).
`15 a-x` computes A = A − mem[r15−1] = b − a.

### Operator table

| Routine | Source | C op | Key instruction | ω mode | Branch taken when |
|---------|--------|------|-----------------|--------|-------------------|
| `b$not` | [b_not.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_not.s) | `!b` | `aex` → A = b XOR 0 = b | Logical | `uza`: A = 0 (b = 0) |
| `b$eq` | [b_eq.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_eq.s) | `a == b` | `15 aex` → A = b XOR a | Logical | `uza`: A = 0 (a = b) |
| `b$ne` | [b_ne.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ne.s) | `a != b` | `15 aex` → A = b XOR a | Logical | `u1a`: A ≠ 0 (a ≠ b) |
| `b$lt` | [b_lt.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_lt.s) | `a < b` | `15 x-a` → A = a − b | Additive | `u1a`: A < 0 (a < b) |
| `b$le` | [b_le.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_le.s) | `a <= b` | `15 a-x` → A = b − a | Additive | `uza`: A ≥ 0 (b ≥ a) |
| `b$gt` | [b_gt.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_gt.s) | `a > b` | `15 a-x` → A = b − a | Additive | `u1a`: A < 0 (b < a) |
| `b$ge` | [b_ge.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ge.s) | `a >= b` | `15 x-a` → A = a − b | Additive | `uza`: A ≥ 0 (a ≥ b) |

**Why pairs share an instruction:** `b$lt`/`b$ge` both compute `a − b` and test opposite
ω conditions (`U1A` vs `UZA`). `b$le`/`b$gt` both compute `b − a` and test opposite
conditions. `b$eq`/`b$ne` both XOR the operands and test A = 0 vs A ≠ 0.

**Limitation for unsigned operands:** the Additive sign test (bit 41 = sign) is valid only
for 41-bit signed integers. If either operand spans the full 48-bit unsigned range (bit 48
set), the subtraction sign test gives the wrong answer. `b$eq` and `b$ne` are
signedness-independent (XOR tests bitwise equality) and are reused for unsigned equality.
The four unsigned ordering helpers below handle the remaining cases.

---

## Unsigned Relational Operators

The subtraction sign-bit test used by `b$lt`, `b$le`, `b$gt`, `b$ge` is only valid within
the 41-bit signed range. For unsigned values in the full 48-bit range a different test is
needed. The four helpers below detect the borrow of the unsigned subtraction with a bit
trick instead of an arithmetic subtract: they form `a ⊕ b` with `AEX`, then use `APX`
(pack, opcode 015) to select bits of `a ⊕ b` under the mask of one operand, and `ASN` to
shift the resulting borrow bit (bit 48) into the low position as the 0/1 result. Because the
test never reads bit 48 as a sign, it is correct across the entire 48-bit unsigned range.

Each receives the two 48-bit unsigned operands in the standard helper convention
(`a` at `mem[r15−1]`, `b` in A) and returns 0 or 1 in A. There is no branch and no `true`
label: the answer *is* the extracted bit, and the two "≤ / ≥" helpers get theirs by
inverting the strict test with `aex #01`.

### `b$ult` — [b_ult.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ult.s) — `a < b` (unsigned)

Packs `a ⊕ b` under `b` and extracts bit 48 — the borrow of `a − b`, i.e. `a < b`.

### `b$ule` — [b_ule.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ule.s) — `a <= b` (unsigned)

Packs `a ⊕ b` under `a` and extracts bit 48 (= `a > b`), then inverts it with `aex #01`.

### `b$ugt` — [b_ugt.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_ugt.s) — `a > b` (unsigned)

Packs `a ⊕ b` under `a` and extracts bit 48 — the borrow of `b − a`, i.e. `a > b`.

### `b$uge` — [b_uge.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_uge.s) — `a >= b` (unsigned)

Packs `a ⊕ b` under `b` and extracts bit 48 (= `a < b`), then inverts it with `aex #01`.

---

## Floating-Point Relational Operators

The four FP orderings mirror their signed-integer counterparts (`a` at `mem[r15−1]`, `b` in
A; result 0/1 in A) but the operands are native 48-bit floating-point words. The signed
helpers subtract the operands as raw integers, which is wrong for FP — a native FP word is
not monotonic when read as a two's-complement integer. The FP helpers instead bracket the
subtract with a bare `ntr` (R := 0, full FP mode) so the result is normalized and rounded:
the Additive sign then reflects the mathematical difference, and equal operands produce an
exact zero (so the `≥`/`≤` equality edge tests correctly). Before returning, each path
restores `R := 7` with `ntr 7` — the integer mode `b$save` leaves in place — so the caller's
following integer ops behave; the `ntr 7` must come *after* the conditional branch, since
`NTR` overwrites the ω flag that `U1A`/`UZA` test. Operands are already FP (valid exponents),
so — unlike `b$div`/`b$mul` — no INT-format bridge is needed.

| Routine | Source | Operation | Subtraction | Group | True condition |
|---------|--------|-----------|-------------|-------|----------------|
| `b$flt` | [b_flt.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_flt.s) | `a < b` | `15 x-a` → A = a − b | Additive | `u1a`: A < 0 (a < b) |
| `b$fle` | [b_fle.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_fle.s) | `a <= b` | `15 a-x` → A = b − a | Additive | `uza`: A ≥ 0 (b ≥ a) |
| `b$fgt` | [b_fgt.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_fgt.s) | `a > b` | `15 a-x` → A = b − a | Additive | `u1a`: A < 0 (b < a) |
| `b$fge` | [b_fge.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_fge.s) | `a >= b` | `15 x-a` → A = a − b | Additive | `uza`: A ≥ 0 (a ≥ b) |

Floating-point `==` and `!=` are pure bit equality (`AEX` + `UZA`/`U1A`), which is
type-independent, so they reuse `b$eq` and `b$ne` rather than dedicated FP helpers.

---

## Type Conversion Helpers

These routines convert between the native BESM-6 floating-point format (`float` ≡
`double`, a single 48-bit word with 7-bit exponent and 40-bit mantissa) and C integer
types. The asymmetry between signed `int` (41 bits) and `unsigned` (48 bits) drives the
split: the **signed** `int`→`double` direction is a short inline sequence in the backend
(`aox #0'64` to set the INT-format exponent, then `ntr` / `a+x` / `ntr 7` to normalise)
and needs no helper, but every other direction needs one of the helpers below.

The realign primitive used throughout: adding INT-format 0.0 (`a+x #0'64`, the exponent-104
word) with normalization+rounding suppressed (`ntr 3`) shifts an FP value's mantissa to the
INT exponent, dropping the fraction. For a **non-negative** value this truncates toward
zero; for a negative two's-complement mantissa it rounds toward −∞ (floors), so the signed
helper brackets it with an absolute value.

### `b$dtoi` — [b_dtoi.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_dtoi.s) — `double` → signed `int`

Receives the 48-bit native FP value in A (no operand on the stack); returns the
truncated 41-bit signed integer in A.

Algorithm: push `x` as a sign reference, take `|x|` with `AVX` (full FP mode), realign the
magnitude to the INT exponent (`ntr 3` / `a+x #0'64`, which truncates a non-negative value
toward zero), reapply the original sign with a second `AVX`, then mask off the exponent
field to leave a raw 41-bit signed integer. The absolute-value bracketing is what yields C
truncation toward zero (a direct realign would floor negatives). Values with |x| ≥ 2⁴⁰ do
not fit the 41-bit signed range and overflow (implementation-defined, C11 §6.3.1.4).

`float` and `double` use the same 48-bit format on BESM-6, so `b$dtoi` serves both
`(int)f` and `(int)d`.

*ω/NTR:* full FP mode (a bare `ntr`) for the `AVX` magnitude, then `ntr 3` for the realign;
the closing exponent-mask `aax` is logical, so the exit is **`NTR 3` / ω = logical**
(`R = 7`) — the implicit-restore idiom (same as `b$div`).

### `b$dtou` — [b_dtou.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_dtou.s) — `double` → unsigned `int`

Receives the FP value in A; returns the full 48-bit unsigned result in A. The signed
realign+mask only recovers 41 bits, so for the whole unsigned range `b$dtou` mirrors
`b$utod`'s split in reverse: `hi = trunc(x·2⁻²⁴)` and `lo = trunc(x − (double)hi·2²⁴)` are
each extracted with the realign primitive (both intermediates non-negative, so no sign
handling), then recombined as `(hi << 24) | lo`. `(unsigned)` of a negative or ≥ 2⁴⁸ value
is undefined, as in C.

*ω/NTR:* alternates full FP mode (a bare `ntr`, for the multiply / normalize) and `ntr 3`
(realign) per half; the final recombine `aox` is logical, so the exit is
**`NTR 3` / ω = logical** (`R = 7`).

### `b$utod` — [b_utod.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_utod.s) — unsigned `int` → `double`

Receives the 48-bit unsigned value in A; returns the native FP value in A. The inline
signed trick cannot be used because a 48-bit unsigned carries data in the exponent field
(bits 48–42) and would have bit 41 misread as the FP sign. Instead the word is split into
two 24-bit halves `u = hi·2²⁴ + lo` (each < 2⁴⁰, exact in the mantissa); each half is
converted with the INT-format-then-normalise trick and the result recombined in floating
point as `(double)hi · 2²⁴ + (double)lo` (the scale factor `2²⁴` is the constant-pool word
`#0'545`). Inputs with more than 40 significant bits round to the 40-bit mantissa
(unavoidable). `float` shares the format, so `b$utod` serves both `(double)u` and
`(float)u`.

*ω/NTR:* runs the conversions in full FP mode (a bare `ntr`) and restores **explicitly** with
a trailing `ntr 7` before the final `utm`/return, so the exit is **`NTR 3` / ω = logical**
(`R = 7`).

---

## Fat-Pointer Helpers

A `char*`/`void*` is a *fat pointer*: bit 48 is a marker, bits 47–45 hold a 3-bit byte
offset code `offset_enc` (the exponent field reads as `64 + offset_enc*8`), and bits 15–1
hold the word address. Within a word, bytes are packed MSB-first: `offset_enc 5` = byte #0
(the MSB / first byte), `offset_enc 0` = byte #5 (the LSB / last byte). So the byte index
from the MSB is `5 - offset_enc`, and advancing the pointer one byte *decrements*
`offset_enc`, borrowing into the word address on the `0 → 5` wrap.

> **All five keep their temporaries on the stack, and must.** They are the helpers a
> *preemptible* system runs into: they are interruptible at every instruction, and what
> interrupts them — a kernel's clock tick reaching a `char` struct member, a signal handler in a
> user program — walks `char` pointers of its own. The Madlen originals kept their working values
> in static cells (`,base,`-relative), and the port carried that over; a tick landing inside one
> then ran a handler whose helper overwrote them, and the interrupted call resumed and finished
> with the *handler's* values. It surfaced in the Unix v7 port as a deadlock in the first `exec`:
> `iget()` read a `char i_flag` through a pointer that a clock tick had turned into a pointer to
> `proc[]`, decided the root inode was locked, and slept forever. No fault, no diagnostic.
>
> Each now reserves a frame by **advancing r15** (an interrupt builds its own frame *at* r15, so
> scratch merely written above it would be overwritten) and addresses slots by **negative offset**
> — never offset 0, which with index register 15 is the machine's stack mode. Where a helper takes
> a stack operand, it is read where the caller left it and the closing `utm` releases the frame
> and pops it in one. `b$umul` and `b$umod` were built this way from the start.
>
> The read-only `.data` tables (`ttab`, `mask`, `shift`) stay static: sharing them is safe.

### `b$stb` — [b_stb.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_stb.s) — store one byte

Read-modify-write of a single byte through a fat pointer. Lightweight convention: the fat
pointer `a` is on the stack top (popped by the helper) and the byte value `b` is in A. The
helper masks the byte, clears the target byte of the containing word via an offset-indexed
mask table, ORs the new byte into place via an offset-indexed shift table, writes the word
back, and returns the stored byte in A.

### `b$pinc` / `b$pdec` — [b_pinc.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_pinc.s) / [b_pdec.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_pdec.s) — `char*`++ / `char*`--

Increment/decrement a fat pointer by one byte. Operand in A, result in A (no stack). The
common case adjusts `offset_enc` by ∓1 with a single end-around add; the wrap case rebuilds
the marker with the boundary `offset_enc` (5 for `++`, 0 for `--`) and steps the word
address.

*ω/NTR:* no `NTR` of its own, so the `NTR 3` suppress bits pass through untouched; every path
ends with a logical `aox`, so the exit is **`NTR 3` / ω = logical** (`R = 7`). (The interior
`arx` end-around adds leave multiplicative ω transiently, but a logical op always follows.)

### `b$padd` — [b_padd.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_padd.s) — fat pointer + signed byte count

Adds a signed byte delta to a fat-or-bare base, returning a normalized fat pointer.
Convention: base `a` on the stack top (popped), signed delta in A, result in A. A bare base
(marker clear — an array word address or struct address) is taken to start at byte #0. With
`m = (5 - offset_enc) + delta`, the new word is `word + floor(m/6)` and the new
`offset_enc = 5 - (m mod 6)`. The floored division by 6 uses the hardware FP divide on two
normalized INT-format operands (masking the fraction rounds toward −∞); the dividend is
first biased by a multiple of 6 large enough to be non-negative (INT-format misreads a
negative two's-complement mantissa) and the bias is subtracted back from the quotient.

*ω/NTR:* the initial `byte# + delta` and bias adds run as raw integer adds, relying on the
**`NTR 3`** suppress bits at entry; the divide drops to full FP mode (a bare `ntr`), then the
helper restores **explicitly** with `ntr 7` for the `m − 6q` and `word'` integer arithmetic.
The closing `aox` (OR the marker word) is logical, so the exit is **`NTR 3` / ω = logical**
(`R = 7`).

### `b$pdiff` — [b_pdiff.s](https://github.com/besm6/c-compiler/blob/main/libc/besm6/unix/b_pdiff.s) — fat pointer − fat pointer

Computes the `ptrdiff_t` byte distance between two `char*`/`void*` fat pointers: `p - q`.
Convention: minuend `a` (= `p`) on the stack top (popped), subtrahend `b` (= `q`) in A,
signed result in A. Each pointer is decoded to an absolute byte position
`abs = word*6 + byte#` (`byte# = 5 - offset_enc`, or 0 if the marker is clear — a bare
array/struct address starts at byte #0) and the result is `abs(a) - abs(b)`. `sizeof(char)`
is 1, so no scaling is applied. All raw integer mode: no normalization and no division
(`6*word = 2*word + 4*word` via shifts), relying on the **`NTR 3`** suppress bits at entry.

*ω/NTR:* the final `a-x` (the `abs(a) - abs(b)` subtract) leaves *additive* ω, so the helper
appends a no-op `aox` to land on **`NTR 3` / ω = logical** (`R = 7`) — without it a caller
that branched on the difference would get the additive sign test instead of a zero test.
