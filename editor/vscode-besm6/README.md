# BESM-6 Assembly — VSCode syntax highlighting

A minimal VSCode extension (a grammar and nothing else — no JavaScript, no build step, no
dependencies) that colors the assembly language accepted by `b6as`.

It claims `.s` and `.S` and follows [`doc/Assembler_Manual.md`](../../doc/Assembler_Manual.md),
which is the spec. The mnemonic tables are transcribed from the assembler and disassembler
themselves: ASCII Madlen names from [`cmd/as/tables.c`](../../cmd/as/tables.c), Cyrillic BEMSH
names from [`cmd/disasm/dis.c`](../../cmd/disasm/dis.c), so `b6disasm -b` output colors too.

## Install

```sh
ln -s "$PWD/editor/vscode-besm6" ~/.vscode/extensions/besm6-asm     # from the repo root
```

Then run **Developer: Reload Window** from the command palette. The status bar should read
*BESM-6 Assembly* when a `.s`/`.S` file is open.

The symlink is the whole install: VSCode has no checked-in, per-workspace way to install an
extension, so this step is per-user and has to be done once on each machine. What *is* checked
in is [`.vscode/settings.json`](../../.vscode/settings.json), which pins `*.s`/`*.S` to this
language for the workspace — so the association holds even if another assembler extension is
installed later and competes for the file extension.

## What it highlights

- **Comments** — `//` anywhere, and `#` only at the start of a line, as the assembler reads
  them. A mid-line `#` is the constant-pool operator, not a comment.
- **Preprocessor** — `#include`, `#define`, `#if…` and friends, since `.S` files are run
  through `cpp` before `b6as` sees them. A `# 12 "file.h"` line marker is a comment.
- **Mnemonics** — split into control flow (`uj`, `vjm`, `ij`, `stop`, `uza`, `u1a`, `vzm`,
  `v1m`, `vlm`) and everything else. Because mnemonics are *not* reserved words, they are
  recognised only where an instruction is expected: `uj sti` leaves the operand plain, and
  `sti:` is a label.
- **Labels, equates and the location counter** — `name:`, `name = expr`, `. = expr`.
- **Directives** — `.text` `.const` `.data` `.strng` `.bss` `.org` `.word` `.half` `.ascii`
  `.globl` `.equ` `.comm`, plus the legacy `.set` that survives in `include/sys.s`.
- **Numbers** — decimal by default, `0…` octal, `0x…` hex, `0b…` binary, the `'` digit
  separator and the `0'…` left-aligned form, and the bit-mask literals `.N`, `.[a:b]`, `.[a=b]`.
- **Operand sigils** — the leading modifier register of `13 uj`, the constant pool `#`, and
  the `<expr>` / `[expr]` address extensions.
- **Raw opcodes** — `$NN` (short) and `@NN` (long).
- **`.ascii` strings** — with the `\n`/`\ooo` escapes; an unknown escape is flagged.

`Cmd-/` toggles `//` comments.

## Editing the grammar

Everything lives in `syntaxes/besm6.tmLanguage.json`. After a change, reload the window, then
use **Developer: Inspect Editor Tokens and Scopes** to see which rule actually won — pattern
order matters and silent mismatches are the usual failure.
