//
// Class Session: collect parameters for simulation and run the Machine instance.
// Hide all internals from user.
//
#ifndef DUBNA_SESSION_H
#define DUBNA_SESSION_H

#include <iostream>
#include <memory>
#include <string>

#include "besm6_arch.h"

//
// External interface to the simulator.
//
class Session {
public:
    // Constructor.
    explicit Session();

    // Set name of the a.out program to run.
    void set_program_file(const char *filename);
    void set_program_file(const std::string &filename) { set_program_file(filename.c_str()); }
    const std::string &get_program_file();

    // Run simulation session with given parameters.
    void run();

    // Finish simulation.
    void finish();

    // Get status of simulation: either EXIT_SUCCESS (0) or
    // EXIT_FAILURE in case of errors.
    int get_exit_status();

    // Fail after the specified number of instructions.
    void set_limit(uint64_t count);
    static uint64_t get_default_limit();

    // Enable verbose mode: print more details to the trace log.
    void set_verbose(bool on = true);

    // Enable a trace log to stdout or to the specified file.
    void enable_trace(const char *mode);
    void set_trace_file(const char *filename, const char *default_mode);

    // Get the number of simulated instructions.
    static uint64_t get_instr_count();

    // Get version of the simulator.
    static const char *get_version();

    // Access to memory.
    void mem_write(const Words &input, unsigned addr);
    void mem_read(Words &output, unsigned nwords, unsigned addr);

    // Destructor.
    virtual ~Session();

    // Delete copy/move constructors.
    Session(const Session &)            = delete;
    Session &operator=(const Session &) = delete;
    Session(Session &&)                 = delete;
    Session &operator=(Session &&)      = delete;

private:
    // Use a "pImpl" idiom to hide implementation details.
    // Place the object representation in a separate class,
    // accessed through an opaque pointer.
    class Hidden;
    std::unique_ptr<Hidden> internal;
};

#endif // DUBNA_SESSION_H
