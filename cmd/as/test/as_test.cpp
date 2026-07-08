//
// Unit tests for the cmd/as assembler engine.
//
// The engine is driven through assemble(&args), which reads an input file and
// writes a BESM-6 a.out object.  Each test names its input/output files after
// the running test (guaranteed unique) and leaves them on disk as debugging
// artefacts.
//
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// The engine library is compiled as C; assemble.h is C++-safe.
extern "C" {
#include "assemble.h"

// Renders the "file:line: " diagnostic prefix from the engine's global state
// (defined in as.c).  Declared here so the harness fatal() below matches the
// CLI's message format without pulling in the whole C-only as.h.
char *format_location(char *buf, int size);
}

// Replace the front-end fatal(): instead of exit(1), surface the assembler
// error as a GoogleTest failure via an uncaught exception.  The message carries
// the same "file:line: " prefix the CLI would print, so tests can assert on the
// reported source location.
extern "C" [[noreturn]] void fatal(char *fmt, ...)
{
    char loc[1056];
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    format_location(loc, sizeof loc);
    throw std::runtime_error(std::string(loc) + buf);
}

// Name of the currently running test, e.g. "Assemble.StopInstruction".
static std::string current_test_name()
{
    const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(info->test_suite_name()) + "." + info->name();
}

// Assemble a multi-line source and return the resulting object file bytes.
static std::vector<unsigned char> assemble(const std::string &source)
{
    std::string base    = current_test_name();
    std::string infile  = base + ".s";
    std::string outfile = base + ".o";

    {
        std::ofstream src(infile, std::ios::trunc);
        src << source;
    }

    struct assembler_args args = {};
    args.infile  = const_cast<char *>(infile.c_str());
    args.outfile = const_cast<char *>(outfile.c_str());
    EXPECT_EQ(assemble(&args), 0);

    std::ifstream obj(outfile, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(obj)),
                                      std::istreambuf_iterator<char>());
}

