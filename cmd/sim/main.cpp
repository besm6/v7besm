//
// Copyright (c) 2023 Serge Vakulenko
//
#include <getopt.h>

#include <cstring>
#include <iostream>

#include "session.h"

//
// CLI options.
//
static const struct option long_options[] = {
    // clang-format off
    { "help",           no_argument,        nullptr,    'h' },
    { "version",        no_argument,        nullptr,    'V' },
    { "verbose",        no_argument,        nullptr,    'v' },
    { "limit",          required_argument,  nullptr,    'l' },
    { "trace",          required_argument,  nullptr,    'T' },
    { "debug",          required_argument,  nullptr,    'd' },
    { "status",         no_argument,        nullptr,    's' },
    {},
    // clang-format on
};

//
// Print usage message.
//
static void print_usage(std::ostream &out, const char *prog_name)
{
    out << "BESM-6 Simulator (b6sim), Version " << Session::get_version() << "\n";
    out << "Usage:" << std::endl;
    out << "    " << prog_name << " [options...] program" << std::endl;
    out << "Input files:" << std::endl;
    out << "    program                 BESM-6 a.out executable to run" << std::endl;
    out << "Options:" << std::endl;
    out << "    -h, --help              Display available options" << std::endl;
    out << "    -V, --version           Print the version number and exit" << std::endl;
    out << "    -v, --verbose           Verbose mode" << std::endl;
    out << "    -l NUM, --limit=NUM     Stop after so many instructions (default "
        << Session::get_default_limit() << ")" << std::endl;
    out << "    --trace=FILE            Redirect trace to the file" << std::endl;
    out << "    -d MODE, --debug=MODE   Select debug mode, default irm" << std::endl;
    out << "    -s, --status            Print program exit status as a signed integer"
        << std::endl;
    out << "Debug modes:" << std::endl;
    out << "    i       Trace instructions" << std::endl;
    out << "    e       Trace extracodes (syscalls)" << std::endl;
    out << "    f       Trace fetch" << std::endl;
    out << "    r       Trace registers" << std::endl;
    out << "    m       Trace memory read/write" << std::endl;
}

//
// Main routine of the simulator,
// when invoked from a command line.
//
int main(int argc, char *argv[])
{
    // Get the program name.
    const char *prog_name = strrchr(argv[0], '/');
    if (prog_name == nullptr) {
        prog_name = argv[0];
    } else {
        prog_name++;
    }
    // Publish it for the diagnostic sinks in machine.cpp/session.cpp.
    sim_progname = prog_name;

    // Instantiate the session.
    // Enable wall clock by default.
    Session session;

    // Parse command line options.
    for (;;) {
        switch (getopt_long(argc, argv, "-hVvl:tT:d:sr", long_options, nullptr)) {
        case EOF:
            break;

        case 0:
            continue;

        case 1:
            // Regular argument: the a.out program to run.
            if (session.get_program_file().empty()) {
                session.set_program_file(optarg);
            } else {
                std::cerr << prog_name << ": error: too many arguments: " << optarg << std::endl;
                print_usage(std::cout, prog_name);
                exit(EXIT_FAILURE);
            }
            continue;

        case 'h':
            // Show usage message and exit.
            print_usage(std::cout, prog_name);
            exit(EXIT_SUCCESS);

        case 'v':
            // Verbose.
            session.set_verbose(true);
            continue;

        case 'V':
            // Show version and exit.
            std::cout << "Version " << Session::get_version() << "\n";
            exit(EXIT_SUCCESS);

        case 'l':
            // Limit the cycle count.
            try {
                session.set_limit(std::stoull(optarg));
            } catch (...) {
                std::cerr << prog_name << ": error: bad --limit option: " << optarg << std::endl;
                print_usage(std::cout, prog_name);
                exit(EXIT_FAILURE);
            }
            continue;

        case 't':
            // Enable tracing of extracodes, to stdout by default.
            session.enable_trace("e");
            continue;

        case 'T':
            // Redirect tracing to a file.
            session.set_trace_file(optarg, "irm");
            continue;

        case 'd':
            // Set trace options.
            session.enable_trace(optarg);
            continue;

        case 's':
            // Print the program exit status as a signed integer.
            session.set_report_status(true);
            continue;

        default:
            print_usage(std::cout, prog_name);
            exit(EXIT_FAILURE);
        }
        break;
    }

    // Must specify a program to run.
    if (session.get_program_file().empty()) {
        print_usage(std::cout, prog_name);
        exit(EXIT_FAILURE);
    }

    // Simulate the last session.
    session.run();
    session.finish();

    return session.get_exit_status();
}
