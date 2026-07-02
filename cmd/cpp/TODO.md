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

## 18. Protect predefined macros and `defined` from `#undef` (follow-on to task 7) — DONE

- **Tests:** `Predefined.RedefiningStdcDiagnosed`, `Predefined.UndefLineDiagnosed`,
  `Predefined.UndefFileDiagnosed`, `Predefined.UndefStdcDiagnosed`,
  `Predefined.UndefDefinedDiagnosed`, and the scope guard
  `Predefined.UndefPlatformMacroAllowed` in
  [test/test_predefined_macros.cpp](test/test_predefined_macros.cpp).
- **What was done:** added a `predefined` flag to `struct symtab`
  ([defs.h](defs.h)), set on the seven C11 standard predefined macros at
  registration ([cpp.c](cpp.c)); `do_define` ([macro.c](macro.c)) and the `#undef`
  handler ([direct.c](direct.c)) now `pperror` on any `#define`/`#undef` of a
  flagged macro (independent of the replacement-list comparison, so identical
  redefinition is rejected too). `defined` — which is not a symbol-table entry —
  is matched by token text in both handlers. The non-standard platform macros
  (`unix`, `pdp11`, …) are deliberately left `#undef`-able, matching §6.10.8.4 and
  GCC/Clang.
