//
// Instruction and register tracing.
//
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "machine.h"

//
// Flag to enable tracing.
//
bool Machine::debug_instructions; // trace machine instuctions
bool Machine::debug_extracodes;   // trace extracodes (except e75)
bool Machine::debug_print;        // trace extracode e64
bool Machine::debug_registers;    // trace CPU registers
bool Machine::debug_memory;       // trace memory read/write
bool Machine::debug_fetch;        // trace instruction fetch
bool Machine::debug_dispak;       // trace in dispak format, to stderr
bool Machine::debug_cprog;        // trace C program: enable on *execute

//
// Emit trace to this stream.
//
std::ofstream Machine::trace_stream;

//
// Enable trace with given modes.
//  i - trace instructions
//  e - trace extracodes
//  f - trace fetch
//  r - trace registers
//  m - trace memory read/write
//  x - trace exceptions
//  d - trace in dispak format, to stderr
//
void Machine::enable_trace(const char *trace_mode)
{
    // Disable all trace options.
    debug_instructions = false;
    debug_extracodes   = false;
    debug_print        = false;
    debug_registers    = false;
    debug_memory       = false;
    debug_fetch        = false;
    debug_dispak       = false;
    debug_cprog        = false;

    if (trace_mode) {
        // Parse the mode string and enable all requested trace flags.
        for (unsigned i = 0; trace_mode[i]; i++) {
            char ch = trace_mode[i];
            switch (ch) {
            case 'e':
                debug_extracodes = true;
                break;
            case 'm':
                debug_memory = true;
                break;
            case 'i':
                debug_instructions = true;
                break;
            case 'r':
                debug_registers = true;
                break;
            case 'f':
                debug_fetch = true;
                break;
            case 'p':
                debug_print = true;
                break;
            case 'd':
                debug_dispak = true;
                break;
            case 'c':
                debug_cprog = true;
                break;
            default:
                throw std::runtime_error("Wrong trace option: " + std::string(1, ch));
            }
        }
    }
}

//
// Enable trace by bitmask,
// for example by VTM instruction with register 0.
//
void Machine::enable_trace(unsigned bitmask)
{
    debug_extracodes   = bitmask & 01;   // -d e
    debug_memory       = bitmask & 02;   // -d m
    debug_instructions = bitmask & 04;   // -d i
    debug_registers    = bitmask & 010;  // -d r
    debug_fetch        = bitmask & 020;  // -d f
    debug_print        = bitmask & 040;  // -d p
    debug_dispak       = bitmask & 0100; // -d d
    debug_cprog        = bitmask & 0200; // -d c
}

//
// Redirect trace output to a given file.
//
void Machine::redirect_trace(const char *file_name, const char *default_mode)
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
    }

    if (!trace_enabled()) {
        // Set default mode.
        enable_trace(default_mode);
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

    // Disable trace options.
    enable_trace("");
}

//
// Start tracing a C program.
//
void Machine::start_trace_on_exec()
{
    if (debug_cprog) {
        debug_memory       = true;
        debug_instructions = true;
        debug_registers    = true;
        debug_cprog        = false;

        cpu.finish();
        trace_registers();
    }
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
// Print memory write in dispak format.
// "       00016: store 0003770010053377"
//
void Machine::print_memory_write_dispak(unsigned addr, Word val)
{
    auto &out       = std::cerr;
    auto save_flags = out.flags();

    out << std::oct << "       " << std::setfill('0') << std::setw(5) << addr << ": store "
        << std::setw(16) << val << '\n';
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
// Print instruction in dispak format.
// "03652: xta 2157(1)          (=6040000000065366) acc=2031463100000000 r[1]=00141"
//
void Processor::print_instruction_dispak()
{
    auto &out       = std::cerr;
    auto save_flags = out.flags();
    auto reg        = RK >> 20;
    auto addr =
        (RK & ONEBIT(20)) ? (RK & BITS(15)) : ((RK & BITS(12)) | ((RK & ONEBIT(19)) ? 070000 : 0));
    auto word = machine.mem_load(ADDR(addr + core.M[reg]));
    std::ostringstream buf;

    besm6_print_instruction_mnemonics(buf, RK);
    out << std::oct << std::setfill('0') << std::setw(5) << core.PC << ": " << std::setfill(' ')
        << std::setw(20) << std::left << buf.str() << " (=" << std::right << std::setfill('0')
        << std::setw(16) << word << ") acc=" << std::setw(16) << core.ACC;
    if (reg) {
        out << " r[" << reg << "]=" << std::setw(5) << core.M[reg];
    }
    out << '\n';
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
