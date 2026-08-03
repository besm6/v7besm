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
`cmd/cpp/*.c`. Since `cmd/init` landed, the machinery for it is a single `b6_prog()` call (§4)
and the plan is all blockers.

**The build machinery is the easy 20%.** The real content of this plan is a set of blockers
found by actually running `b6cc -c` over all eight sources — every one below is reproduced with
a minimal case, not guessed. **All eight now stop in `b6as`, none in the compiler front end**:
the block-scope anonymous-struct bug this plan used to gate on is fixed upstream, and so is the
missing `open_memstream()` (§1). **Nothing is left upstream.** What remains is three limits of
this machine, all three of them this repo's to design around.

### The blockers, each with its minimal repro

| # | Where | What | Minimal repro (all deterministic) |
|---|---|---|---|
| **L1** | this repo (size) | `struct cppstate` is **~38,630 words** of bss; a C pointer reaches 15 bits (32,767 words) and user const+text+data+bss gets 28 pages (28,672). | `parser.c` and `yylex.c` name word **38,650** (`utc cpp / atx 38650`), past the 15-bit reach. `b6as` reports that one as `short address out of range: 013372` — the value **masked** to 15 bits, so it does not read like an overflow; do not chase 013372. The other six sources meet **L3** first, around word 20,000 (`xta 20111` in `cpp.c`). |
| **L2** | this repo (size) | `expand_macro`'s frame is `acttxt[BUFSIZ]+exptxt[4*BUFSIZ]` ≈ **6,827 words**; the user stack is 4,096 (pages 28–31). | [macro.c:677-678](macro.c#L677). |
| **L3** | this machine (addressing) | **A struct cannot exceed 4,096 words.** A member is reached as a base register plus a *short* address field, and that field spans only `[0..07777]` and `[070000..077777]` — see [../as/pass2.c](../as/pass2.c) for why nothing downstream can rescue an offset outside it. This is the architecture, not a compiler defect: there is no long-address form of the base-plus-offset access, so a member past word 4,095 has nowhere to be named. `struct cppstate` is ~38,630 words, and this is what stops six of the eight sources. | `struct s { int pad[4096]; int x; } g; int f(void){ return g.x; }` → `b6as: short address out of range: 010000` from `utc g / xta 4096`. `pad[4095]` compiles. A **file-scope array** of the same size has no such limit — the address goes into an index register — which is why §3's answer is to take the big arrays out of the struct rather than to shrink it. |