// High 24-bit half-word stored at word index `w` of the file (a BESM-6 word is
// two big-endian half-words; the high/left half-word executes first).
static long word_high(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Low 24-bit half-word stored at word index `w` of the file (a BESM-6 word is
// two big-endian half-words; header fields live in the low half-word).
static long word_low(const std::vector<unsigned char> &b, int w)
{
    size_t o = (size_t)w * 6 + 3;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// One symbol-table entry as serialized by cmd/libaout/fputsym.c.
struct Sym {
    std::string name;
    long type;
    long value;
};

// Walk the symbol table.  The file is header (48 B) then the const/text/data
// segments, then their relocation records (same byte sizes), then the symbol
// table (a_syms bytes).  Each entry is n_len(1) n_type(1) n_value(3, big-endian)
// followed by n_len name bytes; the table is zero-padded to a word at the end.
static std::vector<Sym> read_symbols(const std::vector<unsigned char> &b)
{
    long a_const = word_low(b, 1), a_text = word_low(b, 2), a_data = word_low(b, 3);
    long a_syms = word_low(b, 5);
    size_t off = 48 + 2 * (size_t)(a_const + a_text + a_data);
    size_t end = off + (size_t)a_syms;
    std::vector<Sym> out;
    while (off + 5 <= end && b[off] != 0) {
        int nlen   = b[off];
        long type  = b[off + 1];
        long value = ((long)b[off + 2] << 16) | ((long)b[off + 3] << 8) | b[off + 4];
        out.push_back({ std::string(reinterpret_cast<const char *>(&b[off + 5]), nlen), type, value });
        off += 5 + nlen;
    }
    return out;
}

// The i-th relocation half-word of a segment (0 = const, 1 = text, 2 = data).
// Relocation records follow the segments, one 3-byte record per emitted
// half-word, in the same const/text/data order.
static long reloc_half(const std::vector<unsigned char> &b, int seg, int i)
{
    long a_const = word_low(b, 1), a_text = word_low(b, 2), a_data = word_low(b, 3);
    size_t o = 48 + (size_t)(a_const + a_text + a_data); // start of relocation area
    if (seg >= 1)
        o += a_const;
    if (seg >= 2)
        o += a_text;
    o += (size_t)i * 3;
    return ((long)b[o] << 16) | ((long)b[o + 1] << 8) | b[o + 2];
}

// Assemble every machine instruction from the assembler's table[] and check the
// opcode emitted for each one.  Two 24-bit instructions pack into one 48-bit
// word: the first lands in the high (left, executed-first) half-word, the second
// in the low half-word.  The three TALIGN instructions (vjm/ij/stop) cause a utc
// (02200000) filler to be inserted in the low half so the following instruction
// stays word-aligned.  Text begins at file word 8 (a_entry/HDRSZ).
//
// Every instruction also carries an operand, so this exercises the whole address
// path without growing the text segment (still 41 words): direct addresses in
// every number base (decimal, octal 0NNN, hex 0xNN, binary 0bNN), the bit-mask and
// expression syntaxes, a trailing index and a leading modifier register (reg << 20),
// backward label references, an equate, and both comment forms (// and a line-start #).  Operands that change the layout
// (#const, <addr>, [addr]) are covered by the ConstPool / UtcWtc tests instead.
TEST(Assemble, AllInstructions)
{
    std::vector<unsigned char> got = assemble(R"(
K = 0252
#a line-start hash is a whole-line comment and emits nothing
start:  atx 0123            // direct octal address
        stx 100             // decimal address
        mod 0x2a            // hexadecimal address
        xts .5              // single-bit mask .N
        a+x 0100+0023       // expression: addition
        a-x 0200-0055       // subtraction
        x-a 0777&0307       // bitwise and
        amx 0100|0023       // bitwise or
        xta start           // backward label reference
        aax 0143^0060       // bitwise xor
        aex 0123, 5         // indexed: trailing register 5
        arx 07777 ~ 07654   // xor-with-complement (a ^ ~b)
        avx 1 \< 6          // shift left
        aox 0200 \> 1       // shift right
        a/x 7*011           // multiply
        a*x 0144/012        // divide
        apx 0145%012        // modulo
        aux 1+2*3           // no precedence: (1+2)*3 == 011
        acx 1+(2*3)         // grouping forces 1+(2*3) == 7
        anx {0123}          // exponent-truncate braces
        e+x 010             // operator-bearing mnemonic with operand
        e-x 0234
        asx 4095            // max 12-bit decimal == 07777
        4 xtr 0567          // leading modifier register 4
        rte 077
        yta .1
        $32 0123            // raw short opcode with an address
        $33 0246
        e+n 010
        e-n 020
        asn 0300
        ntr K               // absolute equate, resolved to its value
mid:                        // second label
        ati 2
        sti 3
        ita 4
        its 5
        mtj 6, 1            // indexed: trailing register 1
      8 j+m 7
        $46 050
        $47 051
        $50 052
        $51 053
        $52 054
        $53 055
        $54 056
        $55 057
        $56 060
        $57 061
        $60 062
        $61 063
        $62 064
        $63 065
        $64 066
        $65 067
        $66 070
        $67 071
        $70 072
        $71 073
        $72 074
        $73 075
        $74 076
        $75 077
        $76 0100
        $77 0101
        @20 040000          // long opcode with a 15-bit address
        @21 041234
        utc 050000
        wtc 0123
        vtm 040000, 2       // long opcode, indexed
        utm 0b1000          // binary address (== 010)
        uza start           // backward label, long opcode
        u1a mid
        uj 0
        vjm 060000
        ij 0123
        stop 0456
        vzm 070000
        v1m 077777          // max 15-bit address
        @36 012345
        vlm mid             // backward label
)");

    // Header sanity, so a gross regression points at the offending field.
    EXPECT_EQ(word_high(got, 0), 0x424553L); // a_magic high half == "BES"
    EXPECT_EQ(word_low(got, 0), 0x4D0107L);  // a_magic low half  == "M" + FMAGIC
    EXPECT_EQ(word_low(got, 2), 246);  // a_text  == 41 words * 6 bytes

    // Short-address instructions (opcodes 000-077, val = opcode << 12).  Each
    // now carries an operand, so the expected value is val | (addr & 07777);
    // an index or leading modifier register adds (reg << 20).
    EXPECT_EQ(word_high(got, 8), 00000000L | 0123L);  // atx 0123  (start:)
    EXPECT_EQ(word_low(got, 8), 00010000L | 0144L);   // stx 100   (decimal)
    EXPECT_EQ(word_high(got, 9), 00020000L | 052L);  // mod 0x2a  (hex)
    EXPECT_EQ(word_low(got, 9), 00030000L | 020L);   // xts .5    (bit mask)
    EXPECT_EQ(word_high(got, 10), 00040000L | 0123L); // a+x 0100+0023
    EXPECT_EQ(word_low(got, 10), 00050000L | 0123L);  // a-x 0200-0055
    EXPECT_EQ(word_high(got, 11), 00060000L | 0307L); // x-a 0777&0307
    EXPECT_EQ(word_low(got, 11), 00070000L | 0123L);  // amx 0100|0023
    EXPECT_EQ(word_high(got, 12), 00100000L | 8L);    // xta start  (label == word 8)
    EXPECT_EQ(word_low(got, 12), 00110000L | 0123L);  // aax 0143^0060
    EXPECT_EQ(word_high(got, 13), (5L << 20) | 00120000L | 0123L); // aex 0123, 5
    EXPECT_EQ(word_low(got, 13), 01130000L | 07654L); // arx 07777 ~ 07654
    EXPECT_EQ(word_high(got, 14), 00140000L | 0100L); // avx 1 \< 6   (shift left)
    EXPECT_EQ(word_low(got, 14), 00150000L | 0100L);  // aox 0200 \> 1 (shift right)
    EXPECT_EQ(word_high(got, 15), 00160000L | 077L);  // a/x 7*011    (multiply)
    EXPECT_EQ(word_low(got, 15), 00170000L | 012L);   // a*x 0144/012 (divide)
    EXPECT_EQ(word_high(got, 16), 00200000L | 1L);    // apx 0145%012 (modulo)
    EXPECT_EQ(word_low(got, 16), 00210000L | 011L);   // aux 1+2*3 == (1+2)*3
    EXPECT_EQ(word_high(got, 17), 00220000L | 7L);    // acx 1+(2*3)  (grouping)
    EXPECT_EQ(word_low(got, 17), 00230000L | 0123L);  // anx {0123}   (braces)
    EXPECT_EQ(word_high(got, 18), 00240000L | 010L);  // e+x 010
    EXPECT_EQ(word_low(got, 18), 00250000L | 0234L);  // e-x 0234
    EXPECT_EQ(word_high(got, 19), 00260000L | 07777L); // asx 4095  (== 07777)
    EXPECT_EQ(word_low(got, 19), (4L << 20) | 00270000L | 0567L); // 4 xtr 0567
    EXPECT_EQ(word_high(got, 20), 00300000L | 077L);  // rte 077
    EXPECT_EQ(word_low(got, 20), 00310000L | 1L);     // yta .1
    EXPECT_EQ(word_high(got, 21), 00320000L | 0123L); // $32 0123
    EXPECT_EQ(word_low(got, 21), 00330000L | 0246L);  // $33 0246
    EXPECT_EQ(word_high(got, 22), 00340000L | 010L);  // e+n 010
    EXPECT_EQ(word_low(got, 22), 00350000L | 020L);   // e-n 020
    EXPECT_EQ(word_high(got, 23), 00360000L | 0300L); // asn 0300
    EXPECT_EQ(word_low(got, 23), 00370000L | 0252L);  // ntr K  (K = 0252, absolute equate)
    EXPECT_EQ(word_high(got, 24), 00400000L | 2L);    // ati 2  (mid:)
    EXPECT_EQ(word_low(got, 24), 00410000L | 3L);     // sti 3
    EXPECT_EQ(word_high(got, 25), 00420000L | 4L);    // ita 4
    EXPECT_EQ(word_low(got, 25), 00430000L | 5L);     // its 5
    EXPECT_EQ(word_high(got, 26), (1L << 20) | 00440000L | 6L); // mtj 6, 1
    EXPECT_EQ(word_low(got, 26), (8L << 20) | 00450000L | 7L);  // 8 j+m 7
    EXPECT_EQ(word_high(got, 27), 00460000L | 050L);  // $46 050
    EXPECT_EQ(word_low(got, 27), 00470000L | 051L);   // $47 051
    EXPECT_EQ(word_high(got, 28), 00500000L | 052L);  // $50 052
    EXPECT_EQ(word_low(got, 28), 00510000L | 053L);   // $51 053
    EXPECT_EQ(word_high(got, 29), 00520000L | 054L);  // $52 054
    EXPECT_EQ(word_low(got, 29), 00530000L | 055L);   // $53 055
    EXPECT_EQ(word_high(got, 30), 00540000L | 056L);  // $54 056
    EXPECT_EQ(word_low(got, 30), 00550000L | 057L);   // $55 057
    EXPECT_EQ(word_high(got, 31), 00560000L | 060L);  // $56 060
    EXPECT_EQ(word_low(got, 31), 00570000L | 061L);   // $57 061
    EXPECT_EQ(word_high(got, 32), 00600000L | 062L);  // $60 062
    EXPECT_EQ(word_low(got, 32), 00610000L | 063L);   // $61 063
    EXPECT_EQ(word_high(got, 33), 00620000L | 064L);  // $62 064
    EXPECT_EQ(word_low(got, 33), 00630000L | 065L);   // $63 065
    EXPECT_EQ(word_high(got, 34), 00640000L | 066L);  // $64 066
    EXPECT_EQ(word_low(got, 34), 00650000L | 067L);   // $65 067
    EXPECT_EQ(word_high(got, 35), 00660000L | 070L);  // $66 070
    EXPECT_EQ(word_low(got, 35), 00670000L | 071L);   // $67 071
    EXPECT_EQ(word_high(got, 36), 00700000L | 072L);  // $70 072
    EXPECT_EQ(word_low(got, 36), 00710000L | 073L);   // $71 073
    EXPECT_EQ(word_high(got, 37), 00720000L | 074L);  // $72 074
    EXPECT_EQ(word_low(got, 37), 00730000L | 075L);   // $73 075
    EXPECT_EQ(word_high(got, 38), 00740000L | 076L);  // $74 076
    EXPECT_EQ(word_low(got, 38), 00750000L | 077L);   // $75 077
    EXPECT_EQ(word_high(got, 39), 00760000L | 0100L); // $76 0100
    EXPECT_EQ(word_low(got, 39), 00770000L | 0101L);  // $77 0101

    // Long-address instructions (opcodes 020-037, val = opcode << 15,
    // 15-bit address field & 077777).
    EXPECT_EQ(word_high(got, 40), 02000000L | 040000L); // @20 040000
    EXPECT_EQ(word_low(got, 40), 02100000L | 041234L);  // @21 041234
    EXPECT_EQ(word_high(got, 41), 02200000L | 050000L); // utc 050000
    EXPECT_EQ(word_low(got, 41), 02300000L | 0123L);    // wtc 0123
    EXPECT_EQ(word_high(got, 42), (2L << 20) | 02400000L | 040000L); // vtm 040000, 2
    EXPECT_EQ(word_low(got, 42), 02500000L | 010L);     // utm 0b1000 (binary == 010)
    EXPECT_EQ(word_high(got, 43), 02600000L | 8L);      // uza start (== word 8)
    EXPECT_EQ(word_low(got, 43), 02700000L | 24L);      // u1a mid   (== word 24)
    EXPECT_EQ(word_high(got, 44), 03000000L | 0L);      // uj 0
    EXPECT_EQ(word_low(got, 44), 03100000L | 060000L);  // vjm 060000 (lands low: no filler)
    EXPECT_EQ(word_high(got, 45), 03200000L | 0123L);   // ij 0123
    EXPECT_EQ(word_low(got, 45), 02200000L);            // utc filler (align after ij)
    EXPECT_EQ(word_high(got, 46), 03300000L | 0456L);   // stop 0456
    EXPECT_EQ(word_low(got, 46), 02200000L);            // utc filler (align after stop)
    EXPECT_EQ(word_high(got, 47), 03400000L | 070000L); // vzm 070000
    EXPECT_EQ(word_low(got, 47), 03500000L | 077777L);  // v1m 077777 (max 15-bit)
    EXPECT_EQ(word_high(got, 48), 03600000L | 012345L); // @36 012345
    EXPECT_EQ(word_low(got, 48), 03700000L | 24L);      // vlm mid    (== word 24)
}

// The `ext` mnemonic (opcode 033) assembles to the same word as the raw $33
// form checked above, giving the assemble->disassemble roundtrip: the
// disassembler decodes 00330000 back to "ext" (see cmd/disasm/test).
TEST(Assemble, ExtMnemonic)
{
    std::vector<unsigned char> got = assemble("        ext 0246\n");
    EXPECT_EQ(word_high(got, 8), 00330000L | 0246L); // ext 0246
}

// Header fields live in the low half-word of their file word (a_const = word 1,
// a_text = word 2, a_data = word 3, a_bss = word 4, a_syms = word 6).

// The <addr>/[addr] address-extension operands, the alignment fillers forced by
// ':' and by a mid-word label, and the '. = expr' location-counter assignment.
TEST(Assemble, UtcWtc)
{
    // <addr> emits a utc (022) carrying the address, then the instruction with
    // address 0; [addr] emits a wtc (023) the same way.  A lone ':' line, and a
    // label that would otherwise land mid-word, each force a utc (02200000)
    // filler so the next instruction starts a fresh word.  '. = . + 3' in text
    // fills the gap with utc fillers and advances the location.
    auto got = assemble(R"(
        xta <040000>
        atx [050000]
        atx 0
:
        stx 0
lbl:    atx 0
        . = . + 3
        stx 0
)");
    EXPECT_EQ(word_high(got, 8), 02200000L | 040000L);  // utc 040000
    EXPECT_EQ(word_low(got, 8), 00100000L);             // xta 0
    EXPECT_EQ(word_high(got, 9), 02300000L | 050000L); // wtc 050000
    EXPECT_EQ(word_low(got, 9), 00000000L);            // atx 0

    EXPECT_EQ(word_low(got, 10), 02200000L);  // filler from the lone ':'
    EXPECT_EQ(word_high(got, 11), 00010000L); // stx 0
    EXPECT_EQ(word_low(got, 11), 02200000L);  // filler before lbl:
    EXPECT_EQ(word_high(got, 12), 00000000L); // atx 0  (lbl:)

    EXPECT_EQ(word_low(got, 2), 54);          // a_text == 9 words
    EXPECT_EQ(word_high(got, 13), 02200000L); // a utc filler in the gap
    EXPECT_EQ(word_high(got, 16), 00010000L); // stx 0
}

// Data-emitting directives and segment switches.  Each segment accumulates
// independently and the file lays them out const/text/data/strng/bss, so the
// lone text instruction takes file word 8 and the data words follow from word
// 9; .strng folds onto the end of data; .bss only advances a_bss.
TEST(Assemble, DataDirective)
{
    // .word emits a full 48-bit word big-endian: the high 24 bits land in the
    // high (executed-first) half-word, the low 24 bits in the low half-word.
    // .half packs two 24-bit half-words per word, padding the last with zero.
    // .ascii packs six raw bytes per word, big-endian, and always appends
    // padding, so a length that is already a multiple of six gains a zero word.
    auto got = assemble(R"(
        .data
        .word 0123, 0456
        .word 0x0f0f0f
        .half 0111, 0222
        .half 0333
        .ascii "ABCDEF"
        .ascii "Hi\n"
        .text
        atx 0
        .data
        .word 0
        .word 0
        .strng
        .ascii "xy"
        .bss
        . = . + 4   // In bss the counter only advances a_bss; nothing is emitted.
)");
    // Segment sizes in the header: text, data (+strng folded in) and bss.
    EXPECT_EQ(word_low(got, 1), 0);  // a_const
    EXPECT_EQ(word_low(got, 2), 6);  // a_text == 1 word
    EXPECT_EQ(word_low(got, 3), 66); // a_data == 8 + 2 .word + 1 strng word
    EXPECT_EQ(word_low(got, 4), 24); // a_bss  == 4 words

    // .word: the data words begin at file word 9 (text holds word 8).
    EXPECT_EQ(word_high(got, 9), 0);    // .word 0123 -> high 24 bits
    EXPECT_EQ(word_low(got, 9), 0123L); //           -> low 24 bits
    EXPECT_EQ(word_high(got, 10), 0);         // .word 0456
    EXPECT_EQ(word_low(got, 10), 0456L);
    EXPECT_EQ(word_high(got, 11), 0);          // high 24 bits of the value
    EXPECT_EQ(word_low(got, 11), 0x0f0f0fL);   // low 24 bits

    // .half packs two 24-bit half-words per word, padding the last with zero.
    EXPECT_EQ(word_high(got, 12), 0111L);
    EXPECT_EQ(word_low(got, 12), 0222L);
    EXPECT_EQ(word_high(got, 13), 0333L);
    EXPECT_EQ(word_low(got, 13), 0); // pad

    // .ascii packs six raw bytes per word, big-endian.  "ABCDEF" gains the
    // always-appended pad word; "Hi\n" is zero-padded within its word.
    EXPECT_EQ(word_high(got, 14), 0x414243L); // "ABC"
    EXPECT_EQ(word_low(got, 14), 0x444546L);  // "DEF"
    EXPECT_EQ(word_high(got, 15), 0);         // the always-appended pad word
    EXPECT_EQ(word_low(got, 15), 0);
    EXPECT_EQ(word_high(got, 16), 0x48690aL); // 'H' 'i' '\n'
    EXPECT_EQ(word_low(got, 16), 0);          // zero-padded
}

// A segment switch while the current segment holds an odd (unpaired) half-word must
// flush that half-word first: emit_halfword() buffers the pending half-word in a single
// static pair shared across all segments, so without the pre-switch align the new
// segment's first half-word clobbers it and the instruction is lost.  Regression test
// for the pass1.c segment-switch flush fix.
TEST(Assemble, SegmentSwitchFlushesPendingHalfword)
{
    auto got = assemble(R"(
        atx 0525        // text: one short instruction -> an unpaired half-word
        .data
        .half 0111      // first data half-word clobbers the shared buffer (pre-fix)
        .half 0222
)");
    // Text is one word: the instruction, then a utc (EMPCOM) alignment filler.
    // With the bug, word 8 high was overwritten by the data value 0111.
    EXPECT_EQ(word_high(got, 8), 0525L);      // atx 0525 survives the .data switch
    EXPECT_EQ(word_low(got, 8), 02200000L);   // EMPCOM filler flushed by the switch

    // Data lands in its own word (const=0, text=1 word), uncorrupted.
    EXPECT_EQ(word_low(got, 2), 6);           // a_text  == 1 word
    EXPECT_EQ(word_low(got, 3), 6);           // a_data  == 1 word
    EXPECT_EQ(word_high(got, 9), 0111L);
    EXPECT_EQ(word_low(got, 9), 0222L);
}

// Values that use bits 24..31 (and full 48-bit values) must survive intact: the
// 48-bit word splits at the 24-bit half-word boundary, not at bit 32.  Each
// .word emits high 24 bits in the high (executed-first) half-word and low 24
// bits in the low half-word.  These cases all fail if the value is cut at bit 32.
TEST(Assemble, WideWords)
{
    auto got = assemble(R"(
        .data
        .word 0x1000000         // bit 24 alone (hex): high24 == 1
        .word 0xabcdef123456    // a full 48-bit value
        .word 16777216          // 2^24 (decimal) spills into the high half
        .word 0100000000        // 2^24 (octal)
        .word 0b1 \< 24         // shift exactly onto the boundary
        .word .[48:25]          // the whole high half-word
        .word .[28:20]          // a range straddling bit 24
        .word .[28=20]          // the complement of an interior range
)");
    EXPECT_EQ(word_low(got, 3), 48); // a_data == 8 words * 6 bytes

    // high 24 bits -> high half-word; low 24 bits -> low half-word.
    EXPECT_EQ(word_high(got, 8), 1L); // .word 0x1000000
    EXPECT_EQ(word_low(got, 8), 0L);
    EXPECT_EQ(word_high(got, 9), 0xabcdefL); // .word 0xabcdef123456
    EXPECT_EQ(word_low(got, 9), 0x123456L);
    EXPECT_EQ(word_high(got, 10), 1L); // .word 16777216
    EXPECT_EQ(word_low(got, 10), 0L);
    EXPECT_EQ(word_high(got, 11), 1L); // .word 0100000000
    EXPECT_EQ(word_low(got, 11), 0L);
    EXPECT_EQ(word_high(got, 12), 1L); // .word 0b1 \< 24
    EXPECT_EQ(word_low(got, 12), 0L);
    EXPECT_EQ(word_high(got, 13), 0xffffffL);   // .word .[48:25]
    EXPECT_EQ(word_low(got, 13), 0L);           // bits 25..48 == whole high half
    EXPECT_EQ(word_high(got, 14), 0xfL);        // .word .[28:20] -> bits 25..28
    EXPECT_EQ(word_low(got, 14), 0xf80000L);    //               -> bits 20..24
    EXPECT_EQ(word_high(got, 15), 0xfffff8L);   // .word .[28=20] (complement)
    EXPECT_EQ(word_low(got, 15), 0x0fffffL);
}

// A single apostrophe may separate digit groups in an integer literal, C++-style:
// it is ignored by the value, so each separated literal must assemble identically
// to its unseparated form, in every base and across the half-word boundary.
TEST(Assemble, DigitSeparators)
{
    auto got = assemble(R"(
        .data
        .word 1'000             // decimal 1000
        .word 0xab'cd'ef        // hex, low half only
        .word 0100'000'000      // octal 2^24, spills into the high half
        .word 0b1000'0000       // binary 128
        .word 0xabcd'ef12'3456  // full 48-bit, separators across both halves
)");
    EXPECT_EQ(word_high(got, 8), 0L); // .word 1'000
    EXPECT_EQ(word_low(got, 8), 1000L);
    EXPECT_EQ(word_high(got, 9), 0L); // .word 0xab'cd'ef
    EXPECT_EQ(word_low(got, 9), 0xabcdefL);
    EXPECT_EQ(word_high(got, 10), 1L); // .word 0100'000'000
    EXPECT_EQ(word_low(got, 10), 0L);
    EXPECT_EQ(word_high(got, 11), 0L); // .word 0b1000'0000
    EXPECT_EQ(word_low(got, 11), 128L);
    EXPECT_EQ(word_high(got, 12), 0xabcdefL); // .word 0xabcd'ef12'3456
    EXPECT_EQ(word_low(got, 12), 0x123456L);
}

// An apostrophe right after the base prefix (0'123, 0x'abc, 0b'111) makes the
// literal LEFT-aligned: the digits pack against the top of the 48-bit word and
// the low bits are zero.  value == digits << (48 - Ndigits*bits_per_digit); the
// octal base marker '0' is not part of the mantissa.  Internal separators still
// work inside such a literal.
TEST(Assemble, LeftAlignedLiterals)
{
    auto got = assemble(R"(
        .data
        .word 0'123      // 0123 \< 39 -> high 0x298000
        .word 0x'abc     // 0xabc \< 36 -> high 0xabc000
        .word 0b'111     // 0b111 \< 45 -> high 0xe00000
        .word 0x'ab'cd   // internal separator still works -> high 0xabcd00
        .word 0123 \< 39 // cross-check: same word as 0'123
)");
    EXPECT_EQ(word_high(got, 8), 0x298000L); // .word 0'123
    EXPECT_EQ(word_low(got, 8), 0L);
    EXPECT_EQ(word_high(got, 9), 0xabc000L); // .word 0x'abc
    EXPECT_EQ(word_low(got, 9), 0L);
    EXPECT_EQ(word_high(got, 10), 0xe00000L); // .word 0b'111
    EXPECT_EQ(word_low(got, 10), 0L);
    EXPECT_EQ(word_high(got, 11), 0xabcd00L); // .word 0x'ab'cd
    EXPECT_EQ(word_low(got, 11), 0L);
    // 0'123 must equal the explicit shift form.
    EXPECT_EQ(word_high(got, 12), word_high(got, 8)); // .word 0123 \< 39
    EXPECT_EQ(word_low(got, 12), word_low(got, 8));
}

// A .word must serialize its 48-bit value in BESM-6 big-endian half-word order:
// the high 24 bits occupy the first (high) half-word on disk and the low 24 bits
// the second (low) half-word - the same order instructions, .half, and the
// header use.  Uses an asymmetric value so a half-swap is unmistakable.
TEST(Assemble, WordDirectiveHalfOrder)
{
    auto got = assemble(R"(
        .data
        .word 0x334455667788
)");
    int dword = 8 + (int)((word_low(got, 1) + word_low(got, 2)) / 6);
    EXPECT_EQ(word_high(got, dword), 0x334455L); // high 24 bits stored first
    EXPECT_EQ(word_low(got, dword), 0x667788L);  // low 24 bits stored second
}

// The '#' constant-pool operator: the value goes into the const segment (which
// precedes text in the file) and the instruction addresses it; equal constants
// are deduplicated.
TEST(Assemble, ConstPool)
{
    auto got = assemble(R"(
        xta #0123
        xta #0123
)");
    EXPECT_EQ(word_low(got, 1), 6); // a_const == 1 word (deduplicated)
    EXPECT_EQ(word_low(got, 2), 6); // a_text  == 1 word

    // The pooled constant sits at file word 8 (right after the 8-word header).
    EXPECT_EQ(word_high(got, 8), 0);     // constant value, high 24 bits
    EXPECT_EQ(word_low(got, 8), 0123L);  //                 low 24 bits

    // Both instructions address that word (cbase == 8), packed into text word 9.
    EXPECT_EQ(word_high(got, 9), 00100000L | 8L); // xta #0123
    EXPECT_EQ(word_low(got, 9), 00100000L | 8L);  // xta #0123 (same const)
}

// A .globl label lands in the symbol table with the external bit (N_EXT == 040)
// and its relocated text address; a reference to it relocates against the text.
TEST(Assemble, Globl)
{
    auto got = assemble(R"(
        .globl foo
foo:    atx 0
        uj foo
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].name, "foo");
    EXPECT_EQ(syms[0].type, 043L); // N_EXT | N_TEXT
    EXPECT_EQ(syms[0].value, 8L);  // text base 8 + offset 0

    EXPECT_EQ(word_high(got, 8), 00000000L);     // atx 0  (foo:)
    EXPECT_EQ(word_low(got, 8), 03000000L | 8L); // uj foo -> resolved to word 8
    EXPECT_EQ(reloc_half(got, 1, 1), 020L);      // RTEXT (long field, no modifier) for the uj foo
}

// Names may contain '$' and UTF-8 multibyte (e.g. Cyrillic) characters; both
// kinds of label reach the symbol table under their exact byte spelling.  A
// leading '$' still starts a raw opcode, so '$' is only valid within a name.
TEST(Assemble, DollarAndUtf8InNames)
{
    auto got = assemble(R"(
        .globl foo$bar
        .globl метка
foo$bar: atx 0
метка:   uj foo$bar
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 2u);

    auto find = [&](const std::string &n) -> const Sym * {
        auto it = std::find_if(syms.begin(), syms.end(),
                               [&](const Sym &s) { return s.name == n; });
        return it == syms.end() ? nullptr : &*it;
    };
    const Sym *dollar = find("foo$bar");
    const Sym *cyril  = find("метка");
    ASSERT_NE(dollar, nullptr); // '$' lexed as part of the name
    ASSERT_NE(cyril, nullptr);  // high-bit UTF-8 bytes lexed as part of the name
    EXPECT_EQ(dollar->type, 043L); // N_EXT | N_TEXT
    EXPECT_EQ(dollar->value, 8L);  // foo$bar: at text base 8 + offset 0
    EXPECT_EQ(cyril->type, 043L);  // N_EXT | N_TEXT
    // метка: forces word alignment, so a utc filler fills the low half of word 8
    // and the label lands at word 9.
    EXPECT_EQ(cyril->value, 9L);
    EXPECT_EQ(word_low(got, 8), 02200000L); // utc alignment filler

    // The `uj foo$bar` reference (at word 9) resolved to word 8, proving the '$'
    // name lexed as one symbol rather than a raw '$' opcode.
    EXPECT_EQ(word_high(got, 9), 03000000L | 8L);
}

// .comm declares a common block: an external symbol whose value is the
// requested length in words.
TEST(Assemble, Comm)
{
    auto got = assemble(R"(
        .comm buf, 4
        atx 0
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].name, "buf");
    EXPECT_EQ(syms[0].type, 050L); // N_EXT | N_COMM
    EXPECT_EQ(syms[0].value, 4L);
}

// An undefined name becomes an external symbol (N_EXT | N_UNDF == 040) and the
// referencing instruction is emitted with address 0.
TEST(Assemble, ExternalReference)
{
    auto got = assemble(R"(
        uj undef
        atx 0
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].name, "undef");
    EXPECT_EQ(syms[0].type, 040L); // N_EXT | N_UNDF
    EXPECT_EQ(syms[0].value, 0L);
    EXPECT_EQ(word_high(got, 8), 03000000L); // uj undef -> address 0 (filled by the linker)

    // The reference carries an REXT (segment field 070) relocation naming `undef`
    // (symbol index 0), so the linker can patch in its address.
    EXPECT_EQ(reloc_half(got, 1, 0), 070L); // REXT (long field, no modifier), symbol index 0
}

