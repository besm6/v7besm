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
