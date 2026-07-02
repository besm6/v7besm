# b6cpp — C11 conformance TODO

The GoogleTest conformance suite in [test/](test/) drives the built `b6cpp`
binary against the C11 preprocessor requirements (ISO/IEC 9899:2011, N1570).
As of this writing **74 pass; the other 3 are marked `DISABLED_`** so the suite
stays green. This file scopes one task per failure cluster so they can be picked
up individually.

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

## 15. Diagnose wrong macro argument count (§6.10.3)

- **Tests to enable (drop `DISABLED_`):** `Macro.TooFewArgumentsDiagnosed`, `Macro.TooManyArgumentsDiagnosed`
- **Current:** argument-count mismatch is a `ppwarn` (exit 0) —
  [macro.c:362](macro.c#L362), [macro.c:368](macro.c#L368).
- **Scope:** promote `argument mismatch` from `ppwarn` to `pperror` so a
  function-like macro invoked with too few/too many arguments yields a non-zero
  exit. (Coordinate with task 5: a variadic macro's `...` legitimately absorbs
  extra arguments and must not trip this.)

## 16. Directives with leading whitespace before `#` are not recognized

- **Test to enable (drop `DISABLED_`):** `Conditional.Nested`
- **Current:** `  #if …` (indented) is passed through as ordinary text, so a
  nested conditional inside a taken group is not processed. Whitespace *after*
  the `#` (`# define`) already works; only leading whitespace *before* `#` fails.
- **Scope (directive recognition in [scan.c](scan.c)/[direct.c](direct.c)):**
  allow optional horizontal whitespace before the `#` that introduces a
  directive.

## 17. Variadic-macro extensions and edge cases (follow-on to task 5)

- **Tests to enable:** none yet — these are unimplemented extensions/edge cases
  left over from the `...` / `__VA_ARGS__` work; add tests as each is tackled.
- **Current:** task 5 implemented C99 `...` bound to `__VA_ARGS__`, but three
  related behaviors are not handled.
- **Scope (mostly in [macro.c](macro.c)):**
  - GNU **named-varargs** `#define P(args...)`: accept an identifier immediately
    before `...` and bind the trailing arguments to *that* name (in addition to
    the standard anonymous `__VA_ARGS__`).
  - GNU **`, ## __VA_ARGS__` comma elision**: when the variadic part is empty,
    the token-paste against the preceding comma drops that comma.
  - Diagnose `__VA_ARGS__` used in an **object-like** macro body. Task 5's
    diagnostic lives only in the function-like (`if (params)`) path, so
    `#define X __VA_ARGS__` is still accepted silently.

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
