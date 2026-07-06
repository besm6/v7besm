//
// BESM-6 architecture details.
//
#ifndef BESM6_ARCH_H
#define BESM6_ARCH_H

#include <cstdint>
#include <ostream>
#include <vector>

//
// Page, or zone, has 1024 words.
//
static const unsigned PAGE_NWORDS = 1024;

//
// Memory has 32 pages, or 32768 words.
//
static const unsigned MEMORY_NWORDS = 32 * PAGE_NWORDS;

//
// 48-bit memory word in lower bits of uint64_t value.
//
using Word = uint64_t;

//
// Array of words.
//
using Words = std::vector<Word>;

//
// Get instruction mnemonics by opcode, in range 000..077 or 0200..0370.
//
const char *besm6_opname(unsigned opcode);

//
// Find highest bit.
// Bit 48 returns 1, bit 47 -> 2 and so on.
//
unsigned besm6_highest_bit(Word val);

//
// Pack bits by mask.
//
Word besm6_pack(Word val, Word mask);

//
// Unpack bits by mask.
//
Word besm6_unpack(Word val, Word mask);

//
// Count bits.
//
unsigned besm6_count_ones(Word word);

//
// Check whether instruction is extracode.
//
bool is_extracode(unsigned opcode);

//
// Print BESM-6 word.
//
void besm6_print_word_octal(std::ostream &out, Word value);

//
// Print BESM-6 instruction.
//
void besm6_print_instruction_octal(std::ostream &out, unsigned cmd);
void besm6_print_instruction_mnemonics(std::ostream &out, unsigned cmd);

//
// Convert numbers to strings.
//
std::string to_octal(unsigned val);

//
// Bits of memory word, from right to left, starting from 1.
//
#define ONEBIT(n) (1ULL << (n - 1))             // single bit, from 1 to 64
#define BITS(n)   ((uint64_t)~0ULL >> (64 - n)) // mask of bits n..1
#define ADDR(x)   ((x) & BITS(15))              // word address

#define BIT40  0'0010'0000'0000'0000LL // bit 40 - most significant bit of mantissa
#define BIT41  0'0020'0000'0000'0000LL // bit 41 - sign
#define BIT42  0'0040'0000'0000'0000LL // bit 42 - duplicate sign in mantissa
#define BIT48  0'4000'0000'0000'0000LL // bit 48 - exponent sign
#define BITS40 0'0017'7777'7777'7777LL // bits 40..1 - mantissa
#define BITS41 0'0037'7777'7777'7777LL // bits 41..1 - mantissa and sign
#define BITS42 0'0077'7777'7777'7777LL // bits 42..1 - mantissa and both signs
#define BITS48 0'7777'7777'7777'7777LL // bits 48..1

//
// Bits of ALU mode.
//
enum {
    RAU_NORM_DISABLE  = 001, // disable normalization
    RAU_ROUND_DISABLE = 002, // disable rounding
    RAU_LOG           = 004, // logical group flag
    RAU_MULT          = 010, // multiply group flag
    RAU_ADD           = 020, // add group flag
    RAU_OVF_DISABLE   = 040, // disable overflow
    RAU_MODE          = RAU_LOG | RAU_MULT | RAU_ADD,
};

//
// Floating point value as represented in ALU.
//
class MantissaExponent {
public:
    int64_t mantissa;  // Note: signed value
    unsigned exponent; // offset by 64

    //
    // Constructors.
    //
    MantissaExponent() : mantissa(0), exponent(0) {}

    explicit MantissaExponent(Word val)
    {
        exponent = (val >> 41) & BITS(7);
        mantissa = val & BITS41;

        // Sign extend.
        mantissa <<= 64 - 41;
        mantissa >>= 64 - 41;
    }

    //
    // Whether the number is negative.
    // Must check bit 42 instead of bit 41, as the value may be
    // denormalized due to negation.
    //
    bool is_negative() { return (mantissa & BIT42) != 0; }

    //
    // Return true if the number is not normalized.
    // In a normalized number bits 42 and 41 are equal.
    //
    bool is_denormal() { return ((mantissa >> 40) ^ (mantissa >> 41)) & 1; }

    //
    // Change sign of the mantissa.
    // Note: the number may become denormalized.
    //
    void negate() { mantissa = -mantissa; }

    //
    // Normalize "to the right".
    // Increment the exponent and update the mantissa.
    //
    void normalize_to_the_right()
    {
        mantissa >>= 1;
        ++exponent;
    }

    //
    // Multiply by a signed 41-bit integer.
    // Return lower 40 bits.
    //
    uint64_t multiply(int64_t);
};

#endif // BESM6_ARCH_H