// A data word that names a text label gets an RTEXT (segment field 020)
// relocation and stores the label's relocated address.
TEST(Assemble, TextRelocation)
{
    auto got = assemble(R"(
        atx 0
tlabel: atx 0
        .data
        .word tlabel
)");
    int dword = 8 + (int)((word_low(got, 1) + word_low(got, 2)) / 6); // first data word
    EXPECT_EQ(word_high(got, dword), 0L);
    EXPECT_EQ(word_low(got, dword), 011L);         // tlabel == text base 8 + offset 1
    EXPECT_EQ(reloc_half(got, 2, 1) & 070L, 020L); // low half (address) reloc relative to text
}

// Referencing a label defined in the .strng segment exercises TYPESEGM(N_STRNG)
// in expr.c.  Regression test for AS-14: typesegm[] was one entry short, so
// TYPESEGM(N_STRNG) read out of bounds and the reference relocated against the
// wrong segment.  The strng segment folds onto the end of data, so emit_segments rewrites
// the symbol's type to N_DATA and emits the reference as an RDATA relocation; with
// the bug the .word would relocate against some other (garbage) segment instead.
TEST(Assemble, StrngLabelReference)
{
    auto got = assemble(R"(
        .globl slabel
        atx 0
        .strng
slabel: .ascii "ab"
        .data
        .word slabel
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].name, "slabel");
    EXPECT_EQ(syms[0].type, 044L); // N_EXT | N_DATA (strng folded into data)

    // The .word resolves the strng label through TYPESEGM(N_STRNG) -> SSTRNG and
    // relocates against the folded data segment (RDATA); the bug produced a
    // different segment from the out-of-bounds typesegm[] read.
    EXPECT_EQ(reloc_half(got, 2, 1) & 070L, 030L); // RDATA (low half carries the address)
}

