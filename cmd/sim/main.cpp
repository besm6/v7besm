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
    { "debug",          no_argument,        nullptr,    'd' },
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
    out << "Usage:\n";
    out << "    " << prog_name << " [options...] program [arguments...]\n";
    out << "Input files:\n";
    out << "    program                 BESM-6 a.out executable to run\n";
    out << "    arguments               passed to the program as argv[1] and up\n";
    out << "Options:\n";
    out << "    -h, --help              Display available options\n";
    out << "    -V, --version           Print the version number and exit\n";
    out << "    -v, --verbose           Verbose mode; also print a periodic progress message\n";
    out << "    -l NUM, --limit=NUM     Stop after so many instructions (default "
        << Session::get_default_limit() << ")\n";
    out << "    --trace=FILE            Redirect trace to the file\n";
    out << "    -d, --debug             Trace instructions, registers and memory\n";
    out << "    -s, --status            Print program exit status as a signed integer\n";
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
    Session session;

    // Parse command line options.  The leading '+' stops option parsing at the
    // first non-option, so everything from the program name onwards belongs to
    // the simulated program: `b6sim -d prog -d' passes the second -d to prog.
    for (;;) {
        switch (getopt_long(argc, argv, "+hVvl:ds", long_options, nullptr)) {
        case EOF:
            break;

        case 0:
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

        case 'T':
            // Redirect tracing to a file.
            session.set_trace_file(optarg);
            continue;

        case 'd':
            // Enable tracing to stdout.
            session.set_debug(true);
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
    if (optind >= argc) {
        print_usage(std::cout, prog_name);
        exit(EXIT_FAILURE);
    }

    // The program file is argv[0] of the simulated program; the rest follow.
    session.set_program_file(argv[optind]);
    for (int i = optind + 1; i < argc; i++) {
        session.add_program_arg(argv[i]);
    }

    // Simulate the last session.
    session.run();
    session.finish();

    return session.get_exit_status();
}
