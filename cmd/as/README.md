Assembler for BESM-6 under Unix.

The assembly language is documented in [doc/Assembler_Manual.md](../../doc/Assembler_Manual.md).
Build with `make`; the engine is covered by unit tests in `test/`.

## Building for the BESM-6

These same eight sources — plus the `cmd/libaout` files the assembler calls — are built a
**second** time, by the `b6*` cross toolchain, into `build/rootfs/usr/bin/as`: the assembler
that runs on the machine, and the second step of self-hosting (task **C9b** in
[../README.md](../README.md); `cpp` was C9a). [`rootfs/CMakeLists.txt`](rootfs) is the whole of the
build machinery, and there is **no second copy of any source**: what differs is a size profile
in [`as.h`](as.h), keyed on the `besm6` macro `b6cpp` always predefines.

Note the bootstrap. `scripts/BesmCross.cmake` resolves the cross tools as the *in-tree*
targets, so this program is assembled and linked by the host build of its own sources. A
mistake in the shared code shows up as a miscompile of everything on the image.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `HASHSZ` (symbol hash slots) | 2048 | 1024 | 921 symbols; the measured peak is 503 |
| `HCONSZ` (constant-dedup hash slots) | 4096 | 512 | 460 entries; the measured peak is 87 |
| `HCMDSZ` (mnemonic hash slots) | 1024 | 128 | `table[]` has **45** entries; 1024 was always slack |
| `SRCNAME_MAX` (a `# N "file"` path) | 1024 | 256 | charged twice — `as.srcfile`, and a buffer under the whole parse |
| `MAXEXPRDEPTH` (`(` nesting) | 200 | **20** | the stack; see below |

**The sizes are measured, not guessed.** Assembling the 194 `.s` files this repo's own
kernel, libc, `sh`, `sed`, `cpp` and `as` compile to, the high-water marks are **503 symbols,
3,240 bytes of name arena and 87 constants** — all three set by `cmd/sed/sed0.c`, the largest
single source here. The profile leaves ~80% headroom on that. Undersizing the three is not
equally bad, which is why they were not cut equally: the symbol table and the name arena are
**fatal** when they fill ([`symtab.c`](symtab.c)), while the constant-dedup table just stops
merging ([`pass1.c`](pass1.c)) and costs a duplicate word.

### Where the ceilings bind

1. **No struct may exceed 4,096 words.** A member is named by a 12-bit offset from a base
   register and there is no longer form, so `b6as` refuses the offset — *"short address out of
   range"* — and nothing downstream can rescue it. `struct assembler` was ~28,335 words, which
   is not only 6.9× the ceiling but 98.8% of the whole address space before a word of code.
   The fix is not to shrink it but to take the six big arrays *out*: `stab`, `space`,
   `constab`, `hashtab`, `hashctab` and `hashconst` are at file scope in [`as.c`](as.c) now,
   where an index register reaches them at any size. `cpp` did this to `struct cppstate`
   first; [../README.md](../README.md) §6 is the general account.
2. **The 4,096-word stack, which nothing checks.** `parse_line_marker`'s `SRCNAME_MAX` buffer
   is `static`: it never recurses and its frame sat under the entire parse. And
   `parse_expr` → `parse_term` → `parse_operand` → `parse_expr` is a recursion **the input
   drives**, which needs a ceiling of its own — `grep`'s `MAXDEPTH` and `cpp`'s `MAXARGDEPTH`
   are the same move.
3. **The heap**, whose granularity is a page. `as` holds **eight** scratch streams open
   (image + relocations, for each of four segments) plus stdin and stdout, at `BUFSIZ` = 3,072
   bytes = 512 words each. That is ~5,100 words of heap that `rootfs_as_size` cannot see.
4. **28,672 words of image**, which this fills to **19,824** — const 154, text 9,440, data
   1,088, bss 9,142. The ~8,800 words left are not slack but the heap budget above.

### The measured frame chain

