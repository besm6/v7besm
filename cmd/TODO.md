Tasks to do in assembler:

The linker and its six companion binutils have been split out of cmd/ld into their own
directories (cmd/ld keeps ld.c; cmd/ar, cmd/nm, cmd/ranlib, cmd/size, cmd/strip, cmd/lorder
hold the rest), each with a CMakeLists.txt. None are yet wired into the top-level
CMakeLists.txt. Build them one by one — each task adds the tool's
`add_subdirectory(cmd/<tool>)` line to the top-level CMakeLists.txt, builds it via the
top-level `make`, and fixes any warnings/errors (all built with -Wall -Werror -Wshadow).

Each C tool task also adds a unit test, modelled on cmd/disasm/test/disasm_test.cpp: split
the tool's engine into a reusable static library plus a thin main.c front end (mirroring
cmd/as = assembler lib + main.c, and cmd/disasm = disassembler lib + main.c), then add a
test/ subdirectory whose `<tool>_test.cpp` links that library with GTest::gtest_main, is
registered with `gtest_discover_tests`, and is hooked into the aggregate build via
`add_dependencies(build_tests <tool>_test)`. lorder is an install-only shell script, so it
gets no library split and no unit test.

 * SIZE-1: wire cmd/size into the top-level CMake and build b6size; fix warnings/errors.
   Split into a `size` library + main.c and add cmd/size/test/size_test.cpp.

 * STRIP-1: wire cmd/strip into the top-level CMake and build b6strip; fix warnings/errors.
   Split into a `strip` library + main.c and add cmd/strip/test/strip_test.cpp.

 * LORDER-1: wire cmd/lorder (install-only shell script) into the top-level CMake.
