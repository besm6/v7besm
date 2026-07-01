// Shared test harness and convenience matchers for the C11 preprocessor
// conformance suite, all exposed as methods of the PreprocessorTest fixture.
//
// Every test drives an external preprocessor (the "preprocessor-under-test",
// selected at configure time via the C11PP_COMMAND / C11PP_ARGS CMake cache
// variables) over a small source snippet and inspects the result.  The command
// and its base arguments are injected as compile-time string macros; see
// CMakeLists.txt.  Tests derive from PreprocessorTest (usually via a per-suite
// alias) so the harness and matcher calls read as plain one-liners.
//
// The fixture methods are declared here and implemented in test_support.cpp.
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

// Convenience assertion macros.  They expand to unqualified calls, so they are
// usable only inside a PreprocessorTest method (a TEST_F body) — which is the
// only place tests assert anything.
#define EXPECT_TOKENS(src, expected) EXPECT_TRUE(TokensAre((src), (expected)))
#define EXPECT_PP_OK(src) EXPECT_TRUE(Succeeds((src)))
#define EXPECT_PP_DIAGNOSES(src) EXPECT_TRUE(Diagnoses((src)))

// Shared gtest fixture for every preprocessor test suite: it owns the whole
// harness + matcher API as methods, so tests read as plain unqualified calls.
// Each suite aliases this fixture to keep its own name, e.g.
// `using Macro = c11pp::PreprocessorTest;` then `TEST_F(Macro, ...)`.
class PreprocessorTest : public ::testing::Test {
protected:
    using Result  = ::c11pp::Result;   // so tests can write `Result r = ...`
    using AuxFile = ::c11pp::AuxFile;

    // Run the preprocessor-under-test on `source`.  The snippet is written to a
    // throwaway `*.c` file in a unique temp directory (auto-removed), together
    // with any `aux` files, and the temp directory is placed on the include
    // search path so `#include "foo.h"` / `<foo.h>` resolve to the aux files.
    static Result Preprocess(const std::string& source,
                             const std::vector<std::string>& extraArgs = {},
                             const std::vector<AuxFile>& aux = {});

    // Run in "strict" mode: base args plus C11PP_STRICT_ARGS (e.g. clang's
    // -Werror -pedantic-errors).  Constraint violations that a conformant
    // implementation must diagnose but need not treat as fatal become hard
    // errors, so negative tests can assert a nonzero exit code portably.
    static Result PreprocessStrict(const std::string& source,
                                   const std::vector<std::string>& extraArgs = {},
                                   const std::vector<AuxFile>& aux = {});

    // Canonicalise preprocessor output for robust comparison across tools: drop
    // GNU line-marker / "#line" lines and collapse incidental whitespace, while
    // preserving token identity and order (string/char literals kept intact).
    static std::string Normalize(const std::string& out);

    // Assert that preprocessing `source` succeeds and its normalized output
    // equals `expected` (also normalized, so callers can write natural spacing).
    ::testing::AssertionResult TokensAre(const std::string& source,
                                         const std::string& expected,
                                         const std::vector<std::string>& extraArgs = {},
                                         const std::vector<AuxFile>& aux = {});

    // Assert that preprocessing succeeds (exit 0), ignoring the output.
    ::testing::AssertionResult Succeeds(const std::string& source,
                                        const std::vector<std::string>& extraArgs = {},
                                        const std::vector<AuxFile>& aux = {});

    // Assert that a constraint/error is diagnosed: run in strict mode and
    // require a nonzero exit (a conformant tool must at least diagnose; strict
    // mode makes the mandated diagnostic fatal so we can check it portably).
    ::testing::AssertionResult Diagnoses(const std::string& source,
                                         const std::vector<std::string>& extraArgs = {},
                                         const std::vector<AuxFile>& aux = {});
};

}  // namespace c11pp
