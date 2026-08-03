# Build cmd/cpp a second time, natively for BESM-6

## Context

Every product of this repo falls into one of three categories:

- **host tools** — `cmd/*`, compiled by the host C/C++ compiler, run on the build machine;
- **cross-built BESM-6 artifacts** — `kernel/` and `lib/`, compiled by the `b6*` toolchain
  through `b6_obj()` in [../../scripts/BesmCross.cmake](../../scripts/BesmCross.cmake), from
  sources that exist only to run on the BESM-6;
- **native BESM-6 programs** — [`cmd/init`](../init/README.md), linked against libc by
  `b6_prog()` and staged into `build/rootfs/` for the disk image.

What is still missing is the *fourth* thing this document is about: **a BESM-6 program built from
the same sources as a host tool**. `cmd/cpp` is the natural first one — plain C, needs only
libc, and a preprocessor that runs on the target is the first step toward a self-hosting
toolchain. This plan adds exactly that one program, `build/rootfs/usr/bin/cpp`, from the existing
`cmd/cpp/*.c`. Since `cmd/init` landed, the machinery for it is a single `b6_prog()` call (§5)
and the plan is all blockers.

**The build machinery is the easy 20%.** The real content of this plan is a set of blockers
found by actually running `b6cc -c` over all eight sources — every one below is reproduced with
a minimal case, not guessed. Two are **bugs in the external c-compiler**
(<https://github.com/besm6/c-compiler/>), which is a separate repo; they are general-C bugs, not
cpp-specific, so a self-hosting toolchain has to fix them anyway. One is a libc gap. Two are
BESM-6 address-space limits this repo owns.

### The blockers, each with its minimal repro

| # | Where | What | Minimal repro (all deterministic) |
|---|---|---|---|
| **B1** | external compiler (`b6parse`) | The **GCC case-range extension** `case A ... B:` is unparsed. [scan.c:377](scan.c#L377) `case 0x80 ... 0xF9:`. | `switch(c){case 1 ... 5: …}` → `Parse error: expected ':', got '...'`. |
| **B2** | external compiler (`b6lower`) | An **array of an anonymous struct type at block scope** loses its type. [buffer.c:111](buffer.c#L111) declares `static const struct { char t, r; } map[]` inside `translate_trigraphs`. | `char f(void){struct{char t,r;}m[2]; m[0].r='b'; return m[0].r;}` → `Fatal error: Struct or union '__anon_1' not found`. Neither `static` nor an initializer is needed. The same declaration at **file** scope compiles, as does a block-scope *scalar* of an anonymous struct, or an array of a *named* one — it is the combination. |
| **G1** | libc | `open_memstream()` (POSIX 2008, neither C11 nor v7) is not in libc. [macro.c:545](macro.c#L545). | `Symbol 'open_memstream' not found`. |
| **L1** | this repo (size) | `struct cppstate` is **~38,630 words** of bss; a C pointer reaches 15 bits (32,767 words) and user text+data+bss gets 28 pages (28,672). | `b6as: short address out of range: 047217` from `utc cpp / xta 20111` — a member 20,111 words into `struct cppstate`. Five of the eight sources stop here. Note that 047217 *does* fit the 15 bits a long address has, so check whether `b6as`'s addressing is part of this before assuming shrinking bss alone answers it. |
| **L2** | this repo (size) | `expand_macro`'s frame is `acttxt[BUFSIZ]+exptxt[4*BUFSIZ]` ≈ **6,827 words**; the user stack is 4,096 (pages 28–31). | [macro.c:677-678](macro.c#L677). |

Two things claimed in an earlier draft turned out already handled and are **not** in this plan:
`<unistd.h>` and `<fcntl.h>` already exist in [../../include/](../../include/), and
`strtol`/`strtoul` are already in libc
([../../lib/libc/CMakeLists.txt](../../lib/libc/CMakeLists.txt) `LIBC_OBJ`, with
`lib/test/strtolt`). Once headers resolve, `direct.c`'s `strtol` compiles fine. BSD's
`setbuffer()`, which [cpp.c:203](cpp.c#L203) calls, is in libc now as well —
[../../lib/libc/stdio/setbuffer.c](../../lib/libc/stdio/setbuffer.c) — so that line links as
written, and G1 above is the one libc gap left.

Intended outcome: `make` produces `build/rootfs/usr/bin/cpp` beside `etc/init`; a ctest runs it
under `b6sim` and asserts its output matches the host `b6cpp` byte-for-byte.

## Approach

The order matters: **the external-compiler bugs are gates.** No amount of build wiring helps
until B1 and B2 are resolved, since `scan.c` and `buffer.c` do not reach the assembler at all.
Each has a source-level workaround in `cmd/cpp` that unblocks this pilot, and both workarounds
are small, but the durable fix is upstream and the plan says so at every step.

### 1. The external-compiler bugs (B1, B2)

For each: **file it upstream with the minimal repro above**, and land a source workaround here so
the pilot is not blocked on a foreign repo. Every workaround carries a comment naming the
upstream issue so it can be reverted.

- **B1 — case range.** Rewrite [scan.c:377](scan.c#L377) `case 0x80 ... 0xF9:` as an
  `if (c >= 0x80 && c <= 0xF9)` guard ahead of the `switch`, or a `default:` with the range test
  inside. Preserve the existing fall-through behaviour exactly.
- **B2 — block-scope anonymous struct array.** Workaround: give the type a tag and hoist it out
  of the function — a file-scope `struct trigraph { char t, r; };` above
  [buffer.c:109](buffer.c#L109), leaving the `map[]` initializer as it stands. Confirmed to
  compile; check the other seven sources for the same shape, since only `buffer.c` is known to
  have it.

### 2. `open_memstream` (G1)

[macro.c:545](macro.c#L545) captures an isolated macro prescan through
`open_memstream(&mbuf, &mlen)`, and libc has no such routine. It is not enough that the call
site already degrades gracefully when the stream is null (`mf == NULL` falls back to copying the
argument raw): the **symbol still has to resolve** to link at all, and the fallback path does not
prescan, so a cpp built on it would not match the host `b6cpp` byte for byte — which is exactly
what §6's test asserts. Two routes, and this plan does not settle which:

- **Implement it in libc.** A growing heap sink is a new kind of stream here: `_IOSTRG` is a
  *fixed* caller buffer and `_flsbuf` deliberately drops the byte when one fills
  ([../../lib/libc/README.md](../../lib/libc/README.md)), so a real `open_memstream` wants either
  a `realloc` on overflow behind its own flag bit, or a distinct sink. It also pulls the heap into
  a program already fighting L1/L2 for address space.
- **Restructure the call site** to prescan into a fixed arena buffer it already owns, so no
  stream is needed. Cheaper on the target and no libc change, but it is a change to shared
  source that the host build compiles too, so it must be output-neutral there.

### 3. An arch predefine, so a shared source can tune itself (for L1/L2)

[cpp.c:236](cpp.c#L236) has the v7 predefine block (`#if unix` → `unix`, `#if pdp11` → `pdp11`,
…), all keyed on what the *host* compiler defines. Add one **unconditional** line after it:

```c
cpp.sym_arch = define_symbol("besm6");   // b6cpp always targets the BESM-6
```

Last, so it wins over any host `pdp11`/`vax`. This is a *target* macro: source compiled by
`b6cc` sees `besm6`; the same source under clang for the host tool does not. The size profile
below keys off it with no flags on any command line. Add a case to
`test/test_predefined_macros.cpp` asserting `besm6` is defined and `#undef`-able (the
`#undef unix` case at line 149 is the model).

### 4. A BESM-6 size profile in `defs.h` (L1, L2)

Wrap the four constants at [defs.h:50-56](defs.h#L50):

```c
#ifdef besm6
// 32K-word address space, 4K-word stack: see ../init/README.md.  This profile does
// NOT meet the C11 §5.2.4.1 minima (4095 macros, 4095-char logical lines).
#define BUFSIZ  2048
#define SBSIZE  8192
#define SYMSIZ  1021    // prime
#else
// ... today's 8192 / 65536 / 6151 ...
#endif
```

Sizing (chars pack six to a word): `symbols[1021]`×3w = 3,063, `paint_stack[1021]` = 1,021,
`side_buf[8192]` = 1,366, `arena` = 686, remainder ≈ 400 → **≈ 6,500 words** of bss, from
~38,630. Also make `exptxt`'s `4 * BUFSIZ` a named `EXPTXT_MULT` knob. With `BUFSIZ 2048`,
`expand_macro`'s frame is ≈ 2,100 words of the 4,096 available; if too tight in practice, step
`BUFSIZ` to 1024 rather than dropping the multiplier (the diagnostic frames in `direct.c` scale
with `BUFSIZ` too).

**The honesty requirement:** the comment must state plainly that the BESM-6 profile is
non-conforming, and [README.md](README.md) must record that `test/`'s C11 conformance results
apply to the **host** build only.

### 5. The native link — **already built, one call**

*This step is done, by [`cmd/init`](../init/README.md), which became the first native program
instead of `cpp`.* `b6_prog()` in [../../scripts/BesmCross.cmake](../../scripts/BesmCross.cmake)
is the generic helper this section said a second native program would justify, and the staging
root `build/rootfs/` exists. All that remains here is a `cmd/cpp/CMakeLists.txt` addition:

```cmake
b6_prog(cpp DEST usr/bin/cpp
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES cpp.c buffer.c scan.c direct.c macro.c diag.c parser.c yylex.c)
```

with `KINC`/`KHDRS` set beside it as `cmd/init/CMakeLists.txt` does, and the directory added
inside the top-level `if(B6RUNTIME_LIB)` guard after `add_subdirectory(lib)`.

Two differences from what this section originally planned, both settled by `cmd/init`:

- **It builds by default**, not behind an opt-in `make rootfs`; `b6_prog()` marks its target
  `ALL`. No top-level `Makefile` change, and `make run` runs the `rootfs`-labelled tests with
  everything else. (The host `cmd/cpp` build and this one are separate targets in the same
  directory — the host tool is `b6cpp`, this is `build/rootfs/usr/bin/cpp`.)
- **The size ceilings are enforced, not merely reported.** `b6_prog()` registers
  `rootfs_cpp_size`, running `scripts/check-size.sh` against 28,672 words of
  `const+text+data+bss` and a 32,767-word symbol ceiling — which is exactly blockers **L1** and
  **L2** above, turned into a test. Expect it to fail until §4's size profile lands.

### 6. The test: native cpp must agree with host cpp

One ctest, label `rootfs`, driven by a `rootfs/run-test.sh` shaped like
[../../lib/test/run-test.sh](../../lib/test/run-test.sh):

1. host `b6cpp` over `rootfs/fixture.c` → `expected.i`
2. native cpp under `b6sim` over the same fixture → `actual.i`
3. `diff`

Two cpps from one source disagreeing is a port bug. The fixture must exercise object- and
function-like macros, `#if`/`#elif`, `#`/`##`, and a local `#include`; must avoid
`__DATE__`/`__TIME__` (differ per run, [cpp.c:291](cpp.c#L291)); and must stay within the reduced
`SYMSIZ`/`BUFSIZ`.

### 7. Documentation

- [README.md](README.md) — a "Building for the BESM-6" section: the non-conforming size profile
  and why, and the three ceilings (32,767-word pointer reach, 28-page text+data+bss, 4,096-word
  stack). The build category itself is documented in [../init/README.md](../init/README.md).
- [../../CLAUDE.md](../../CLAUDE.md) — its "Native BESM-6 programs" section already describes
  the category and `b6_prog()`; add `cpp` as the second one, and the size profile as the first
  time the ceilings actually bound a program.
- [README.md](README.md) — the `besm6` predefine, the host-only scope of the conformance suite,
  and a short "Building for the BESM-6" note listing the workarounds and their upstream issue
  numbers.
- [../../kernel/TODO.md](../../kernel/TODO.md) — task 24 named `build/rootfs/`; note that
  `usr/bin/cpp` now sits in it beside `etc/init`.

## Order of work (gates first)

1. **B1, B2** — file them upstream; land the two source workarounds (§1). `scan.c` and
   `buffer.c` do not reach the assembler until this is done.
2. **G1** — settle `open_memstream` one way or the other (§2). It is the only blocker whose
   answer changes what the finished program *does*, so it wants deciding early.
3. Confirm all eight sources compile with `-I ../../include`.
4. **Predefine + size profile** (§3, §4); check `b6size -w` puts text+data+bss under 28,672
   words and no symbol exceeds word 32,767.
5. The `b6_prog()` call (§5) — one block in `cmd/cpp/CMakeLists.txt`.
6. Fixture, `run-test.sh`, ctest (§6).
7. Docs (§7).

## Verification

```sh
make && make install          # host tools, kernel, lib, AND build/rootfs/usr/bin/cpp
make run                      # the whole suite, the rootfs label included
```

- `ctest --test-dir build -L rootfs` — two things now: `rootfs_cpp_size` (the ceilings, from
  `b6_prog()`) and the output-agreement test of §6. Close to the ceiling in the size test's
  report means the §4 profile needs another notch down.
- `b6nm -n build/rootfs/usr/bin/cpp | tail` — by hand, for where the space went.
- `build/cmd/cpp/test/cpp_test` — the host C11 suite still passes in full; the `besm6` predefine
  and the split declarators must be invisible to the host build. (Its cases are registered
  individually by `gtest_discover_tests`, so there is no single ctest label; run the binary or
  rely on `make run`.)
- `ctest --test-dir build -L lib` — the `cmd/cpp` source edits touch nothing here, but confirm.

A stronger check once it runs: feed the native cpp one of this repo's own kernel sources under
`b6sim` and diff against host `b6cpp` on the same file — the real workload, and the one most
likely to exhaust the reduced `SYMSIZ`.

## Out of scope

No other `cmd/` tool built natively, no userland beyond the `init` that is already there (`sh`,
`bin/*`), no change to `root.manifest` — the image is staged, not yet put on a disk.
Those are [../../kernel/TODO.md](../../kernel/TODO.md) task 24; this plan leaves the staging
root ready for them. The three upstream compiler fixes (B1–B3) live in a **separate repo**; this
plan files them and works around them locally, but landing them there is its own task.
