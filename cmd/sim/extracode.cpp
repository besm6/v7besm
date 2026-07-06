//
// Extracode 077 is the Unix v7 system-call trap.
//
// A program issues a syscall with `$77 <number>`, which the CPU decodes as
// extracode 077 with the syscall number as its executive address (left in M[14]
// by the instruction dispatch in processor.cpp).  Arguments follow the BESM-6
// calling convention (doc/Besm6_Calling_Conventions.md): the last/only argument
// is in the accumulator, and the result is returned in the accumulator.  The
// trap resumes at the following instruction, so the stub returns to its caller
// on its own (e.g. `13 uj`).  See kernel/sysent.c for the syscall numbers.
//
#include <cstdio>
#include <string>

#include "machine.h"

//
// Unix v7 system call numbers (subset).
//
enum {
    SYS_exit  = 1,
    SYS_write = 4,
};

//
// Execute an extracode.  Only 077 (the Unix syscall trap) is valid in user mode;
// anything else is an illegal instruction and terminates the simulation.
//
void Processor::extracode(unsigned opcode)
{
    if (opcode != 077) {
        throw Exception("Illegal extracode " + to_octal(opcode));
    }

    // The syscall number is the executive address, kept in M[14].
    syscall(core.M[14]);
}

//
// Dispatch a Unix v7 system call.
//
void Processor::syscall(unsigned num)
{
    switch (num) {
    case SYS_exit:
        // void _exit(int status): status is in the accumulator. No return.
        machine.set_exit_status(core.ACC & 0xff);
        throw Exception(""); // empty message: clean halt

    case SYS_write:
        // For now write() is treated as putch(int c): the character to output
        // is the last argument, left in the accumulator. Return it.
        {
            int ch = core.ACC & 0xff;
            putchar(ch);
            core.ACC = ch;
        }
        break;

    default:
        throw Exception("Unimplemented syscall " + std::to_string(num));
    }
}
