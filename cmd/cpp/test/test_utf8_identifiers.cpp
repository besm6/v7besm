// UTF-8 (high-byte) identifiers: the preprocessor treats bytes 0x80-0xFF as
// identifier characters, so Cyrillic (and other multibyte) macro names work.
// C11 §6.4.2 leaves the extended identifier set implementation-defined; b6cpp
// accepts raw UTF-8 bytes, matching GCC/Clang's default behavior.
#include "test_support.h"

using Utf8 = PreprocessorTest;

// An object-like macro with a Cyrillic name defines and expands like any other.
TEST_F(Utf8, ObjectLikeCyrillic) {
    EXPECT_TOKENS("#define длина 100\nдлина\n", "100");
}

// A high-byte identifier used in ordinary text is passed through verbatim.
TEST_F(Utf8, UndefinedCyrillicPassesThrough) {
    EXPECT_TOKENS("привет\n", "привет");
}

// A function-like macro may take Cyrillic parameter names.
TEST_F(Utf8, FunctionLikeCyrillicParams) {
    EXPECT_TOKENS("#define сумма(а,б) а+б\nсумма(2,3)\n", "2+3");
}

// Names are significant to their full length, not truncated at 8 bytes: two
// names sharing their first 10 bytes ("длина") but differing afterwards are
// distinct macros.  (Before full-length significance both collided at 8 bytes.)
TEST_F(Utf8, SignificanceBeyondEightBytes) {
    EXPECT_TOKENS(
        "#define длинаОдин 1\n"
        "#define длинаДва 2\n"
        "длинаОдин длинаДва\n",
        "1 2");
}

// #ifdef sees a Cyrillic macro name.
TEST_F(Utf8, IfdefCyrillic) {
    EXPECT_TOKENS("#define флаг 1\n#ifdef флаг\nYES\n#endif\n", "YES");
}

// #undef removes a Cyrillic macro; a later #ifdef then sees it as undefined.
TEST_F(Utf8, UndefCyrillic) {
    EXPECT_TOKENS(
        "#define длина 100\n"
        "#undef длина\n"
        "#ifdef длина\nNO\n#endif\n"
        "done\n",
        "done");
}

// A Cyrillic macro can be defined from the command line with -D.
TEST_F(Utf8, CommandLineDefine) {
    EXPECT_TRUE(TokensAre("длина\n", "100", { "-Dдлина=100" }));
}

// Marker bytes still work when Cyrillic text flows through stringize (#): the
// 0xFD stringize marker must survive alongside multibyte argument bytes.
TEST_F(Utf8, StringizeKeepsCyrillic) {
    EXPECT_TOKENS("#define S(x) #x\nS(привет)\n", "\"привет\"");
}