Two things claimed in an earlier draft turned out already handled and are **not** in this plan:
`<unistd.h>` and `<fcntl.h>` already exist in [../../include/](../../include/), and
`strtol`/`strtoul` are already in libc
([../../lib/libc/CMakeLists.txt](../../lib/libc/CMakeLists.txt) `LIBC_OBJ`, with
`lib/test/strtolt`). Once headers resolve, `direct.c`'s `strtol` compiles fine. BSD's
`setbuffer()`, which [cpp.c:203](cpp.c#L203) calls, is in libc now as well —
[../../lib/libc/stdio/setbuffer.c](../../lib/libc/stdio/setbuffer.c) — so that line links as
written, and with §1 done there is no libc gap left.

Intended outcome: `make` produces `build/rootfs/usr/bin/cpp` beside `etc/init`; a ctest runs it
under `b6sim` and asserts its output matches the host `b6cpp` byte-for-byte.

## Approach

The order matters: **`struct cppstate` is the gate**, and it is a gate twice over. Every source
now fails on that one object, first because a member of it cannot be named past word 4,095 (L3)
and then because it does not fit the address space at all (L1). §3 answers both, in that order —
**take the four big arrays out of the struct**, which is a plain refactor owing nothing to this
machine, and only then size what is left. Nothing here waits on another repository.

### 1. `open_memstream` — done

[macro.c:545](macro.c#L545) captures an isolated macro prescan through
`open_memstream(&mbuf, &mlen)`, and libc now has it:
[../../lib/libc/stdio/memstream.c](../../lib/libc/stdio/memstream.c), declared in
[../../include/stdio.h](../../include/stdio.h), documented in
[../../lib/libc/man/fopen.3s](../../lib/libc/man/fopen.3s) and covered by `lib/test/stdiot`.

Restructuring the call site was the alternative and was not taken. The reason the call site's own
`mf == NULL` fallback was never enough is worth keeping: the **symbol still has to resolve** to
link at all, and the fallback does not prescan, so a cpp built on it would not match the host
`b6cpp` byte for byte — which is exactly what §5's test asserts.

It is an ordinary `_iob` stream carrying `_IOSTRG | _IOMEM`, held at `_cnt == 0` so that
`_flsbuf()` sees every byte and grows a heap buffer to take it; a program that never opens one
pays a word of bss and nothing else. What it costs *this* program is the heap, in an address
space already fighting L1/L2 — the initial buffer is 48 bytes and doubles, so a prescan of a few
dozen characters never reaches `BUFSIZ`.

### 2. An arch predefine, so a shared source can tune itself (for L1/L2)

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

### 3. The four big arrays out of the struct (L3), then a size profile (L1, L2)

**First, and independently of any sizing: move the four large arrays out of `struct cppstate`**
to file scope in [cpp.c](cpp.c), leaving `extern` declarations beside the struct in
[defs.h](defs.h). They are all of the struct's bulk and none of its cursors:

| member | words today |
|---|---|
| `symbols[SYMSIZ]` — `struct symtab` ×3w | 18,453 |
| `side_buf[SBSIZE]` | 10,923 |
| `paint_stack[SYMSIZ]` | 6,151 |
| `arena[8+2*BUFSIZ+8]` | 2,734 |

What is left is the cursors, the four `ALFSIZ` scan tables, the include and pushback stacks, the
option flags and the `sym_*` handles — **≈ 400 words**, an order of magnitude inside L3's 4,096,
and it stays there under any size profile because nothing left in it scales with `SYMSIZ`,
`SBSIZE` or `BUFSIZ`. A file-scope array of any size is reached through an index register, so
`symbols[i].value` and `side_buf[65535]` both compile at full size — confirmed with the four
declarations above compiled alone. The struct keeps doing what its comment says it is for
(namespacing the *state*), and this costs the host build nothing, so it is **not** guarded on
the `besm6` predefine of §2 — one form of the source for both targets.

L3 is then gone for good rather than held under a ceiling, and the profile below is free to be
chosen on merit.

**Then the size profile**, for L1 and L2 alone. Wrap the four constants at
[defs.h:50-56](defs.h#L50):

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
`side_buf[8192]` = 1,366, `arena` = 686, the struct itself ≈ 400 → **≈ 6,500 words** of bss,
from ~38,630. That is well inside L1's 28,672 with room for text, and the four objects are now
separate symbols, so what the 32,767-word ceiling has to hold is the *last* of them rather than
a single 38,630-word block. Also make `exptxt`'s `4 * BUFSIZ` a named `EXPTXT_MULT` knob. With
`BUFSIZ 2048`, `expand_macro`'s frame is ≈ 2,100 words of the 4,096 available; if too tight in
practice, step `BUFSIZ` to 1024 rather than dropping the multiplier (the diagnostic frames in
`direct.c` scale with `BUFSIZ` too).

The profile above is the conservative first cut. Once it builds and passes §5, there is headroom
to step `SYMSIZ`/`SBSIZE` back up toward the C11 minima against the measured `b6size -w` — L3 no
longer objects to any of it, and L2 bounds only `BUFSIZ`.

**The honesty requirement:** the comment must state plainly that the BESM-6 profile is
non-conforming, and [README.md](README.md) must record that `test/`'s C11 conformance results
apply to the **host** build only.

### 4. The native link — **already built, one call**

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
  **L2** above, turned into a test. Expect it to fail until §3's size profile lands. It does
  **not** catch L3, which `b6as` stops long before there is a program to measure — that one has
  no test but the build itself.

### 5. The test: native cpp must agree with host cpp

One ctest, label `rootfs`, driven by a `rootfs/run-test.sh` shaped like
[../../lib/test/run-test.sh](../../lib/test/run-test.sh):

1. host `b6cpp` over `rootfs/fixture.c` → `expected.i`
2. native cpp under `b6sim` over the same fixture → `actual.i`
3. `diff`

Two cpps from one source disagreeing is a port bug. The fixture must exercise object- and
function-like macros, `#if`/`#elif`, `#`/`##`, and a local `#include`; must avoid
`__DATE__`/`__TIME__` (differ per run, [cpp.c:291](cpp.c#L291)); and must stay within the reduced
`SYMSIZ`/`BUFSIZ`.

### 6. Documentation

- [README.md](README.md) — a "Building for the BESM-6" section: the `besm6` predefine, the
  non-conforming size profile and why, the host-only scope of the C11 conformance suite, and the
  **four** ceilings (32,767-word pointer reach, 28-page const+text+data+bss, 4,096-word stack,
  and L3's 4,096-word struct). The build category itself is documented in
  [../init/README.md](../init/README.md).
- [../../CLAUDE.md](../../CLAUDE.md) — its "Native BESM-6 programs" section already describes
  the category and `b6_prog()`; add `cpp` as the second one, and the size profile as the first
  time the ceilings actually bound a program.
- [../README.md](../README.md) — **L3 belongs in the hazard list**, beside the `int`/`char *`
  traps. It is a property of the machine that every v7 source with one big state struct will
  meet, it is invisible until `b6as` refuses an offset, and the fix (arrays to file scope) is
  the same every time.
- [../../kernel/TODO.md](../../kernel/TODO.md) — task 24 named `build/rootfs/`; note that
  `usr/bin/cpp` now sits in it beside `etc/init`.

## Order of work (gates first)

1. **The four arrays out of `struct cppstate`** (§3, first half) — the gate, and worth landing
   on its own: it is target-independent, so the host `cmd/cpp` and its C11 suite prove it before
   anything is cross-built.
2. **Predefine + size profile** (§2, §3, second half); then check `b6size -w` puts
   const+text+data+bss under 28,672 words and no symbol exceeds word 32,767.
3. Confirm all eight sources compile with `-I ../../include` — they reach `b6as` today and stop
   there, so this is the step that says whether §3 was enough.
4. The `b6_prog()` call (§4) — one block in `cmd/cpp/CMakeLists.txt`.
5. Fixture, `run-test.sh`, ctest (§5).
6. Docs (§6).

## Verification

```sh
make && make install          # host tools, kernel, lib, AND build/rootfs/usr/bin/cpp
make run                      # the whole suite, the rootfs label included
```

- `ctest --test-dir build -L rootfs` — two things now: `rootfs_cpp_size` (the ceilings, from
  `b6_prog()`) and the output-agreement test of §5. Close to the ceiling in the size test's
  report means the §3 profile needs another notch down.
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
root ready for them.
