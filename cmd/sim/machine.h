//
// Implement BESM-6 machine.
//
#ifndef DUBNA_MACHINE_H
#define DUBNA_MACHINE_H

#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include "processor.h"

class Machine {
private:
    // Simulate this number of instructions.
    uint64_t instr_limit{ DEFAULT_LIMIT };

    // Enable a progress message to stderr.
    bool progress_message_enabled{ false };

    // Every few seconds, print a message to stderr, to track the simulation progress.
    void show_progress();

    // Time of last check.
    std::chrono::time_point<std::chrono::steady_clock> progress_time_last;

    // Last instr_count when progress message was printed.
    uint64_t progress_count{ 0 };

    // Program exit status, set by the _exit() Unix syscall.
    int program_exit_status{ 0 };

    // Trace output.
    static std::ofstream trace_stream;

    // Trace modes.
    static bool debug_instructions; // trace machine instuctions
    static bool debug_extracodes;   // trace extracodes (except e75)
    static bool debug_print;        // trace extracode e64
    static bool debug_registers;    // trace CPU registers
    static bool debug_memory;       // trace memory read/write
    static bool debug_fetch;        // trace instruction fetch
    static bool debug_dispak;       // trace in dispak format, to stderr
    static bool debug_cprog;        // trace C program: enable on *execute
    bool after_call{};              // right after JVM instruction
    bool after_return{};            // right after UJ(13) instruction

    // Static stuff.
    static const uint64_t DEFAULT_LIMIT;    // Limit of instructions to simulate, by default
    static bool verbose;                    // Verbose flag for tracing
    static uint64_t simulated_instructions; // Count of instructions

public:
    // 32K words of virtual memory.
    Memory &memory;

    // BESM-6 processor.
    Processor cpu;

    // Constructor.
    explicit Machine(Memory &memory);

    // Destructor.
    ~Machine();

    // Run simulation.
    void run();

    // Save output files.
    void finish();

    // Enable a progress message to stderr.
    void enable_progress_message(bool on) { progress_message_enabled = on; }

    // Get instruction count.
    static uint64_t get_instr_count() { return simulated_instructions; }
    static void incr_simulated_instructions() { simulated_instructions++; }

    // Limit the simulation to this number of instructions.
    void set_limit(uint64_t count) { instr_limit = count; }
    static uint64_t get_default_limit() { return DEFAULT_LIMIT; }

    // Verbose flag for tracing.
    static void set_verbose(bool on) { verbose = on; }
    static bool get_verbose() { return verbose; }

    // Enable trace output to the given file,
    // or to std::cout when filename not present.
    static void enable_trace(const char *mode);
    static void enable_trace(unsigned bitmask);
    static void redirect_trace(const char *file_name, const char *default_mode);
    static void close_trace();
    void start_trace_on_exec();
    static bool trace_enabled()
    {
        return debug_instructions || debug_extracodes || debug_print || debug_registers ||
               debug_memory || debug_fetch;
    }
    void set_after_call() { after_call = true; };
    void set_after_return() { after_return = true; };

    // Emit trace to this stream.
    static std::ostream &get_trace_stream();

    // Memory access.
    Word mem_fetch(unsigned addr);
    Word mem_load(unsigned addr);
    void mem_store(unsigned addr, Word val);

    // Load a BESM-6 a.out executable into memory and set the entry point.
    // Throws std::runtime_error when the file is missing or has a bad format.
    void load_program(const std::string &filename);

    // Exit status set by the _exit() Unix syscall.
    int get_exit_status() const { return program_exit_status; }
    void set_exit_status(int status) { program_exit_status = status; }

    //
    // Trace methods.
    //
    static void trace_fetch(unsigned addr, Word val)
    {
        if (debug_fetch)
            print_fetch(addr, val);
    }

    static void trace_memory_write(unsigned addr, Word val)
    {
        if (debug_memory)
            print_memory_access(addr, val, "Write");
    }

    static void trace_memory_write_dispak(unsigned addr, Word val)
    {
        if (debug_dispak)
            print_memory_write_dispak(addr, val);
    }

    static void trace_memory_read(unsigned addr, Word val)
    {
        if (debug_memory)
            print_memory_access(addr, val, "Read");
    }

    void trace_instruction(unsigned opcode)
    {
        // Print e50...e77 except e75, and also e20, e21.
        if (debug_instructions || (debug_extracodes && opcode != 075 && is_extracode(opcode)))
            cpu.print_instruction();
        if (debug_dispak)
            cpu.print_instruction_dispak();
    }

    void trace_registers()
    {
        if (debug_registers)
            cpu.print_registers();
    }

    static void print_fetch(unsigned addr, Word val);
    static void print_memory_access(unsigned addr, Word val, const char *opname);
    static void print_memory_write_dispak(unsigned addr, Word val);
};

#endif // DUBNA_MACHINE_H
