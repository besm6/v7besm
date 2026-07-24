# Build cmd/cpp a second time, natively for BESM-6

## Context

Every product of this repo today falls into one of two categories:

- **host tools** — `cmd/*`, compiled by the host C/C++ compiler, run on the build machine;
- **cross-built BESM-6 artifacts** — `kernel/` and `lib/`, compiled by the `b6*` toolchain
  through `b6_obj()` in [../../scripts/BesmCross.cmake](../../scripts/BesmCross.cmake), from
  sources that exist only to run on the BESM-6.

There is no third: **a BESM-6 program built from the same sources as a host tool**. `cmd/cpp` is
the natural first one — plain C, needs only libc, and a preprocessor that runs on the target is
the first step toward a self-hosting toolchain. This plan adds exactly that one program,
`build/rootfs/usr/bin/cpp`, from the existing `cmd/cpp/*.c`, behind an opt-in `make rootfs`
target. The default build is untouched.

**The build machinery is the easy 20%.** The real content of this plan is a set of blockers
found by actually running `b6cc -c` over all eight sources — every one below is reproduced with
a minimal case, not guessed. Three are **bugs in the external c-compiler**
(<https://github.com/besm6/c-compiler/>), which is a separate repo; they are general-C bugs, not
cpp-specific, so a self-hosting toolchain has to fix them anyway. Two are BESM-6 address-space
limits this repo owns. One is a libc gap.

### The blockers, each with its minimal repro

| # | Where | What | Minimal repro (all deterministic) |
|---|---|---|---|
| **B1** | external compiler (`b6lower`) | A struct's comma-separated declarator list loses its **interior** members once another declaration follows. `struct cppstate` is full of `char *out_ptr, *tok_ptr, *scan_ptr;` — `tok_ptr` vanishes. | `struct s { char *a, *tok_ptr, *c; int p; }; …g.tok_ptr` → `Struct s has no member tok_ptr`. Remove the trailing `int p;` and it compiles. |
| **B2** | external compiler (`b6lower`) | **Enum constants in any aggregate initializer** crash the lowerer (const or not). `yylex.c:22` and `parser.c` both do this. | `enum{X,Y}; static const int a[]={X,Y};` → `literal_to_int64: Cannot convert enum`. Same with plain `int a[]`. Integer literals are fine. |
| **B3** | external compiler (`b6parse`) | The **GCC case-range extension** `case A ... B:` is unparsed. [scan.c:377](scan.c#L377) `case 0x80 ... 0xF9:`. | `switch(c){case 1 ... 5: …}` → `Parse error: expected ':', got '...'`. |
| **G1** | libc | `setbuffer()` (BSD) is not in libc. [cpp.c:203](cpp.c#L203). | `Symbol 'setbuffer' not found`. |
| **L1** | this repo (size) | `struct cppstate` is **~38,630 words** of bss; a C pointer reaches 15 bits (32,767 words) and user text+data+bss gets 28 pages (28,672). | `b6as: short address out of range: 013344` from `utc cpp / atx 38628` (38628 mod 32768 = 013344). |
| **L2** | this repo (size) | `expand_macro`'s frame is `acttxt[BUFSIZ]+exptxt[4*BUFSIZ]` ≈ **6,827 words**; the user stack is 4,096 (pages 28–31). | [macro.c:677-678](macro.c#L677). |

Two things claimed in an earlier draft turned out already handled and are **not** in this plan:
`<unistd.h>` and `<fcntl.h>` already exist in [../../include/](../../include/), and
`strtol`/`strtoul` are already in libc
([../../lib/libc/CMakeLists.txt](../../lib/libc/CMakeLists.txt) `LIBC_OBJ`, with
`lib/test/strtolt`). Once headers resolve, `direct.c`'s `strtol` compiles fine.

Intended outcome: `make rootfs` produces `build/rootfs/usr/bin/cpp`; a ctest runs it under
`b6sim` and asserts its output matches the host `b6cpp` byte-for-byte.

## Approach

The order matters: **the external-compiler bugs are gates.** No amount of build wiring helps
until B1–B3 are resolved, and B1 in particular is unavoidable — `struct cppstate` cannot be
expressed without comma declarators being reliable. Each has a source-level workaround in
`cmd/cpp` that unblocks this pilot, but the durable fix is upstream, and the plan says so at
every step.

### 1. The external-compiler bugs (B1, B2, B3)

For each: **file it upstream with the minimal repro above**, and land a source workaround here so
the pilot is not blocked on a foreign repo. Every workaround carries a comment naming the
upstream issue so it can be reverted.

- **B1 — comma declarators.** Workaround: split each affected member line in
  [defs.h](defs.h#L64) into one declarator per line (`char *out_ptr;` / `char *tok_ptr;` /
  `char *scan_ptr;`). Confirmed to compile. There are ~5 such lines in `struct cppstate`; check
  every `.c` for the same pattern in local structs too (`macro.c`'s `struct macro`, etc.). This
  is verbose but harmless, and mechanical to revert. *This is the one bug that must be fixed
  upstream regardless* — one-declarator-per-line is not a style anyone should be forced into.
- **B2 — enum in initializer.** The two sites ([yylex.c:22](yylex.c#L22) `val2[]`, and the
  token-code tables in `parser.c`) initialize `int[]` from the `OROR`/`ANDAND`/… enum.
  Workaround: give those tokens explicit `#define` values (or an `int` cast per element) so the
  initializer holds integer literals, with a comment mapping each back to its token name.
- **B3 — case range.** Rewrite [scan.c:377](scan.c#L377) `case 0x80 ... 0xF9:` as an
  `if (c >= 0x80 && c <= 0xF9)` guard ahead of the `switch`, or a `default:` with the range test
  inside. Preserve the existing fall-through behaviour exactly.

### 2. `setbuffer` → `setvbuf` (G1)

[cpp.c:203](cpp.c#L203) `setbuffer(cpp.out_file, sobuf, sizeof(sobuf))` →
`setvbuf(cpp.out_file, sobuf, _IOFBF, sizeof(sobuf))`. `setvbuf` **is** in libc (`LIBC_OBJ`), is
ISO C, and is exactly equivalent here. This is a plain source improvement — `setbuffer` is BSD
and has no reason to be preferred — so no `#ifdef` is needed; change it for both builds.

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
// 32K-word address space, 4K-word stack: see rootfs/README.md.  This profile does
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

### 5. `rootfs/CMakeLists.txt` — the native link

New top-level directory, one file, deliberately minimal (no generic helper; a second native
program would justify one). It mirrors [../../lib/test/CMakeLists.txt](../../lib/test/CMakeLists.txt)'s
`b6_libtest` macro:

```cmake
set(SRC ${CMAKE_SOURCE_DIR}/cmd/cpp)
set(STAGE ${CMAKE_BINARY_DIR}/rootfs/usr/bin)
get_filename_component(LIBC_DIR ${CMAKE_BINARY_DIR}/lib/libc ABSOLUTE)
set(KINC ${CMAKE_SOURCE_DIR}/include)                 # so b6_obj finds <stdio.h> etc.
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

foreach(f cpp buffer scan direct macro diag parser yylex)
    b6_obj(o ${SRC}/${f}.c -I${SRC})
    list(APPEND OBJS ${o})
endforeach()

add_custom_command(OUTPUT ${STAGE}/cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGE}
    COMMAND ${B6LD} ${LIBC_DIR}/crt0.o ${OBJS} -o ${STAGE}/cpp
            -L${LIBC_DIR} -L${B6LIBDIR} -lc -lruntime
    COMMAND ${B6SIZE} -w ${STAGE}/cpp
    DEPENDS ${OBJS} ${LIBC_DIR}/crt0.o ${LIBC_DIR}/libc.a)

add_custom_target(rootfs DEPENDS ${STAGE}/cpp)        # NOT `ALL'
add_dependencies(rootfs libc)
```

Link order is the archive-scan contract from [../../lib/README.md](../../lib/README.md):
`crt0.o`, objects, then `-lc -lruntime`. `b6size -w` at the end is the size report, as the kernel
link does it.

Wire it into [../../CMakeLists.txt](../../CMakeLists.txt) **after** `add_subdirectory(lib)`,
inside the existing `if(B6RUNTIME_LIB)` guard (it needs `b6cc`, `libruntime.a`, libc). Because
the target is not `ALL`, `make` and `make test` do not build it.

Top-level [../../Makefile](../../Makefile): add

```make
rootfs: build
	$(MAKE) -Cbuild rootfs
	ctest --test-dir build -L rootfs
```

and change `run:` to `ctest --test-dir build -LE rootfs`, so the default test run does not try to
execute an image that was never built.

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

- `rootfs/README.md` — the third build category; the three ceilings (32,767-word pointer reach,
  28-page text+data+bss, 4,096-word stack); the non-conforming profile and why; `make rootfs`.
- [../../CLAUDE.md](../../CLAUDE.md) — a paragraph on the third category and the `rootfs` target.
  Its "Kernel" section is also **stale** — it says the kernel keeps a hand-written Makefile and is
  not part of the CMake build, but [../../kernel/Makefile](../../kernel/Makefile) is now a thin
  wrapper over `add_subdirectory(kernel)`; fix that while in the file.
- [README.md](README.md) — the `besm6` predefine, the host-only scope of the conformance suite,
  and a short "Building for the BESM-6" note listing the workarounds and their upstream issue
  numbers.
- [../../kernel/TODO.md](../../kernel/TODO.md#L207) — task 24 already names `build/rootfs/`; note
  its staging root now exists.

## Order of work (gates first)

1. **B1** (comma declarators) — file upstream, split declarators in `defs.h` and any local
   structs. Nothing else compiles until this is done.
2. **B2, B3, G1** — file B2/B3 upstream; land the source workarounds; `setbuffer`→`setvbuf`.
3. Confirm all eight sources compile with `-I ../../include`.
4. **Predefine + size profile** (§3, §4); check `b6size -w` puts text+data+bss under 28,672
   words and no symbol exceeds word 32,767.
5. `rootfs/CMakeLists.txt`, the `rootfs` target, Makefile wiring (§5).
6. Fixture, `run-test.sh`, ctest (§6).
7. Docs (§7).

## Verification

```sh
make && make install          # unchanged: host tools, kernel, lib still build
make run                      # unchanged: default suite green, rootfs excluded (-LE rootfs)
make rootfs                   # builds build/rootfs/usr/bin/cpp and runs the rootfs test
```

- `b6size -w build/rootfs/usr/bin/cpp` — text+data+bss **under 28,672 words** (the link prints
  it). Close to the ceiling means the §4 profile needs another notch down.
- `b6nm -n build/rootfs/usr/bin/cpp | tail` — no symbol above word 32,767.
- `ctest --test-dir build -L rootfs` — native output equals host output exactly.
- `build/cmd/cpp/test/cpp_test` — the host C11 suite still passes in full; the `besm6` predefine
  and the split declarators must be invisible to the host build. (Its cases are registered
  individually by `gtest_discover_tests`, so there is no single ctest label; run the binary or
  rely on `make run`.)
- `ctest --test-dir build -L lib` — the `cmd/cpp` source edits touch nothing here, but confirm.

A stronger check once it runs: feed the native cpp one of this repo's own kernel sources under
`b6sim` and diff against host `b6cpp` on the same file — the real workload, and the one most
likely to exhaust the reduced `SYMSIZ`.

## Out of scope

No generic `b6_prog()` helper, no other `cmd/` tool built natively, no userland (`init`, `sh`,
`bin/*`), no change to `kernel/test/root.manifest` — the image is staged, not yet put on a disk.
Those are [../../kernel/TODO.md](../../kernel/TODO.md#L207) task 24; this plan leaves the staging
root ready for them. The three upstream compiler fixes (B1–B3) live in a **separate repo**; this
plan files them and works around them locally, but landing them there is its own task.
