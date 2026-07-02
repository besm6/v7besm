# b6cpp — C11 conformance TODO

The GoogleTest conformance suite in [test/](test/) drives the built `b6cpp`
binary against the C11 preprocessor requirements (ISO/IEC 9899:2011, N1570).
As of this writing **all 77 tests pass; none are marked `DISABLED_`**. This file
scopes one task per remaining feature cluster so they can be picked up
individually.

**Every task starts the same way: re-enable its tests first.** The tests a task
lists are currently disabled — their `TEST_F` names carry a `DISABLED_` prefix in
the referenced `test_*.cpp`. Step one is to drop that prefix so the tests run,
watch them fail, then implement the feature in b6cpp until they pass. The test
names in this file are written *without* the `DISABLED_` prefix they currently
carry in the source (e.g. `Paste.IdentifierPaste` is `TEST_F(Paste,
DISABLED_IdentifierPaste)` in [test/test_paste.cpp](test/test_paste.cpp)).

Run the suite with:

```sh
make && make run                                # whole project (skips DISABLED_)
./build/cmd/cpp/test/cpp_test                   # the conformance suite
./build/cmd/cpp/test/cpp_test --gtest_also_run_disabled_tests --gtest_filter='Paste.*'
```

b6cpp is John F. Reiser's pre-ANSI `cpp`, so most failures are genuinely
unimplemented ANSI/C99/C11 features rather than regressions.

### Cross-cutting note: constraint violations must exit non-zero

Several tests use `EXPECT_PP_DIAGNOSES`, which asserts a **non-zero exit**
(the harness sets `C11PP_STRICT_ARGS=""`, so there is no "make warnings fatal"
flag — the underlying condition must itself be an error). b6cpp currently
reports many constraint violations with `ppwarn()` (non-fatal, exit 0). The
relevant tasks below call out where a `ppwarn` must become a `pperror`
(`pperror` bumps `cpp.exit_code`, see [diag.c](diag.c)).

Source-file map: `cpp.c` (startup, predefined macros, arg parsing) ·
`direct.c` (directive dispatch) · `macro.c` (definition + expansion) ·
`scan.c` (lexing, comments) · `parser.c`/`yylex.c` (`#if` expression) ·
`diag.c` (diagnostics) · `defs.h` (limits/table sizes).

---

## 18. Protect predefined macros and `defined` from `#undef` (follow-on to task 7)

- **Tests to enable:** none yet — add tests as each is tackled.
- **Current:** task 7 made an incompatible `#define` of a macro (including a
  predefined one such as `__LINE__`) an error, and rejected `#define defined`.
  Three §6.10.8.4 cases remain unguarded:
  - `#undef defined` is silently accepted (`defined` is not a macro-table entry,
    so the name must be matched from the token text, not via a symtab lookup).
  - `#undef __LINE__` / `#undef __FILE__` (and the other predefineds) silently
    drop the macro instead of erroring.
  - Identical redefinition of a predefined macro (e.g. `#define __LINE__ 1`) is
    accepted because it matches the stored body; §6.10.8.4 forbids `#define` of a
    predefined name regardless of the replacement list.
- **Scope (in [macro.c](macro.c)/[direct.c](direct.c)):** flag predefined
  symbols (and `defined`) so both `#define` and `#undef` of them → `pperror`,
  independent of the replacement-list comparison.