// Assemble a source expected to fail, returning the diagnostic message (the
// "file:line: message" text thrown by the harness fatal()).  Returns "" if no
// error was raised.  "xts .101" is a reliable trigger: a bit number > 64.
static std::string assemble_error(const std::string &source)
{
    try {
        assemble(source);
    } catch (const std::exception &e) {
        return e.what();
    }
    return "";
}

// A mid-file "# N \"file\"" marker makes fatal() report the marker's source
// location rather than the physical line of the intermediate file.
TEST(LineMarker, MidFileLocation)
{
    std::string msg = assemble_error(R"(
# 5 "bar.h"
        xts .101
)");
    EXPECT_NE(msg.find("bar.h:5: "), std::string::npos) << "message was: " << msg;
}

// A marker on the very first line (no preceding newline) is handled by the
// first-line path in assemble(); the following line is source line N.
TEST(LineMarker, FirstLineLocation)
{
    std::string msg = assemble_error("# 1 \"foo.c\"\n        xts .101\n");
    EXPECT_NE(msg.find("foo.c:1: "), std::string::npos) << "message was: " << msg;
}

// Consecutive markers (as emitted around an #include return): the last one wins.
TEST(LineMarker, ConsecutiveMarkersLastWins)
{
    std::string msg = assemble_error(R"(
# 5 "bar.h"
# 20 "baz.h"
        xts .101
)");
    EXPECT_NE(msg.find("baz.h:20: "), std::string::npos) << "message was: " << msg;
}

