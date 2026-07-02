// C11 §6.10.8 — Predefined macro names.
#include "test_support.h"

using Predefined = PreprocessorTest;

// §6.10.8.1: __STDC__ expands to 1.
TEST_F(Predefined, StdcIsOne) {
    EXPECT_TOKENS("__STDC__\n", "1");
}

// §6.10.8.1: __STDC_VERSION__ is 201112L for C11 — the defining evidence that
// the tool is operating in C11 mode.
TEST_F(Predefined, StdcVersionIsC11) {
    EXPECT_TOKENS("__STDC_VERSION__\n", "201112L");
}

// §6.10.8.1: __STDC_HOSTED__ is defined (1 for a hosted implementation).
TEST_F(Predefined, StdcHostedDefined) {
    EXPECT_TOKENS("#ifdef __STDC_HOSTED__\nHOSTED\n#endif\n", "HOSTED");
}

// §6.10.8.1: __LINE__ is the current source line number.
TEST_F(Predefined, LineNumber) {
    // __LINE__ appears on the third line of the snippet.
    EXPECT_TOKENS("\n\n__LINE__\n", "3");
}

// §6.10.8.1: __FILE__ is a string literal naming the current source file.
TEST_F(Predefined, FileIsStringLiteral) {
    Result r = Preprocess("__FILE__\n");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    std::string out = Normalize(r.out);
    ASSERT_GE(out.size(), 2u);
    EXPECT_EQ(out.front(), '"');
    EXPECT_EQ(out.back(), '"');
    EXPECT_NE(out.find("input.c"), std::string::npos) << "got: " << out;
}

// §6.10.8.1: __DATE__ is "Mmm dd yyyy" (11 chars) and __TIME__ is "hh:mm:ss"
// (8 chars).  We check the mandated shape, not the volatile value.  Note the
// day field uses a leading space for values < 10, so the raw output must NOT be
// whitespace-normalized here — extract the quoted spans directly.
// Drop lines whose first non-blank character is '#' (GNU line markers carry
// quoted file names that would otherwise be mistaken for the date literal).
static std::string StripDirectiveLines(const std::string& s) {
    std::string out;
    std::istringstream is(s);
    std::string line;
    while (std::getline(is, line)) {
        std::size_t p = line.find_first_not_of(" \t");
        if (p != std::string::npos && line[p] == '#') continue;
        out += line;
        out.push_back('\n');
    }
    return out;
}

static std::string NthQuotedSpan(const std::string& s, int n) {
    std::size_t pos = 0;
    for (int i = 0; i <= n; ++i) {
        std::size_t open = s.find('"', pos);
        if (open == std::string::npos) return {};
        std::size_t close = s.find('"', open + 1);
        if (close == std::string::npos) return {};
        if (i == n) return s.substr(open, close - open + 1);
        pos = close + 1;
    }
    return {};
}

TEST_F(Predefined, DateAndTimeShape) {
    Result r = Preprocess("__DATE__\n__TIME__\n");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    std::string body = StripDirectiveLines(r.out);
    std::string date = NthQuotedSpan(body, 0);
    std::string time = NthQuotedSpan(body, 1);
    ASSERT_FALSE(date.empty()) << "no date literal in: " << r.out;
    ASSERT_FALSE(time.empty()) << "no time literal in: " << r.out;
    EXPECT_EQ(date.size() - 2, 11u) << "date: " << date;  // "Mmm dd yyyy"
    EXPECT_EQ(time.size() - 2, 8u) << "time: " << time;    // "hh:mm:ss"
}

// §6.10.8.4: none of the predefined macros (nor `defined`) may be redefined.
TEST_F(Predefined, RedefiningLineDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define __LINE__ 7\n");
}

TEST_F(Predefined, DefiningDefinedDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define defined 1\n");
}

// §6.10.8.4: #define of a predefined macro is an error even when the
// replacement list matches the built-in body (task 7 only caught mismatches).
TEST_F(Predefined, RedefiningStdcDiagnosed) {
    EXPECT_PP_DIAGNOSES("#define __STDC__ 1\n");
}

// §6.10.8.4: none of the predefined macros (nor `defined`) may be #undef'd.
TEST_F(Predefined, UndefLineDiagnosed) {
    EXPECT_PP_DIAGNOSES("#undef __LINE__\n");
}

TEST_F(Predefined, UndefFileDiagnosed) {
    EXPECT_PP_DIAGNOSES("#undef __FILE__\n");
}

TEST_F(Predefined, UndefStdcDiagnosed) {
    EXPECT_PP_DIAGNOSES("#undef __STDC__\n");
}

// `defined` is not a symbol-table entry, yet §6.10.8.4 still forbids #undef'ing it.
TEST_F(Predefined, UndefDefinedDiagnosed) {
    EXPECT_PP_DIAGNOSES("#undef defined\n");
}

// Scope guard: the non-standard platform macros are ordinary and stay
// freely #undef'able (matching §6.10.8.4 and GCC/Clang).
TEST_F(Predefined, UndefPlatformMacroAllowed) {
    EXPECT_PP_OK("#undef unix\n");
}
