//
// Class Session: collect parameters for simulation and run the Machine instance.
// Hide all internals from user.
//
#include "session.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "machine.h"
#include "memory.h"

#ifdef _WIN32
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <unistd.h>
#define ISATTY isatty
#define FILENO fileno
#endif

//
// Internal implementation of the simulation session, hidden from user.
//
class Session::Hidden {
private:
    Memory memory;
    Machine machine{ memory };

    // A name of the a.out program to run.
    std::string program_file;

    // Status of the simulation.
    int exit_status{ EXIT_SUCCESS };

public:
    //
    // Instantiate the session.
    //
    explicit Hidden()
    {
        // Enable progress message only when error output goes to a user terminal.
        if (ISATTY(FILENO(stdin))) {
            // Print a progress message on stderr every few seconds.
            machine.enable_progress_message(true);
        }
    }

    //
    // Get status of simulation: either EXIT_SUCCESS (0) or
    // EXIT_FAILURE in case of errors.
    //
    int get_exit_status() const { return exit_status; }

    //
    // Set name of the a.out program to run.
    //
    void set_program_file(const char *filename)
    {
        if (!program_file.empty()) {
            std::cerr << "Too many program files: " << filename << std::endl;
            ::exit(EXIT_FAILURE);
        }
        program_file = filename;
    }

    const std::string &get_program_file() const { return program_file; }

    //
    // Load and run the a.out program.
    //
    void run()
    {
        try {
            machine.load_program(program_file);
            machine.run();
            machine.finish();

            // Propagate the program's own _exit() status.
            exit_status = machine.get_exit_status();
        } catch (const std::exception &ex) {
            // Print exception message.
            std::cerr << "Error: " << ex.what() << std::endl;
            exit_status = EXIT_FAILURE;
        } catch (...) {
            // Assuming the exception message already printed.
            exit_status = EXIT_FAILURE;
        }
    }

    //
    // Finish simulation.
    // Close trace files.
    //
    static void finish()
    {
        // Finish the trace output.
        Machine::close_trace();
    }

    //
    // Enable verbose mode.
    // Print more details to the trace log.
    //
    void set_verbose(bool on) { machine.set_verbose(on); }

    //
    // Print the program exit status as a signed integer (--status option).
    //
    void set_report_status(bool on) { machine.set_report_status(on); }

    //
    // Enable trace log to stdout.
    //
    static void enable_trace(const char *mode)
    {
        if (mode && *mode) {
            Machine::enable_trace(mode);
        } else {
            Machine::close_trace();
        }
    }

    //
    // Enable trace log to the specified file.
    //
    static void set_trace_file(const char *filename, const char *default_mode)
    {
        Machine::redirect_trace(filename, default_mode);
        Machine::get_trace_stream() << "BESM-6 Simulator (b6sim) Version: " << VERSION_STRING
                                    << "\n";
    }

    //
    // Fail after the specified number of instructions.
    //
    void set_limit(uint64_t count) { machine.set_limit(count); }

    //
    // Backdoor access to DRAM memory.
    // No tracing.
    //
    void mem_write(const Words &input, uint64_t addr) { memory.write_words(input, addr); }

    void mem_read(Words &output, unsigned nrows, uint64_t addr)
    {
        memory.read_words(output, nrows, addr);
    }
};

//
// Instaltiate the Session object.
// Allocate the internal implementation.
//
Session::Session() : internal(std::make_unique<Session::Hidden>())
{
}

//
// Destructor: implicitly delete the hidden object.
//
Session::~Session() = default;

//
// Set name of the a.out program to run.
//
void Session::set_program_file(const char *filename)
{
    internal->set_program_file(filename);
}

const std::string &Session::get_program_file()
{
    return internal->get_program_file();
}

//
// Get status of simulation: either EXIT_SUCCESS (0) or
// EXIT_FAILURE in case of errors.
//
int Session::get_exit_status()
{
    return internal->get_exit_status();
}

//
// Run simulation session with given parameters.
//
void Session::run()
{
    internal->run();
}

//
// Finish simulation.
//
void Session::finish()
{
    internal->finish();
}

//
// Enable a trace log to stdout or to the specified file.
//
void Session::enable_trace(const char *mode)
{
    internal->enable_trace(mode);
}

void Session::set_trace_file(const char *filename, const char *default_mode)
{
    internal->set_trace_file(filename, default_mode);
}

//
// Enable verbose mode.
//
void Session::set_verbose(bool on)
{
    internal->set_verbose(on);
}

//
// Print the program exit status as a signed integer.
//
void Session::set_report_status(bool on)
{
    internal->set_report_status(on);
}

//
// Fail after the specified number of instructions.
//
void Session::set_limit(uint64_t count)
{
    internal->set_limit(count);
}

//
// Query the default limit of instructions.
//
uint64_t Session::get_default_limit()
{
    return Machine::get_default_limit();
}

//
// Get the number of simulated instructions.
//
uint64_t Session::get_instr_count()
{
    return Machine::get_instr_count();
}

//
// Get version of the simulator.
//
const char *Session::get_version()
{
    // Return string, obtained from CMakeLists.txt.
    return VERSION_STRING;
}

//
// Access to DRAM memory.
//
void Session::mem_write(const Words &input, unsigned addr)
{
    internal->mem_write(input, addr);
}

void Session::mem_read(Words &output, unsigned nrows, unsigned addr)
{
    internal->mem_read(output, nrows, addr);
}
