//
// Extracode 077 is the Unix v7 system-call trap.
//
// A program issues a syscall with `$77 <number>`, which the CPU decodes as
// extracode 077 with the syscall number as its executive address (left in M[14]
// by the instruction dispatch in processor.cpp).  Arguments follow the BESM-6
// calling convention (doc/Besm6_Calling_Conventions.md): the last/only argument
// is in the accumulator, the earlier ones sit below the stack pointer, and the
// result is returned in the accumulator (errno in M[14]).  The trap resumes at
// the following instruction, so the stub returns to its caller on its own
// (e.g. `13 uj`).  The syscalls themselves live in syscall.cpp; the numbers are
// in kernel/sysent.c.
//
#include "machine.h"

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