// A '#' line that is not a valid marker stays an ordinary whole-line comment: it
// emits nothing and leaves no source file recorded, so diagnostics fall back to
// the input file name and physical line number.
TEST(LineMarker, NonMarkerHashIsComment)
{
    // The comment line must not perturb the emitted code: assembling with and
    // without it yields identical objects.
    auto with_comment = assemble(R"(
# this is just a comment, not a marker
        atx 0123
)");
    auto without_comment = assemble(R"(
        atx 0123
)");
    EXPECT_EQ(with_comment, without_comment);

    // With no marker seen, an error reports the intermediate file, not a marker.
    std::string msg = assemble_error(R"(
# plain comment
        xts .101
)");
    EXPECT_EQ(msg.find(".c:"), std::string::npos) << "message was: " << msg;
    EXPECT_NE(msg.find(".s, "), std::string::npos) << "message was: " << msg;
}

// Defining the same label twice is an error: the second "foo:" must be rejected
// rather than silently rebinding the symbol to the later location.
TEST(Assemble, DuplicateLabel)
{
    std::string msg = assemble_error("foo: .word 1\nfoo: .word 2\n");
    EXPECT_NE(msg.find("name already defined"), std::string::npos)
        << "message was: " << msg;
}

// A ".globl" declaration only marks a name external; it does not define it, so a
// subsequent label with the same name is still the first (valid) definition.
TEST(Assemble, GloblThenLabelOk)
{
    std::string msg = assemble_error("        .globl foo\nfoo: .word 1\n");
    EXPECT_EQ(msg, "") << "message was: " << msg;
}

