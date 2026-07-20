//
// Instruction and register tracing.
//
#include <fstream>
#include <iomanip>
#include <iostream>

#include "machine.h"

//
// Flag to enable tracing.
//
bool Machine::debug_enabled; // trace instructions, registers and memory

// Instruction fetch tracing is dormant: nothing on the command line turns it
// on.  Set it here by hand when you need to see every word fetched.
bool Machine::debug_fetch;

//
// Emit trace to this stream.
//
std::ofstream Machine::trace_stream;

//
// Enable or disable tracing of instructions, registers and memory,
// as requested by the -d option or by a VTM instruction with register 0.
//
void Machine::set_debug(bool on)
{
    debug_enabled = on;
}

//
// Redirect trace output to a given file.
//
void Machine::redirect_trace(const char *file_name)
{
    if (trace_stream.is_open()) {
        // Close previous file.
        trace_stream.close();
    }
    if (file_name && file_name[0]) {
        // Open new trace file.
        trace_stream.open(file_name);
        if (!trace_stream.is_open())
            throw std::runtime_error("Cannot write to " + std::string(file_name));

        if (!trace_enabled()) {
            // A trace file implies tracing.
            set_debug(true);
        }
    }
}

std::ostream &Machine::get_trace_stream()
{
    if (trace_stream.is_open()) {
        return trace_stream;
    }
    return std::cout;
}

void Machine::close_trace()
{
    if (trace_stream.is_open()) {
        // Close output.
        trace_stream.close();
    }

    // Disable tracing.
    set_debug(false);
}

//
// Print instruction fetch.
//
void Machine::print_fetch(unsigned addr, Word val)
{
    auto &out       = Machine::get_trace_stream();
    auto save_flags = out.flags();

    out << "      Fetch [" << std::oct << std::setfill('0') << std::setw(5) << addr << "] = ";
    besm6_print_instruction_octal(out, (val >> 24) & BITS(24));
    out << ' ';
    besm6_print_instruction_octal(out, val & BITS(24));
    out << std::endl;

    // Restore.
    out.flags(save_flags);
}

//
// Print memory read/write.
//
void Machine::print_memory_access(unsigned addr, Word val, const char *opname)
{
    auto &out       = Machine::get_trace_stream();
    auto save_flags = out.flags();

    out << "      Memory " << opname << " [" << std::oct << std::setfill('0') << std::setw(5)
        << addr << "] = ";
    besm6_print_word_octal(out, val);
    out << std::endl;

    // Restore.
    out.flags(save_flags);
}

//
// Print instruction address, opcode from RK and mnemonics.
//
void Processor::print_instruction()
{
    auto &out       = Machine::get_trace_stream();
    auto save_flags = out.flags();

    out << std::oct << std::setfill('0') << std::setw(5) << core.PC << ' '
        << (core.right_instr_flag ? 'R' : 'L') << ": ";
    besm6_print_instruction_octal(out, RK);
    out << ' ';
    besm6_print_instruction_mnemonics(out, RK);
    print_executive_address();
    out << std::endl;

    // Restore.
    out.flags(save_flags);
}

//
// Print executive address of extracode, optional.
//
void Processor::print_executive_address()
{
    if (RK & ONEBIT(20)) {
        // No extracodes in long commands.
        return;
    }
    auto opcode = (RK >> 12) & 077;
    if (is_extracode(opcode)) {
        // Extracode - print executive address,
        auto reg = (RK >> 20) & 017;
        if (reg != 0 || core.apply_mod_reg != 0) {
            auto addr = RK & 07777;
            if (RK & ONEBIT(19)) {
                addr |= 070000;
            }
            if (reg > 0) {
                addr = ADDR(core.M[reg]);
            }
            if (core.apply_mod_reg) {
                addr = ADDR(addr + core.MOD);
            }

            auto &out       = Machine::get_trace_stream();
            auto save_flags = out.flags();
            out << " = " << std::oct << addr;
            out.flags(save_flags);
        }
    }
}

//
// Print changes in CPU registers.
//
void Processor::print_registers()
{
    auto &out       = Machine::get_trace_stream();
    auto save_flags = out.flags();

    if (core.ACC != prev.ACC) {
        out << "      ACC = ";
        besm6_print_word_octal(out, core.ACC);
        out << std::endl;
    }
    if (core.RMR != prev.RMR) {
        out << "      RMR = ";
        besm6_print_word_octal(out, core.RMR);
        out << std::endl;
    }
    for (unsigned i = 0; i < 16; i++) {
        if (core.M[i] != prev.M[i]) {
            out << "      M" << std::oct << i << " = " << std::setfill('0') << std::setw(5)
                << core.M[i] << std::endl;
        }
    }
    if (core.RAU != prev.RAU) {
        out << "      RAU = " << std::oct << std::setfill('0') << std::setw(2) << core.RAU
            << std::endl;
    }
    if (core.apply_mod_reg != prev.apply_mod_reg) {
        if (core.apply_mod_reg) {
            out << "      MOD = " << std::oct << std::setfill('0') << std::setw(5) << core.MOD;
        } else {
            out << "      Clear MOD";
        }
        out << std::endl;
    }

    // Update previous state.
    prev = core;

    // Restore output flags.
    out.flags(save_flags);
}
