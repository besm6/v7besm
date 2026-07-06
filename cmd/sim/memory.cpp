//
// BESM-6 memory unit.
//
#include "memory.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

//
// Backdoor read from memory.
// No tracing.
//
void Memory::read_words(Words &output, unsigned nwords, unsigned addr)
{
    output.resize(nwords);
    memcpy(output.data(), &mem[addr], nwords * sizeof(Word));
}

//
// Backdoor write to memory.
// No tracing.
//
void Memory::write_words(const Words &input, unsigned addr)
{
    unsigned nwords = input.size();
    memcpy(&mem[addr], input.data(), nwords * sizeof(Word));
}

//
// Bulk read from memory.
//
void Memory::read_words(Word output[], unsigned nwords, unsigned addr)
{
    memcpy(output, &mem[addr], nwords * sizeof(Word));
}

//
// Bulk write to memory.
//
void Memory::write_words(const Word input[], unsigned nwords, unsigned addr)
{
    memcpy(&mem[addr], input, nwords * sizeof(Word));
}

//
// Dump block of memory to file.
//
void Memory::dump(unsigned serial_num, unsigned disk_unit, unsigned zone, unsigned sector,
                  unsigned addr, unsigned nwords)
{
    // Create unique filename.
    std::ostringstream buf;
    buf << serial_num << "-";
    if (disk_unit == 0) {
        buf << "phys";
    } else if (disk_unit < 030) {
        buf << "drum" << std::oct << disk_unit;
    } else {
        buf << "disk" << std::oct << disk_unit;
    }
    buf << "-zone" << zone;
    if (nwords < 1024)
        buf << "-sector" << sector;
    buf << ".dump";

    // Open file.
    std::string filename = buf.str();
    std::ofstream out(filename);
    if (!out.is_open())
        throw std::runtime_error("Cannot create " + filename);

    out << "; " << filename << std::endl;
    for (; nwords > 0; nwords--, addr++) {
        auto word = mem[addr] & BITS48;
        if (word == 0)
            continue;

        out << "в " << std::oct << std::setfill('0') << std::setw(5) << addr << "  с ";
        besm6_print_word_octal(out, word);
        out << "  к ";
        besm6_print_instruction_mnemonics(out, (unsigned)(word >> 24));
        out << ", ";
        besm6_print_instruction_mnemonics(out, word & BITS(24));
        out << std::endl;
    }
}
