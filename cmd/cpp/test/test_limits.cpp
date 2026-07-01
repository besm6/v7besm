// C11 §5.2.4.1 — Minimum translation limits the implementation must support.
// The controlling expressions/definitions are generated so we exercise the
// mandated minimums rather than hand-written approximations.
#include "test_support.h"

#include <string>

using namespace c11pp;

// §5.2.4.1: 63 nesting levels of conditional inclusion.
TEST(Limits, ConditionalNesting63) {
    const int depth = 63;
    std::string src;
    for (int i = 0; i < depth; ++i) src += "#if 1\n";
    src += "DEEP\n";
    for (int i = 0; i < depth; ++i) src += "#endif\n";
    EXPECT_TOKENS(src, "DEEP");
}

// §5.2.4.1: 4095 macro identifiers simultaneously defined in one preprocessing
// translation unit.
TEST(Limits, MacrosDefined4095) {
    const int count = 4095;
    std::string src;
    for (int i = 0; i < count; ++i)
        src += "#define M" + std::to_string(i) + " " + std::to_string(i) + "\n";
    src += "M" + std::to_string(count - 1) + "\n";  // reference the last one
    EXPECT_TOKENS(src, std::to_string(count - 1));
}

// §5.2.4.1: 127 parameters in one macro definition (and invocation).
TEST(Limits, MacroParameters127) {
    const int n = 127;
    std::string params, args;
    for (int i = 0; i < n; ++i) {
        if (i) {
            params += ",";
            args += ",";
        }
        params += "p" + std::to_string(i);
        args += std::to_string(i);
    }
    std::string src = "#define F(" + params + ") p" + std::to_string(n - 1) + "\n";
    src += "F(" + args + ")\n";
    EXPECT_TOKENS(src, std::to_string(n - 1));  // yields the last argument
}

// §5.2.4.1: 4095 characters in a logical source line.
TEST(Limits, LogicalLine4095) {
    std::string literal(4200, 'a');  // comfortably over the 4095 minimum
    std::string src = "#define LONG \"" + literal + "\"\nLONG\n";
    EXPECT_TOKENS(src, "\"" + literal + "\"");
}
