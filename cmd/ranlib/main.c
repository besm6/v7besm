/*
 * Command-line front end for BESM-6 ranlib.
 *
 * All the work lives in the reusable engine (cmd/ranlib/ranlib.c); this just
 * hands the argument vector to ranlib_run() and forwards its exit code.
 */
#include "symdef.h"

int main(int argc, char **argv)
{
    return ranlib_run(argc, argv);
}
