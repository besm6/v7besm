Tasks to do in assembler:

 * AR-10: copyfil() in cmd/ar/ar.c pads archive member *data* to an even byte count (the IODD/OODD flags, a PDP-11 2-byte-word legacy) instead of to a 6-byte BESM-6 word. The header is now fully word-aligned (ar_name[30], ARHDRSZ=60) and ld advances by `archdr.ar_size + ARHDRSZ`, so member data must also be padded to a whole 6-byte word for member offsets to stay word-aligned. Replace the even-byte padding in copyfil() (and the matching size accounting) with rounding to a multiple of W=6.

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

 * AR-11: wire cmd/ar into the top-level CMake and build b6ar; fix warnings/errors. Split
   into an `ar` library + main.c and add cmd/ar/test/ar_test.cpp. (Related: the AR-10
   padding fix in cmd/ar/ar.c.)

 * NM-1: wire cmd/nm into the top-level CMake and build b6nm; fix warnings/errors. Split
   into an `nm` library + main.c and add cmd/nm/test/nm_test.cpp.

 * RANLIB-1: wire cmd/ranlib into the top-level CMake and build b6ranlib; fix
   warnings/errors. Split into a `ranlib` library + main.c and add cmd/ranlib/test/ranlib_test.cpp.

 * SIZE-1: wire cmd/size into the top-level CMake and build b6size; fix warnings/errors.
   Split into a `size` library + main.c and add cmd/size/test/size_test.cpp.

 * STRIP-1: wire cmd/strip into the top-level CMake and build b6strip; fix warnings/errors.
   Split into a `strip` library + main.c and add cmd/strip/test/strip_test.cpp.

 * LORDER-1: wire cmd/lorder (install-only shell script) into the top-level CMake.
