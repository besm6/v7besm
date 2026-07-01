// Shared test harness and convenience matchers for the C11 preprocessor
// conformance suite.
//
// Every test drives an external preprocessor (the "preprocessor-under-test",
// selected at configure time via the C11PP_COMMAND / C11PP_ARGS CMake cache
// variables) over a small source snippet and inspects the result.  The command
// and its base arguments are injected as compile-time string macros; see
// CMakeLists.txt.  The matchers and PreprocessorTest fixture further down layer
// on top so individual test files read as one-liners keyed to C11 clauses.
//
// The harness is declared here and implemented in test_support.cpp.
#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace c11pp {

// An auxiliary file materialised next to the main source (used by #include
// tests).  `name` is a path relative to the temporary directory.
struct AuxFile {
    std::string name;
    std::string content;
};

// Captured result of one preprocessor run.
struct Result {
    std::string out;        // captured standard output
    std::string err;        // captured standard error (diagnostics)
    int exit_code = -1;     // process exit status (nonzero == failed to translate)
};

// Run the preprocessor-under-test on `source`.  The snippet is written to a
// throwaway `*.c` file in a unique temp directory (auto-removed), together with
// any `aux` files, and the temp directory is placed on the include search path
// so that `#include "foo.h"` / `<foo.h>` resolve to the aux files.
Result Preprocess(const std::string& source,
                  const std::vector<std::string>& extraArgs = {},
                  const std::vector<AuxFile>& aux = {});

// Run in "strict" mode: base args plus C11PP_STRICT_ARGS (e.g. clang's
// -Werror -pedantic-errors).  Constraint violations that a conformant
// implementation must diagnose but need not treat as fatal become hard errors,
// so negative tests can assert a nonzero exit code portably.
Result PreprocessStrict(const std::string& source,
                        const std::vector<std::string>& extraArgs = {},
                        const std::vector<AuxFile>& aux = {});

// Canonicalise preprocessor output for robust comparison across tools:
//   * drop GNU line-marker lines ("# 1 \"file\"") and "#line" markers,
//   * collapse runs of horizontal whitespace to a single space,
//   * trim each line and drop empty lines.
// Token identity and order are preserved; incidental spacing/line-count
// differences are not.
std::string Normalize(const std::string& out);

// Assert that preprocessing `source` succeeds and its normalized output equals
// `expected` (also normalized, so callers can write natural spacing).
inline ::testing::AssertionResult TokensAre(const std::string& source,
                                            const std::string& expected,
                                            const std::vector<std::string>& extraArgs = {},
                                            const std::vector<AuxFile>& aux = {}) {
    Result r = Preprocess(source, extraArgs, aux);
    if (r.exit_code != 0) {
        return ::testing::AssertionFailure()
               << "preprocessor exited " << r.exit_code << "\n--- stderr ---\n"
               << r.err;
    }
    std::string got = Normalize(r.out);
    std::string want = Normalize(expected);
    if (got == want) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "output mismatch\n  expected: [" << want << "]\n  actual:   [" << got
           << "]";
}

// Assert that preprocessing succeeds (exit 0), ignoring the output.
inline ::testing::AssertionResult Succeeds(const std::string& source,
                                           const std::vector<std::string>& extraArgs = {},
                                           const std::vector<AuxFile>& aux = {}) {
    Result r = Preprocess(source, extraArgs, aux);
    if (r.exit_code == 0) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "expected success but exited " << r.exit_code << "\n--- stderr ---\n"
           << r.err;
}

// Assert that a constraint/error is diagnosed: run in strict mode and require a
// nonzero exit (a conformant tool must at least diagnose; strict mode makes the
// mandated diagnostic fatal so we can check it portably).
inline ::testing::AssertionResult Diagnoses(const std::string& source,
                                            const std::vector<std::string>& extraArgs = {},
                                            const std::vector<AuxFile>& aux = {}) {
    Result r = PreprocessStrict(source, extraArgs, aux);
    if (r.exit_code != 0) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "expected a diagnostic (nonzero exit) but tool exited 0\n--- stdout ---\n"
           << r.out;
}

#define EXPECT_TOKENS(src, expected) EXPECT_TRUE(::c11pp::TokensAre((src), (expected)))
#define EXPECT_PP_OK(src) EXPECT_TRUE(::c11pp::Succeeds((src)))
#define EXPECT_PP_DIAGNOSES(src) EXPECT_TRUE(::c11pp::Diagnoses((src)))

// Shared gtest fixture for every preprocessor test suite.  It exposes the
// harness (Preprocess / PreprocessStrict / Normalize) and the matcher helpers
// as members so tests read as plain calls without `::c11pp::` qualification.
// Each suite aliases this fixture to keep its own name, e.g.
// `using Macro = c11pp::PreprocessorTest;` then `TEST_F(Macro, ...)`.
class PreprocessorTest : public ::testing::Test {
protected:
    using Result  = ::c11pp::Result;   // so tests can write `Result r = ...`
    using AuxFile = ::c11pp::AuxFile;

    Result Preprocess(const std::string& src,
                      const std::vector<std::string>& extraArgs = {},
                      const std::vector<AuxFile>& aux = {}) {
        return ::c11pp::Preprocess(src, extraArgs, aux);
    }
    Result PreprocessStrict(const std::string& src,
                            const std::vector<std::string>& extraArgs = {},
                            const std::vector<AuxFile>& aux = {}) {
        return ::c11pp::PreprocessStrict(src, extraArgs, aux);
    }
    static std::string Normalize(const std::string& s) { return ::c11pp::Normalize(s); }

    // Matcher helpers, so tests that spell out an assertion (rather than use the
    // EXPECT_* macros above) can call them unqualified inside a TEST_F body.
    ::testing::AssertionResult TokensAre(const std::string& source,
                                         const std::string& expected,
                                         const std::vector<std::string>& extraArgs = {},
                                         const std::vector<AuxFile>& aux = {}) {
        return ::c11pp::TokensAre(source, expected, extraArgs, aux);
    }
    ::testing::AssertionResult Succeeds(const std::string& source,
                                        const std::vector<std::string>& extraArgs = {},
                                        const std::vector<AuxFile>& aux = {}) {
        return ::c11pp::Succeeds(source, extraArgs, aux);
    }
    ::testing::AssertionResult Diagnoses(const std::string& source,
                                         const std::vector<std::string>& extraArgs = {},
                                         const std::vector<AuxFile>& aux = {}) {
        return ::c11pp::Diagnoses(source, extraArgs, aux);
    }
};

}  // namespace c11pp
