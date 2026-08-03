# /usr/bin/as -- cmd/as built a SECOND time, natively for the BESM-6 (task C9b).
#
# The sources are ../*.c plus the cmd/libaout files the assembler calls, the very
# ones the host tool b6as is built from.  There is no copy and no fork: the only
# thing the target changes is the size profile in ../as.h, which keys off the
# `besm6' macro b6cpp always predefines.  ../README.md has the account, and
# ../test/'s GoogleTest suite covers the HOST build only.
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# tool is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard that includes scripts/BesmCross.cmake -- it has to be, since BesmCross
# resolves the cross toolchain out of those very targets.  So b6_prog() does not
# exist yet at that point, and CMake will not take one directory twice.  This one
# is added from inside the guard, after lib/, exactly as cmd/cpp/rootfs is.
#
# NOTE THE BOOTSTRAP: the tools BesmCross resolves are the in-tree b6as and b6ld,
# so this program is assembled and linked by the host build of its own sources.
# A mistake in the shared code shows up as a miscompile of everything on the
# image, not as a failure here.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the
# header dependency is the whole tree at once, as it is in lib/ and cmd/cpp/rootfs.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and the headers that glob does not reach: as.h is where the whole size
# profile lives, so leaving it out of the dependency would be the worst blind
# spot here, and the cross headers define the object format both halves share.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../as.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/../assemble.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

# The libaout sources, named by cmd/libaout/sources.cmake so this list and the
# host library's cannot drift apart.  B6_LIBAOUT_SOURCES_NATIVE is the subset
# without the four file-descriptor routines, which only ar/ranlib call.
include(${CMAKE_SOURCE_DIR}/cmd/libaout/sources.cmake)

# -I.. for "as.h", -Icross for "besm6/b.out.h".  Both must be absolute: CFLAGS
# goes onto the b6cc command line as written, and the compiler does not run in
# this directory.
b6_prog(as DEST usr/bin/as
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/.. -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../as.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../tables.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../lex.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../symtab.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../expr.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../pass1.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../pass2.c
                ${B6_LIBAOUT_SOURCES_NATIVE})

add_subdirectory(test)