// The infix "name .comm len" form was removed; only ".comm name, len" is accepted.
TEST(Assemble, InfixCommRejected)
{
    std::string msg = assemble_error("buf .comm 4\n");
    EXPECT_NE(msg.find("bad command"), std::string::npos) << "message was: " << msg;
}

// ".equ name, expr" is the directive form of an equate (the infix "name .equ expr"
// form was removed).  The name takes the value and segment class of the expression.
TEST(Assemble, EquDirective)
{
    auto got = assemble(R"(
.equ val, 7
        xta val
)");
    EXPECT_EQ(word_high(got, 8), 00100000L | 7L); // xta val -> absolute value 7
    EXPECT_EQ(reloc_half(got, 1, 0) & 070L, 0L);  // RABS: val is an absolute equate
}

// The infix "name .equ expr" form was removed; only ".equ name, expr" is accepted.
TEST(Assemble, InfixEquRejected)
{
    std::string msg = assemble_error("foo .equ 5\n");
    EXPECT_NE(msg.find("bad command"), std::string::npos) << "message was: " << msg;
}

// A word matching an instruction mnemonic may still name a label: "sti:" defines
// a text label, and a later "uj sti" resolves against it (RTEXT relocation, not an
// undefined external).  Mnemonic recognition is confined to instruction position,
// so a following ':' makes the word a label.
TEST(Assemble, MnemonicAsLabel)
{
    auto got = assemble(R"(
sti:
        uj sti
)");
    EXPECT_EQ(word_high(got, 8), 03000000L | 8L);  // uj sti -> text base word 8 (relocated)
    EXPECT_EQ(reloc_half(got, 1, 0) & 070L, 020L); // RTEXT: sti resolved as a local text label
}

