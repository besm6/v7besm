//
// Main memory for BESM-6: 32k pages of 1024 48-bit words.
//
#ifndef DUBNA_MEMORY_H
#define DUBNA_MEMORY_H

#include <array>

#include "besm6_arch.h"

class Memory {
private:
    // Memory contents.
    std::array<Word, MEMORY_NWORDS> mem{};

public:
    explicit Memory() = default;
    virtual ~Memory() = default;

    // Store data to memory.
    void store(unsigned addr, Word val) { mem[addr] = val; }

    // Load data from memory.
    Word load(unsigned addr) { return mem[addr]; }

    // Bulk access to memory.
    void write_words(const Words &input, unsigned addr);
    void read_words(Words &output, unsigned nwords, unsigned addr);
    void write_words(const Word input[], unsigned nwords, unsigned addr);
    void read_words(Word output[], unsigned nwords, unsigned addr);
    Word *get_ptr(unsigned addr) { return &mem[addr]; }

    // Cannot copy the Memory object.
    Memory(const Memory &)            = delete;
    Memory &operator=(const Memory &) = delete;
};

//
// Byte pointer.
//
class BytePointer {
private:
    Memory &memory;

public:
    unsigned word_addr;
    unsigned byte_index;

    BytePointer(Memory &m, unsigned wa, unsigned bi = 0) : memory(m), word_addr(wa), byte_index(bi)
    {
    }

    // Fetch byte at the pointer. No increment.
    uint8_t peek_byte()
    {
        const Word *ptr = memory.get_ptr(word_addr);
        return *ptr >> (40 - byte_index * 8);
    }

    // Get byte at the pointer, and increment.
    uint8_t get_byte()
    {
        auto ch = peek_byte();
        increment();
        return ch;
    }

    // Store byte at the pointer, and increment.
    void put_byte(uint8_t ch)
    {
        Word *ptr            = memory.get_ptr(word_addr);
        const unsigned shift = 40 - byte_index * 8;
        *ptr                 = (*ptr & ~(0xffull << shift)) | ((Word)ch << shift);
        increment();
    }

    // Store byte at the pointer, and increment.
    void increment()
    {
        byte_index++;
        if (byte_index == 6) {
            byte_index = 0;
            word_addr++;
        }
    }
};

#endif // DUBNA_MEMORY_H
