# b6cpp — C11 conformance TODO

The GoogleTest conformance suite in [test/](test/) drives the built `b6cpp`
binary against the C11 preprocessor requirements (ISO/IEC 9899:2011, N1570).
As of this writing **44 pass; the other 32 are marked `DISABLED_`** so the suite
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

## 5. Variadic macros `...` / `__VA_ARGS__` (C99/C11 §6.10.3)

- **Tests to enable (drop `DISABLED_`):** `Varargs.MultipleArguments`, `Varargs.SingleArgument`,
  `Varargs.NamedPlusVariadic`, `Varargs.CommasPreserved`,
  `Varargs.VaArgsOutsideVariadicDiagnosed`
- **Current:** `...` in a parameter list is rejected as `bad formal: .`
  ([macro.c:103](macro.c#L103)).
- **Scope (in [macro.c](macro.c)):**
  - Accept `...` as the last parameter; bind all trailing arguments (commas
    preserved) to `__VA_ARGS__` in the body.
  - Support a named prefix before `...` (`LOG(fmt, ...)`).
  - `__VA_ARGS__` appearing in a non-variadic macro is a constraint violation →
    `pperror` (`VaArgsOutsideVariadicDiagnosed`).

## 6. Predefined macros `__STDC__`, `__STDC_VERSION__`, `__STDC_HOSTED__`, `__DATE__`, `__TIME__` (§6.10.8)

- **Tests to enable (drop `DISABLED_`):** `Predefined.StdcIsOne`, `Predefined.StdcVersionIsC11`,
  `Predefined.StdcHostedDefined`, `Predefined.DateAndTimeShape`
- **Current:** these names pass through undefined (`__LINE__`/`__FILE__` already
  work — see the synthesis in [macro.c:311](macro.c#L311)).
- **Scope (register alongside the existing predefineds in [cpp.c](cpp.c)):**
  - `__STDC__` → `1`, `__STDC_VERSION__` → `201112L`, `__STDC_HOSTED__` → `1`.
  - `__DATE__` → `"Mmm dd yyyy"`, `__TIME__` → `"hh:mm:ss"` string literals.
    (Note: `Date.now()`/`localtime` at run time; the test only checks the
    literal *shape*, not the value.)

## 7. Diagnose illegal (re)definitions (§6.10.3 / §6.10.8.4)

- **Tests to enable (drop `DISABLED_`):** `Macro.IncompatibleRedefinitionDiagnosed`,
  `Predefined.RedefiningLineDiagnosed`, `Predefined.DefiningDefinedDiagnosed`
- **Current:** redefining a macro with a *different* body is silently accepted
  (new value wins); `#define __LINE__ 7` only warns (exit 0); `#define defined`
  is silently ignored.
- **Scope (in [macro.c](macro.c)/[direct.c](direct.c) define handling):**
  - Redefinition with a non-identical replacement list → `pperror`.
    (Identical redefinition stays legal — `Macro.IdenticalRedefinitionAllowed`
    must keep passing.)
  - `#define`/`#undef` of a predefined macro (`__LINE__`, …) or of `defined` →
    `pperror` (promote the existing `__LINE__ redefined` warning to an error).

## 8. `#line` directive has no effect (§6.10.4)

- **Tests to enable (drop `DISABLED_`):** `LineControl.SetsLineAndFile`, `LineControl.SetsLineOnly`,
  `LineControl.MacroExpandedOperands`
- **Current:** `#line` is registered but does not change `__LINE__`/`__FILE__`
  (the following line still reports its physical number/name).
- **Scope (in [direct.c](direct.c)):** macro-expand the operands, then set the
  current line counter (`cpp.line_no[...]`) and, if a second operand is present,
  the reported file name used by `__FILE__`.

## 9. `#error` directive (§6.10.5)

- **Test to enable (drop `DISABLED_`):** `ErrorDirective.SkippedErrorIsInert`
  (`ErrorDirective.StopsTranslation` currently passes only by accident — an
  unknown directive already errors.)
- **Current:** `#error` is not a known directive, so it reports
  `undefined control` **even inside a skipped `#if 0` group**.
- **Scope (in [direct.c](direct.c)):** implement `#error` so it (a) is ignored
  when the enclosing conditional group is not taken, and (b) emits its message
  and a non-zero exit when reached. Keep `StopsTranslation` green.

## 10. `#pragma` directive and `_Pragma` operator (§6.10.6, §6.10.9)

- **Tests to enable (drop `DISABLED_`):** `Pragma.UnknownPragmaAccepted`, `PragmaOperator.LeavesOtherTokens`
- **Current:** `#pragma …` reports `undefined control` (exit 1); the `_Pragma`
  operator is passed through untouched.
- **Scope:**
  - [direct.c](direct.c): accept `#pragma` and swallow an unrecognized pragma
    without error (a conformant tool ignores unknown pragmas).
  - [macro.c](macro.c)/[scan.c](scan.c): implement the `_Pragma("…")` operator —
    destringize its string-literal argument into a `#pragma …` directive line,
    leaving surrounding tokens intact.

## 11. Comment handling (§5.1.1.2 / §6.4.9)

- **Tests to enable (drop `DISABLED_`):** `TranslationPhases.BlockCommentBecomesSpace`,
  `TranslationPhases.LineCommentRemoved`, `TranslationPhases.UnterminatedCommentDiagnosed`
- **Current:** `a/**/b` becomes `ab` (comment deleted, tokens fuse — should be
  `a b`); `//` line comments are not recognized (C99); an unterminated `/*` is
  not diagnosed.
- **Scope (in [scan.c](scan.c), around the `/*` handling at
  [scan.c:122](scan.c#L122)):**
  - Replace each comment with a single space so adjacent tokens do not merge.
  - Recognize `//` … end-of-line comments.
  - Diagnose a `/*` with no closing `*/` before EOF → `pperror`.

## 12. Trigraph replacement (§5.2.1.1, translation phase 1)

- **Tests to enable (drop `DISABLED_`):** `Trigraphs.HashIntroducesDirective`,
  `Trigraphs.PunctuationMappings`, `Trigraphs.SlashActsAsLineSplice`
- **Current:** trigraph sequences are left literal. (The tests pass
  `-trigraphs -w`, which b6cpp harmlessly ignores — unknown flags do not error.)
- **Scope (early lexing in [scan.c](scan.c), before line-splice/comment
  phases):** translate the nine trigraphs unconditionally:
  `??=`→`#`, `??(`→`[`, `??)`→`]`, `??<`→`{`, `??>`→`}`, `??!`→`|`,
  `??'`→`^`, `??-`→`~`, `??/`→`\` (the last then acts as a line-continuation).

## 13. Raise translation limits to the C11 minimums (§5.2.4.1)

- **Tests to enable (drop `DISABLED_`):** `Limits.MacrosDefined4095`, `Limits.MacroParameters127`,
  `Limits.LogicalLine4095`
- **Current caps are far below the mandated minimums:** `too many defines` at
  ~388 ([macro.c:233](macro.c#L233)); `too many formals` at ~30
  ([macro.c:108](macro.c#L108)); `token too long` well under 4095 chars
  ([scan.c](scan.c) buffer).
- **Scope:** raise the relevant table/buffer sizes (macro table, formal-list
  size, line/token buffer) in [defs.h](defs.h)/[macro.c](macro.c)/[scan.c](scan.c)
  to at least: **4095** macros simultaneously defined, **127** parameters per
  macro, **4095** characters in a logical source line.

## 14. Macro rescanning / self-reference must not be a hard error (§6.10.3.4)

- **Tests to enable (drop `DISABLED_`):** `Macro.SelfReference`, `Macro.NoRescanRecursion`
- **Current:** `#define X X` then `X` errors `macro recursion`
  ([macro.c:302](macro.c#L302)); `#define f(x) x f` then `f(1)(2)` errors
  `unterminated macro call`.
- **Expected:** a macro that references itself (directly or via rescanning)
  expands **once**; the recurring name is left un-re-expanded ("blue paint"),
  so `X` → `X` and `f(1)(2)` → `1 f(2)`. No error, exit 0.
- **Scope (in [macro.c](macro.c) expansion/rescan):** mark a macro as
  "in progress" while expanding and suppress its re-expansion during rescan,
  instead of aborting. (There is a `-R` "allow recursion" flag today; the
  correct default is paint-and-continue, not error.)

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
