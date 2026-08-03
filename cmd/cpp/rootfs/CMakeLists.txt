# /usr/bin/cpp -- cmd/cpp built a SECOND time, natively for the BESM-6 (task C9a).
#
# The sources are ../*.c, the very ones the host tool b6cpp is built from one directory up.
# There is no copy and no fork: the only thing the target changes is the size profile in
# ../defs.h, which keys off the `besm6' macro b6cpp itself always predefines.  ../README.md
# has the account, and ../test/'s C11 conformance suite covers the HOST build only.
#
# WHY THIS IS A SUBDIRECTORY rather than four more lines in ../CMakeLists.txt: the host tool
# is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB) guard that
# includes scripts/BesmCross.cmake -- it has to be, since BesmCross resolves the cross
# toolchain out of those very targets.  So b6_prog() does not exist yet at that point, and
# CMake will not take one directory twice.  This one is added from inside the guard, after
# lib/, exactly as cmd/init and the rest of the native programs are.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/ and cmd/init/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# AND cpp's own two headers, which that glob does not reach -- and defs.h is where the whole
# size profile lives, so leaving it out of the dependency would be the worst blind spot here.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/../intern.h)

# -I.. so `#include "intern.h"' resolves to cpp's own header.  It must be absolute: CFLAGS
# goes onto the b6cc command line as written, and the compiler does not run in this directory.
b6_prog(cpp DEST usr/bin/cpp
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../cpp.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../buffer.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../scan.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../direct.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../macro.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../diag.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../parser.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../yylex.c)

add_subdirectory(test)
