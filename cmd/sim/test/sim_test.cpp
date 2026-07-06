//
// Unit tests for the BESM-6 simulator core: the Unix v7 syscall trap
// (extracode 077) and illegal-instruction handling.
//
// The tests poke raw instruction words into memory and run the machine
// directly, avoiding a dependency on the assembler/linker.  A 48-bit word
// packs two 24-bit instructions: the left one (bits 48..25) executes first,
// then the right one (bits 24..1).
//
#include <cstdio>

#include <gtest/gtest.h>

#include "machine.h"
#include "memory.h"

//
// Program entry point: BADDR = HDRSZ / W = 48 / 6 = 8.
//
static const unsigned ENTRY = 8;

//
// Encode a short instruction: register modifier, opcode (000..077), address.
//
static unsigned insn(unsigned reg, unsigned opcode, unsigned addr)
{
    return (reg << 20) | (opcode << 12) | (addr & 07777);
}

//
// Pack two 24-bit instructions into one word (left executes first).
//
static Word word(unsigned left, unsigned right)
{
    return ((Word)left << 24) | right;
}

// $77 N — extracode 077 (Unix syscall) with syscall number N.
static unsigned syscall_insn(unsigned num)
{
    return insn(0, 077, num);
}

//
// _exit(status) returns the status through Machine::get_exit_status().
//
TEST(Syscall, ExitStatus)
{
    Memory memory;
    Machine machine{ memory };

    // exit in the left half; the right half is never reached.
    memory.store(ENTRY, word(syscall_insn(1), 0));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc(0);

    machine.run();
    EXPECT_EQ(machine.get_exit_status(), 0);
}

TEST(Syscall, ExitNonZeroStatus)
{
    Memory memory;
    Machine machine{ memory };

    memory.store(ENTRY, word(syscall_insn(1), 0));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc(5);

    machine.run();
    EXPECT_EQ(machine.get_exit_status(), 5);
}

//
// write() (treated as putch) outputs the character in the accumulator; the
// program then exits.
//
TEST(Syscall, WriteThenExit)
{
    Memory memory;
    Machine machine{ memory };

    // write in the left half, exit in the right half of the same word.
    memory.store(ENTRY, word(syscall_insn(4), syscall_insn(1)));
    machine.cpu.set_pc(ENTRY);
    machine.cpu.set_acc('A');

    testing::internal::CaptureStdout();
    machine.run();
    fflush(stdout);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(out, "A");
    EXPECT_EQ(machine.get_exit_status(), 'A');
}

//
// An unimplemented syscall terminates with an error.
//
TEST(Syscall, UnimplementedTerminates)
{
    Memory memory;
    Machine machine{ memory };

    memory.store(ENTRY, word(syscall_insn(99), 0));
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}

//
// An illegal instruction terminates the simulation with an error.
//
TEST(Cpu, IllegalInstructionTerminates)
{
    Memory memory;
    Machine machine{ memory };

    // Opcode 002 (рег/mod) is illegal in user mode.
    memory.store(ENTRY, word(insn(0, 002, 0), 0));
    machine.cpu.set_pc(ENTRY);

    EXPECT_THROW(machine.run(), std::runtime_error);
}
