# BESM-6 Unix Assembler Manual

This manual describes the assembly language accepted by `b6as`, the BESM-6 assembler in
`cmd/as/`. It is a reference for the *syntax* of the language — how source text is tokenized,
how statements are built, what directives and operand forms exist, and how expressions are
evaluated. It does **not** re-document the instruction semantics or the ABI; for those, see:

- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — opcodes, registers, instruction
  formats, and the AU mode register.
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — the procedure-call ABI
  (argument passing, `r13`/`r14`, stack frames).
- [Besm6_Data_Representation.md](Besm6_Data_Representation.md) — how C scalars are laid out
  in a 48-bit word.

Everything below is derived from the assembler sources themselves: `lex.c` (lexer), `expr.c`
(expression parser), `pass1.c` (statement parser and code generation), `tables.c` (instruction
and character tables), `as.h` (shared definitions), `symtab.c` (symbols and directive
lookup), and `main.c` (command line).

---

## 1. Overview

The BESM-6 is a **48-bit, word-addressed** machine. The addressable unit is one 48-bit word;
there are no sub-word load/store instructions. Each machine instruction is 24 bits wide, and
**two instructions are packed into one word** — the left (high) instruction in bits 48–25
executes first, the right (low) instruction in bits 24–1 second. Bits are numbered
right-to-left from 1 (bit 1 = LSB, bit 48 = MSB). See
[Instruction Set §1](Besm6_Instruction_Set.md#1-overview).

`b6as` uses **AT&T-style syntax with Madlen mnemonics**: a statement is one instruction,
written as `[modreg] mnemonic operand[, index]`. The assembler is a classic two-pass design;
it reads one source file and produces a BESM-6 `a.out` object file (the format defined in
[`cross/besm6/b.out.h`](../cross/besm6/b.out.h)), with separate const, text, data, and bss
segments.

> **Octal convention.** Octal is the native notation for BESM-6 (opcodes, masks, addresses
> are all customarily written in octal). Be aware, however, that **the lexer does not default
> to octal** — a bare integer literal is *decimal*. A leading `0` selects octal. See
> [§6](#6-numbers-and-literals).

---

## 2. Invoking the assembler

```
b6as [-uaxXd] [-o outfile] [infile]
```

| Option | Effect |
|--------|--------|
| `-o filename` | Set the output object file. Default: `a.out`. |
| `-u` | Treat undefined names as an error. |
| `-a` | Do **not** align instructions on word boundaries (suppresses the padding normally inserted after `vjm`/`ij`/`stop` and for `.word`/`.ascii`). |
| `-x` | Discard local symbols from the output symbol table. |
| `-X` | Discard local symbols whose names start with `.` (compiler-generated labels). Implies `-x`. |
| `-d` | Debug mode (verbose). |

If no `infile` is given and standard input is a terminal, the usage message is printed.
Otherwise source is read from standard input.

The assembler runs two passes: pass 1 generates code into temporary per-segment files and
builds the symbol table; pass 2 relocates and emits the final object, followed by the
relocation and symbol tables.

---

## 3. Source-line structure

A statement occupies one line. In its most general form:

```
[label:]  [modreg]  operation  [operands]      // comment
```

- **`label:`** — an optional symbol definition (see [§5](#5-symbols-and-labels)).
- **`modreg`** — an optional integer that designates the **modifier (index) register**
  to be used by the instruction on this line. For example `7 xta data` assembles `xta data`
  with index register M7. The value is masked to 4 bits (registers 0–15).
- **`operation`** — a machine instruction mnemonic, a raw opcode (`$XX`/`@XX`), an assembler
  directive (`.text`, `.word`, …), a symbol definition (`name = expr`), or a
  location-counter assignment (`. = expr`).
- **operands** — instruction- or directive-specific, separated by commas.

**Statement terminator.** Statements end at the newline. There is no statement-separator
character — one statement per line. Spaces and tabs separate tokens and are otherwise
ignored.

A line consisting only of `:` forces alignment of the current segment to a word boundary
(useful before a label that must land on the left instruction of a word).

---

## 4. Comments

Two comment forms are recognized:

| Syntax | Where | Meaning |
|--------|-------|---------|
| `//` … | anywhere on a line | Comment to end of line. |
| `#` … | **only at the start of a line** | Comment to end of line. |

The `#` comment is recognized only immediately after a newline. A `#` appearing **mid-line**
is the constant-pool operator (see [§8](#8-registers-and-addressing)), not a comment. There
is no block-comment syntax.

A line-start `#` of the special form `# <lineno> "<filename>"` is a **cc-style line marker**,
as emitted by the C preprocessor when it flattens `#include`s into a single stream. The
assembler reads the line number and file name from the marker and reports subsequent
diagnostics against that original source location (`filename:lineno:`) instead of the physical
line of the preprocessed input. Any other line-start `#` (including a marker that is
malformed) is treated as an ordinary whole-line comment and emits nothing.

---

## 5. Symbols and labels

**Name characters.** A name is a run of letters, digits, `_`, `.`, `$`, and high-bit bytes
(codes 0200–0377, which carry UTF-8 multibyte characters, including Cyrillic letters). A
name may not begin with a digit (a leading digit starts a number) nor with `$` (a leading
`$` starts a raw `$NN` opcode, see [§9.3](#93-raw-opcodes)). Names are case-sensitive.

**Mnemonics are not reserved.** A name may be spelled like a machine instruction (`sti`,
`mod`, `ext`, …): a word is read as a mnemonic only where an instruction is expected — the
start of a statement — and even there a following `:` or `=` marks it as a label or equate
instead. In operand position a mnemonic spelling is always an ordinary symbol, so
`uj sti` jumps to a label named `sti`. (The one exception is the `.equ` directive form —
`sti .equ 5` — which stays ambiguous with a `.`-operand and is not supported for a
mnemonic-spelled name; use the `=` form instead.)

**Defining a label.** A name followed by a colon defines a symbol whose value is the current
location (in words) within the current segment:

```
loop:   xta count
        u1a loop
```

Defining a label first aligns the current segment to a word boundary.

**Equating a symbol.** A symbol may be set to the value of an expression with either of:

```
buf_size = 0200          // infix '=' form
buf_size .equ 0200        // .equ directive form
```

The symbol takes the segment/relocation class of the expression. Equating to an external
(undefined) symbol is an error ("indirect equivalence").

**The location counter.** Inside expressions, `.` evaluates to the current location (see
[§7](#7-expressions)). As a statement, `. = expr` advances the location counter of the
current segment to `expr` words, filling the gap (with `utc 0` fillers in text, zeros
elsewhere). The new value must not be less than the current one.

**Global symbols.** `.globl` marks names as external/global (see [§10](#10-directives)).
Local symbols can be stripped at output time with `-x`/`-X` ([§2](#2-invoking-the-assembler)).

---

## 6. Numbers and literals

### 6.1 Integer bases

A numeric literal's base is chosen by a C-style prefix. **With no prefix, the literal is
decimal** — a leading `0` is what makes it octal. There are no base-selecting suffixes.

| Form | Base | Example | Value (decimal) |
|------|------|---------|-----------------|
| `digits` | decimal | `1234` | 1234 |
| `0digits` | octal (leading zero) | `01234` | 668 |
| `0x` / `0X` digits | hexadecimal | `0x1ff` | 511 |
| `0b` / `0B` digits | binary | `0b101` | 5 |

Literals are full 48-bit values, stored internally as two halves (`left`, `right`). There is
no dedicated negative-literal syntax; negate with a unary minus in an expression
([§7](#7-expressions)).

A single apostrophe `'` may be used as a digit-group separator between two digits, C++-style;
it does not affect the value. This works in every base: `1'000'000`, `0xdead'beef`,
`0100'000'000`, `0b1000'0000`. The apostrophe must sit **between** digits — a trailing or
doubled `'` (e.g. `1'`, `1''0`) is an error.

An apostrophe placed **immediately after the base prefix** instead marks the literal as
**left-aligned** in the 48-bit word: the digits pack against the most-significant end and
the low bits are zero-filled, producing a full-width word. Available for octal, hexadecimal
and binary — not decimal (which has no prefix).

| Form | Meaning | Value |
|------|---------|-------|
| `0'123` | octal `123`, left-aligned | `0123 \< 39` = `01230000000000000` |
| `0x'abc` | hex `abc`, left-aligned | `0xabc \< 36` = `0xabc000000000` |
| `0b'111` | binary `111`, left-aligned | `0b111 \< 45` |

Formally, for `N` mantissa digits of `B` bits each (octal `B`=3, hex `B`=4, binary `B`=1),
the value is `digits \< (48 - N*B)`; the octal base marker `0` is not part of the mantissa.
Internal separators still work (`0x'ab'cd`). At least one digit must follow the marker, and
the mantissa may not exceed 48 bits.

### 6.2 Bit-mask literals

A `.`-prefixed bracket or number builds a bit mask directly. Bit numbers run 1–64 (bit 1 =
LSB), matching the BESM-6 numbering convention.

| Form | Meaning |
|------|---------|
| `.[a:b]` | Mask with bits `a` through `b` set to 1 (order of `a`, `b` does not matter). |
| `.[a=b]` | Complement of `.[a:b]` (bits `a..b` cleared, all others set). |
| `.N` | Single bit `N` set. |

Examples: `.[1:8]` is the low eight bits; `.5` is `020` octal; `.[48=41]` clears the
exponent field and sets everything else.

### 6.3 Characters and raw opcodes

There is no general character-constant (`'c'`) syntax. Two lexical forms read **two octal
digits** and produce a raw instruction opcode rather than a number — see
[§9.3](#93-raw-opcodes).

---

## 7. Expressions

Wherever a value is needed (an address, a directive operand, the right side of `=`), an
**expression** may appear. The grammar is:

```
expression  =  [operand] { operator operand } ...
operand     =  number | name | "." | "(" expression ")" | "{" expression "}"
operator    =  "+" | "-" | "&" | "|" | "^" | "~" | "\<" | "\>" | "*" | "/" | "%"
```

**Operands.**

| Operand | Meaning |
|---------|---------|
| number | A literal ([§6](#6-numbers-and-literals)). |
| name | A symbol; contributes its value and segment/relocation class. Undefined names become external references. |
| `.` | The current location (word offset in the current segment). |
| `( … )` | Grouping. |
| `{ … }` | Same as `( … )`, but **truncates the exponent field** of the result (clears bits 48–25 of the value's high half). Useful for stripping a floating-point exponent. |

**Operators.**

| Operator | Operation |
|----------|-----------|
| `+` | Addition. |
| `-` | Subtraction. |
| `&` | Bitwise AND. |
| `\|` | Bitwise OR. |
| `^` | Bitwise XOR. |
| `~` | XOR with the complement (`a ~ b` = `a ^ ~b`). |
| `\<` | Shift left (count = right operand mod 64). Written as backslash-less-than. |
| `\>` | Shift right. Written as backslash-greater-than. |
| `*` | Multiply (low 31 bits). |
| `/` | Divide (low 31 bits; division by zero is an error). |
| `%` | Modulo (low 31 bits; modulo by zero is an error). |

> **No operator precedence.** Expressions are evaluated **strictly left to right**. `1 + 2 * 3`
> is `(1 + 2) * 3 = 9`, not 7. Use parentheses to force any other grouping.

**Relocation constraints.** Only one relocatable (segment-relative or external) term may flow
through an expression, and only `+` may combine it:

- `+` accepts a relocatable left or right term (e.g. `label + 4`).
- `-` requires its right operand to be absolute (`label - 4` is fine; `label - other` is not).
- All bitwise, shift, and multiplicative operators require **both** operands absolute.

Violations produce "too complex expression".

---

## 8. Registers and addressing

### 8.1 The register model

The relevant registers (full details in
[Instruction Set §2](Besm6_Instruction_Set.md#2-cpu-registers)):

- **A** — the 48-bit accumulator, the implicit operand of arithmetic/logical instructions.
- **M[1]–M[15]** — 15-bit index (modifier) registers; M[0] always reads 0; M[15] (017) is the
  conventional stack pointer. These are the `r1`–`r15` of the
  [calling conventions](Besm6_Calling_Conventions.md).
- **C** — the 15-bit address-modification register, added to the effective address; cleared
  after every instruction except `utc`/`wtc`.

### 8.2 Operand forms

For a machine instruction, the operand after the mnemonic may take any of these forms
(`pass1.c:makecmd`):

| Form | Example | Meaning |
|------|---------|---------|
| *(none)* | `xta` | Address field 0. |
| expression | `xta data` | Direct address; the address field is the expression's value, relocated by its segment. |
| `# expr` | `a+x #1.0` | **Constant pool**: place `expr`'s value in the const segment (deduplicated) and address it. Use for literals and large constants. |
| `expr , reg` | `xta tab, 3` | Index the access with register M`reg` (`reg` must be absolute, 0–15). |
| `< expr >` | `xta <bigaddr>` | **Address extension**: emit `utc expr` first, loading C, then the instruction. Lets a 15-bit address reach beyond the 12-bit short field. |
| `[ expr ]` | `atx [bigaddr]` | Emit `wtc expr` first (write-to-C variant), then the instruction. |

The leading **modifier register** (the `modreg` of [§3](#3-source-line-structure)) is distinct
from the trailing `, reg` index; both ultimately fill the instruction's 4-bit modifier field.

---

## 9. Instructions

### 9.1 The two formats

BESM-6 instructions come in two formats (see
[Instruction Set §3](Besm6_Instruction_Set.md#3-instruction-formats)):

- **Short address** — opcodes `000`–`077` (octal), a 12-bit address field. Most arithmetic,
  logical, and stack operations.
- **Long address** — opcodes `020`–`037`, a 15-bit address field. Jumps, register-load, and
  address-setup instructions. In the assembler these mnemonics carry the `TLONG` flag.

Two 24-bit instructions pack into one 48-bit word; the assembler fills the left half first,
then the right. Three long jumps — `vjm`, `ij`, `stop` — additionally carry an *align-after*
flag: a filler (`utc 0`) is inserted after them so the next instruction starts a fresh word
(suppressed by `-a`).

### 9.2 The mnemonic table

Mnemonics map to opcodes in `cmd/as/tables.c`. Madlen mnemonics use the operator characters
`+ - * /` inside the name (e.g. `a+x`, `x-a`, `a*x`, `e-n`, `j+m`); the lexer recognizes these
as single tokens when an instruction is expected. A representative sample (see the table for
the full set):

**Short-address (000–077), `val = opcode << 12`:**

| Mnemonic | Opcode | Mnemonic | Opcode |
|----------|--------|----------|--------|
| `atx` | 000 | `xta` | 010 |
| `stx` | 001 | `aax` | 011 |
| `a+x` | 004 | `aox` | 015 |
| `a-x` | 005 | `a/x` | 016 |
| `x-a` | 006 | `a*x` | 017 |
| `ext` | 033 | `ntr` | 037 |
| `ati` | 040 | `ita` | 042 |
| `mtj` | 044 | `j+m` | 045 |

Opcodes with no dedicated mnemonic (the extracodes and reserved slots — e.g. `032`,
`046`…`077`, and long `020`, `021`, `036`) have no named form; write them with the raw
`$NN`/`@NN` syntax described in [§9.3](#93-raw-opcodes).

**Long-address (020–037), `val = opcode << 15`:**

| Mnemonic | Opcode | Meaning (see Instruction Set §6) |
|----------|--------|----------------------------------|
| `utc` | 022 | Load C (no clear). |
| `wtc` | 023 | Load C from memory. |
| `vtm` | 024 | Set index register. |
| `utm` | 025 | Add to index register. |
| `uza` | 026 | Branch if accumulator zero (ω). |
| `u1a` | 027 | Branch if accumulator nonzero (ω). |
| `uj`  | 030 | Unconditional jump. |
| `vjm` | 031 | Jump to subroutine *(align after)*. |
| `ij`  | 032 | Interrupt return *(align after)*. |
| `stop`| 033 | Stop *(align after)*. |
| `vlm` | 037 | Loop on index register. |

For the precise semantics, ω-mode behavior, and stack effects of each instruction, consult
[Instruction Set §6](Besm6_Instruction_Set.md#6-instruction-reference).

### 9.3 Raw opcodes

When a mnemonic is not available, an opcode is written directly as **two octal digits**
after a sigil (e.g. `$32` is short opcode 032, `@20` is long opcode 020):

| Form | Meaning |
|------|---------|
| `$NN` | Short-address instruction with octal opcode `NN` (`val = NN << 12`). |
| `@NN` | Long-address instruction with octal opcode `NN` (`val = NN << 15`). |

These take the same operand forms as named instructions.

---

## 10. Directives

All directives begin with `.`. The complete set (`cmd/as/symtab.c:lookacmd`):

### Segment selection

| Directive | Operands | Effect |
|-----------|----------|--------|
| `.text` | — | Switch to the **text** (code) segment. This is the initial segment. |
| `.data` | — | Switch to the **data** (initialized data) segment. |
| `.strng` | — | Switch to the **string** segment (string constants; folded into data at link time). |
| `.bss` | — | Switch to the **bss** (uninitialized data) segment. |

The const segment is not selected by a directive; it is populated implicitly via the `#`
constant-pool operator ([§8](#8-registers-and-addressing)).

### Emitting data

| Directive | Operands | Effect |
|-----------|----------|--------|
| `.word` | `expr [, expr …]` | Emit each expression as a full **48-bit word**, big-endian. Aligns to a word boundary first. |
| `.half` | `expr [, expr …]` | Emit each expression as a **24-bit half-word** (no alignment), packing two per word — the first in the high half, the second in the low half. |
| `.ascii` | `"string"` | Emit the string, packed six characters per word and zero-padded to a word boundary. Aligns first. See escapes below. |

The half-word order is the same big-endian layout used everywhere else (instructions,
the header, and the constant pool): the high 24-bit half-word is stored first on disk, so
the six bytes of a word read as one big-endian 48-bit number. An address-carrying `.word`
keeps its relocation on the low (second) half-word.

**`.ascii` escape sequences** (inside the double-quoted string):

| Escape | Meaning |
|--------|---------|
| `\n` `\t` `\r` `\b` `\f` | Newline, tab, carriage return, backspace, form feed. |
| `\ooo` | One to three octal digits → that character code. |
| `\`<newline> | Line continuation (the newline is dropped). |

### Symbols

| Directive | Operands | Effect |
|-----------|----------|--------|
| `.globl` | `name [, name …]` | Mark each name as external/global. |
| `.equ` | `name .equ expr` | Equate `name` to `expr` (also available as `name = expr`). |
| `.comm` | `name [, len]` | Declare a **common** block of `len` words (default 1); becomes bss at link time. |

---

## 11. Worked examples

### 11.1 Straight-line instructions

Bare mnemonics, two packing into each word:

```
        xta a
        a+x b
        atx c
        stop
```

### 11.2 A counted loop

```
        vtm -10, 2          // M2 = -10 (loop counter)
loop:   xta tab, 2          // A = tab[M2]
        a+x sum
        atx sum
        vlm loop, 2         // step M2 toward zero, branch to loop while nonzero
        uj  done
```

### 11.3 Data and strings

```
        .data
sum:    .word 0
tab:    .word 1, 2, 3, 4
flags:  .half 0, .[1:4]     // two half-words: 0 and the low-4-bit mask
msg:    .ascii "hello\n"    // packed 6 chars/word, zero-padded
        .bss
buf:    . = . + 0100        // reserve 64 words
```

### 11.4 Constant pool and address extension

```
        a+x #3.14159        // load the constant from the const segment and add
        xta <far_table>     // utc far_table; xta  — reach a 15-bit address
        atx [far_slot]      // wtc far_slot; atx
```

### 11.5 Symbols

```
        .globl entry
count   = 0144              // octal 144 = 100 decimal
entry:  vtm count, 1
        uj  entry
```

---

## 12. Quick reference

**Directives:** `.text` `.data` `.strng` `.bss` · `.word` `.half` `.ascii` · `.globl`
`.equ` `.comm`.

**Expression operators (left-to-right, no precedence):** `+` `-` `&` `|` `^` `~` `\<` `\>`
`*` `/` `%`; grouping `( )`; exponent-truncate `{ }`; current location `.`.

**Number formats:** decimal (default) `1234`; octal `01234`; hex `0x1ff`; binary `0b101`;
bit masks `.[a:b]` `.[a=b]` `.N`. A `'` between digits is an ignored separator (`1'000'000`);
a `'` right after the base prefix left-aligns the literal in the word (`0'123`, `0x'abc`, `0b'111`).

**Operand forms:** `op addr` · `op #const` · `op addr, reg` · `op <addr>` · `op [addr]` ·
`reg op addr` (leading modifier register).

**Raw opcodes (octal):** `$NN` (short), `@NN` (long).

**Comments:** `//` anywhere; `#` only at line start.

---

## Source pointers

The authoritative implementation lives in `cmd/as/`:
`lex.c` (lexer), `expr.c` (expressions), `pass1.c` (statement parser / codegen),
`tables.c` (instruction and character tables), `symtab.c` (symbols and directive lookup),
`as.h` (shared definitions), `main.c` (command line). The object/relocation format is in
[`cross/besm6/b.out.h`](../cross/besm6/b.out.h).
