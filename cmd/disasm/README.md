# b6disasm — disassembler for BESM-6 `a.out`

Walks an object file or executable in on-disk order — const, then text, then data — printing
the const and data words as octal half-word pairs and decoding the text into re-assemblable
instructions: `disasm [-bcCrR] file ...`.

| flag | |
| --- | --- |
| `-b` | BEMSH (Cyrillic) mnemonics instead of the default MADLEN |
| `-c` | print the raw instruction word in octal beside each decoding |
| `-C` | print the octal only, skipping the decoder (implies `-c`) |
| `-r` | print the relocation record beside each half-word |
| `-R` | print relocation as numbers rather than symbolically (implies `-r`) |

Not a v7 program: this repo wrote it. `-r` needs the relocation records to still be there, so
it is refused on a fully linked image, where `RELFLG` marks their absence.

Every text word holds **two 24-bit instructions**, the high (left) half executing first, and the
disassembler prints them one to a line under the one address. The encoding is
[doc/Besm6_Instruction_Set.md](../../doc/Besm6_Instruction_Set.md); the output syntax is
[`b6as`](../as)'s own, so what comes out can be fed back in.

Build with `make`; the engine is covered by the GoogleTest suite in [`test/`](test), which links
it in-process.

## Source layout

| File | Responsibility |
| --- | --- |
| `dis.c` | The engine: the four mnemonic tables, `disasm_insn()` — the decoder proper — and the segment walkers `prwords`/`prtext`/`prcmd`/`prrel`/`disfile`/`disassemble`. |
| `main.c` | The command-line wrapper: flags, the dialect choice, one call per named file. |
| `disasm.h` | The engine's interface and the four tables, so the tests can reach them. |

**The mnemonic tables are transcribed from [`../as/tables.c`](../as/tables.c)** and must stay
level with it: an opcode the assembler names and this does not disassembles to its raw `@NN` or
`$NN` form, which `b6as` does accept back. The same two tables are also transcribed a third
time into [`scripts/vscode-besm6/`](../../scripts/vscode-besm6), whose grammar highlights them.

## Building for the BESM-6

These same sources — plus the `cmd/libaout` files `disasm` calls — are built a **second** time,
by the `b6*` cross toolchain, into `build/rootfs/usr/bin/disasm`: task **C9c** in
[../TODO.md](../TODO.md), with [`../nm`](../nm), [`../size`](../size) and
[`../strip`](../strip). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the build machinery,
and there is **no second copy of any source**.

**No size profile.** Like [`../size`](../size), and unlike the five toolchain programs before
it, this one is character for character what the host tool is built from: there is no `#ifdef
besm6` anywhere in it. It holds one 64-byte line buffer, the two mnemonic-table pairs — 80
`const char *` and the strings they point at — and streams everything else through `stdio`. The
one place it costs more than it looks is `-r`, which **opens the input twice**, so three streams
at `BUFSIZ` is 1,536 words of heap; nothing else in the program allocates at all.

**The Cyrillic names are UTF-8 string literals** — `"зп"`, `"сч"`, `"пе"` — and they come through
`b6cpp`/`b6parse`/`b6lower` unharmed, six bytes to the word like any other string. That is the
one thing about this program that could plausibly have differed between the two builds, so the
`-b` case in the agreement suite exists specifically to assert it.

The program is **6,439 words** (99 const, 4,678 text, 614 data, 1,048 bss) with its top
relocatable symbol at 6,447 — against ceilings of 28,672 and 32,767. The 614 words of data are
mostly those tables' pointers.

### The stack

No recursion, and the only array is `prcmd`'s 64-byte line buffer (11 words). The deepest chain
is `main` (31 words) → `disassemble` (46) → `disfile` (31) → `prtext` (73) → `prrel` (156) →
`printf` (3) → `_doprnt` (**281**), which is **621 words** of the 4,096; without `-r` it is
`prcmd` (18) in `prrel`'s place, and 483.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_disasm_obj`, `_reloc`, `_relnum`, `_image`, `_octal`, `_bemsh`** — the host
  `b6disasm` and the native `disasm` over one fixture, the two listings diffed **live**. The
  fixture's text reaches every branch of `disasm_insn()`: both instruction formats, an indexed
  instruction and a bare one, and all three printings of an address — omitted when zero, bare
  below 8, `%#o` above it. It is decoded first as a relocatable object, which is the only form
  `-r` accepts, and then again after linking, which is the only form without relocation records.
  Two disassemblers built from one source must decode a file the same way; a checked-in
  `.expected` could not express that, since the day a mnemonic is added a live diff *requires*
  the addition to land identically on both targets.
- **`cmd_disasm_badflag`, `_noargs`, `_notfound`** — ordinary `b6sim` cases with a checked-in
  `.expected`, which is the other half: they pin the usage summary, the diagnostics and the exit
  status, which two disassemblers wrong in the same way would not.

`rootfs_disasm_size` (registered by `b6_prog()`) is the third: it holds the program under the two
address-space ceilings.

The check by hand is the one the program exists for — the machine reading back the instructions
it was built out of:

```sh
cd build
b6sim rootfs/usr/bin/disasm -c rootfs/usr/bin/ld | head
b6sim rootfs/usr/bin/disasm -b rootfs/usr/bin/disasm | head   # itself, in Cyrillic
```