// In operand position a mnemonic spelling is an ordinary symbol, never an
// instruction: "uj mod" references an (undefined) symbol `mod`, which becomes an
// external reference just like any other undefined name.
TEST(Assemble, MnemonicAsOperand)
{
    auto got = assemble(R"(
        uj mod
        atx 0
)");
    auto syms = read_symbols(got);
    ASSERT_EQ(syms.size(), 1u);
    EXPECT_EQ(syms[0].name, "mod");
    EXPECT_EQ(syms[0].type, 040L);                 // N_EXT | N_UNDF
    EXPECT_EQ(word_high(got, 8), 03000000L);       // uj mod -> address 0 (filled by the linker)
    EXPECT_EQ(reloc_half(got, 1, 0), 070L);        // REXT naming `mod` (symbol index 0)
}

// A "name = expr" definition may also use a mnemonic spelling: a following '=' in
// instruction position marks the word as a symbol being defined, not an opcode.
TEST(Assemble, MnemonicAsEquate)
{
    auto got = assemble(R"(
ext = 5
        xta ext
)");
    EXPECT_EQ(word_high(got, 8), 00100000L | 5L);  // xta ext -> absolute value 5
    EXPECT_EQ(reloc_half(got, 1, 0) & 070L, 0L);   // RABS: ext is an absolute equate
}

// Regression guard: a bare mnemonic followed by an operand (no ':' / '=') is still
// assembled as the instruction, unchanged.
TEST(Assemble, MnemonicStillInstruction)
{
    auto got = assemble("        sti 3\n");
    EXPECT_EQ(word_high(got, 8), 00410000L | 3L);  // sti 3 (store to index register)
}
