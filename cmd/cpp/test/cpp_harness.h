// C11 preprocessor conformance suite — shared test harness.
//
// Every test drives an external preprocessor (the "preprocessor-under-test",
// selected at configure time via the C11PP_COMMAND / C11PP_ARGS CMake cache
// variables) over a small source snippet and inspects the result.  The command
// and its base arguments are injected as compile-time string macros; see
// CMakeLists.txt.
#pragma once

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

}  // namespace c11pp
