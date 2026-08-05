//
// b6man2umm.  The whole driver is run() in run.cpp, which lives in the engine
// library so the GoogleTest binary can drive it too -- cmd/nm's split, for cmd/nm's
// reason.
//
#include "man2umm.h"

int main(int argc, char **argv)
{
    return umm::cli(argc, argv);
}
