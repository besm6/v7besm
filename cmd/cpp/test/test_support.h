// Convenience matchers layered on top of the harness, so individual test files
// read as one-liners keyed to C11 clauses.
#pragma once

#include "cpp_harness.h"
#include <gtest/gtest.h>

namespace c11pp {

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

}  // namespace c11pp