Read out of `build/cmd/as/rootfs/as.dis` — the `15 utm 0NNN` in each prologue, which is what
[../grep/README.md](../grep/README.md) means by reading the frame rather than estimating it:

| | words |
| --- | ---: |
| `main` | 98 |
| `assemble` | 125 |
| `generate_code` | 249 |
| `assemble_instruction` | 82 |
| `parse_expr` | 31 |
| `parse_term` | 11 |
| `parse_operand` | 49 |
| `parse_binary` | 13 |
| `next_token` | 372 |
| `parse_line_marker` | 124 |

Resident before an expression is reached: 98 + 125 + 249 + 82 = **554**. Each further `(`
costs `parse_expr` + `parse_term` + `parse_operand` + `parse_binary` = **104**, and
`next_token` (372) plus `parse_line_marker` (124) sit under the deepest one. So depth *D*
costs 1,050 + 104·*D*, and **20 fits at 3,130 of the 4,096** — the ~950 words left pay for
`fatal`'s own frame and the runtime helpers. Compiler output never nests parentheses at all,
and hand-written assembly rarely goes past three.

### What the type changes were about

The value the lexer and the expression evaluator carry is a whole 48-bit BESM-6 word, and it
used to be an `int64_t`. **There is no `int64_t` on this machine** — an `int` is 41-bit signed
and an `unsigned` is exactly 48 — so those 22 sites are `uword_t` now
([`cross/besm6/types.h`](../../cross/besm6/types.h)), which is `uint64_t` for the host and
`unsigned` for the target. Everything narrower stays `word_t`/`long`: a half-word, an address
and a relocation record all fit in 41 bits with room to spare. The conversion changes no
behaviour — every result in `apply_op` was already masked to 48 bits and nothing here ever
compared a word value signed.

Three related things were **not** retypings:

- `read_bit_mask` built its mask as `~0UL >> (63 - k)`, which hard-codes the width of the type
  it shifts. It is written against an explicit width now.
- `SUPERHASH` folded its key through a cast to `short`. A `short` is 16 bits on the build
  machine and 41 here, so the cast folded on one target and not the other; the truncation is
  written out, and the multiply stays signed because this machine multiplies signed in one
  instruction.
- `mkstemp()` does not exist in this libc. The eight scratch files are `tmpfile()` now, which
  is precisely the `fopen("w+")` + `unlink()` the old code spelled by hand.

**And one compiler limit worth knowing, because it is silent:** `b6lower` ignores designated
initializers and initializes **positionally**. `struct assembler_args args = { .outfile =
"a.out" };` put the literal into `infile`, the first member, and the native assembler read its
own output file as its input. It compiles without a word. [`main.c`](main.c) zeroes and
assigns instead.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_as_agree` / `rootfs_as_compiled`** — the host `b6as` and the native `as` over one
  fixture, the two objects compared **live**. Two assemblers built from one source must produce
  the same object byte for byte; a checked-in `a.out` could not express that, because the day
  an opcode encoding changes in [`tables.c`](tables.c) a live comparison *requires* the change
  to land identically on both targets. `agree.s` is hand-written and reaches every base,
  operator and operand form the language has; `compiled.s` is real `b6cc` output, which is what
  the assembler will actually be fed.
- **`cmd_as_*`** — ordinary `b6sim` cases with a checked-in `.expected`, which is the other
  half: they pin the diagnostics and the exit status, which two assemblers wrong in the same
  way would not. `cmd_as_deep` pins the `MAXEXPRDEPTH` message, which the host build cannot
  produce.

`rootfs_as_size` (registered by `b6_prog()`) is the third: it holds the program under 28,672
words and its top symbol under 32,767.

The stronger check, by hand, is the real workload — **all 194 `.s` files above come out
byte-identical**:

```sh
build/cmd/as/b6as -o host.o x.s
b6sim build/rootfs/usr/bin/as -o native.o x.s
cmp host.o native.o
```
