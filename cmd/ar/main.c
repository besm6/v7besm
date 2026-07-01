// Command-line front end for the BESM-6 archiver.
//
// All the work lives in the reusable engine (cmd/ar/ar.c); this just hands the
// argument vector to ar_run() and forwards its exit code.
#include "archive.h"

int main(int argc, char **argv)
{
    return ar_run(argc, argv);
}
