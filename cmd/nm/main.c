//
// Command-line front end for BESM-6 nm.
//
// All the work lives in the reusable engine (cmd/nm/nm.c); this just hands the
// argument vector to nm_run() and forwards its exit code.
//
#include "nm.h"

int main(int argc, char **argv)
{
    return nm_run(argc, argv);
}
