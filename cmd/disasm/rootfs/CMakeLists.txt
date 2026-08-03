# /usr/bin/disasm -- cmd/disasm built a SECOND time, natively for the BESM-6 (task C9c).
#
# The sources are ../main.c and ../dis.c plus the cmd/libaout files the
# disassembler calls, the very ones the host tool b6disasm is built from.  There is
# no copy and no fork, and no size profile: dis.c holds one 64-byte line buffer and
# the two mnemonic tables, and streams the rest.  ../README.md has the account, and
# ../test/'s GoogleTest suite covers the HOST build only.
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# tool is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard that includes scripts/BesmCross.cmake -- it has to be, since BesmCross
# resolves the cross toolchain out of those very targets.  So b6_prog() does not
# exist yet at that point, and CMake will not take one directory twice.  This one
# is added from inside the guard, after lib/, exactly as cmd/ld/rootfs is.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the
# header dependency is the whole tree at once, as it is in lib/ and cmd/ld/rootfs.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and the headers that glob does not reach: disasm's own, and the cross header
# that defines the object format both halves read.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../disasm.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

# The libaout sources, named by cmd/libaout/sources.cmake so this list and the
# host library's cannot drift apart.  B6_LIBAOUT_SOURCES_NATIVE is the subset
# without the four file-descriptor routines, which only ar/ranlib call.
include(${CMAKE_SOURCE_DIR}/cmd/libaout/sources.cmake)

# -I.. for "disasm.h", -Icross for "besm6/b.out.h".  Both must be absolute: CFLAGS
# goes onto the b6cc command line as written, and the compiler does not run in
# this directory.
b6_prog(disasm DEST usr/bin/disasm
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/.. -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../dis.c
                ${B6_LIBAOUT_SOURCES_NATIVE})

add_subdirectory(test)
