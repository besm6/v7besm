//
// Command-line front end for BESM-6 size.
//
// All the work lives in the reusable engine (cmd/size/size.c); this just hands
// the argument vector to size_run() and forwards its exit code.
//
#include "size.h"

int main(int argc, char **argv)
{
    return size_run(argc, argv);
}
