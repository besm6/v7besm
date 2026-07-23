//
// Command-line front end for BESM-6 strip.
//
// All the work lives in the reusable engine (cmd/strip/strip.c); this just hands
// the argument vector to strip_run() and forwards its exit code.
//
#include "strip.h"

int main(int argc, char **argv)
{
    return strip_run(argc, argv);
}
