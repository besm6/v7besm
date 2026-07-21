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

    // When set (--status option), print the program's _exit() value as a
    // 41-bit signed integer on stdout.
    bool report_status_enabled{ false };

    // Program break: the first free word above the data/bss image.  Set by
    // load_program() and moved by the break() (sbrk) Unix syscall.
    unsigned program_break{ 0 };

    // Trace output.
    static std::ofstream trace_stream;

    // Trace mode: instructions, registers and memory (the -d option).
    static bool debug_enabled;

    // Instruction fetch tracing, dormant: no option turns it on.
    static bool debug_fetch;

    bool after_call{};   // right after JVM instruction
    bool after_return{}; // right after UJ(13) instruction

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

    // Enable trace output to std::cout, or to the given file.
    static void set_debug(bool on);
    static void redirect_trace(const char *file_name);
    static void close_trace();
    static bool trace_enabled() { return debug_enabled || debug_fetch; }
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

    // Replace the running image with a new a.out (the exec() Unix syscall):
    // reload the program, lay the argument block at the base of the stack the
    // way the kernel's exece() does, and jump to the new entry point.  Throws
    // std::runtime_error on a bad/missing file or an oversized argument list.
    void exec(const std::string &filename, const std::vector<std::string> &argv,
              const std::vector<std::string> &envp);

    // Exit status set by the _exit() Unix syscall.  set_exit_status() takes the
    // raw accumulator: it stores the low byte as the host process return code
    // and, when --status is enabled, prints the full 41-bit signed value.
    int get_exit_status() const { return program_exit_status; }
    void set_exit_status(Word acc);

    // Enable printing of the program exit status (--status option).
    void set_report_status(bool on) { report_status_enabled = on; }

    // Program break, maintained by the break() (sbrk) Unix syscall.
    unsigned get_program_break() const { return program_break; }
    void set_program_break(unsigned addr) { program_break = addr; }

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
        if (debug_enabled)
            print_memory_access(addr, val, "Write");
    }

    static void trace_memory_read(unsigned addr, Word val)
    {
        if (debug_enabled)
            print_memory_access(addr, val, "Read");
    }

    void trace_instruction()
    {
        if (debug_enabled)
            cpu.print_instruction();
    }

    void trace_registers()
    {
        if (debug_enabled)
            cpu.print_registers();
    }

    static void print_fetch(unsigned addr, Word val);
    static void print_memory_access(unsigned addr, Word val, const char *opname);
};

#endif // DUBNA_MACHINE_H
