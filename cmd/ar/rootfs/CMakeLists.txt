# /usr/bin/ar -- cmd/ar built a SECOND time, natively for the BESM-6 (task C9d).
#
# The sources are ../*.c plus the cmd/libaout files the archiver calls, the very
# ones the host tool b6ar is built from.  There is no copy and no fork -- and here
# there is not even a size profile: NOTHING in these sources keys off the `besm6'
# macro, which makes ar the third of the nine (after size and disasm) that the
# address-space ceilings did not touch.  struct arstate is ~190 words against a
# 4,096-word limit, so the "lift the big arrays out of the struct" move that
# dominated C9a and C9b has no work to do.  ../README.md has the account, and
# ../test/'s GoogleTest suite covers the HOST build only.
#
# THIS IS THE FIRST NATIVE PROGRAM TO WANT THE FULL LIBAOUT LIST.  getarhdr,
# getint, putarhdr and putint -- the four file-descriptor routines that are
# exactly the difference between B6_LIBAOUT_SOURCES and B6_LIBAOUT_SOURCES_NATIVE
# -- exist for ar and ranlib and for nothing else; the other seven native tools
# read and write through the FILE* flavour.  cmd/libaout/sources.cmake says so.
#
# WHAT DID CHANGE IS IN LIBC, not here: mkstemp(), which ar calls three times and
# this libc had not got.  as, ld and strip all escaped it by moving to tmpfile(),
# because only the stream was ever wanted there; ar wants the NAME -- finish()
# unlinks it -- and a descriptor it can read back, so tmpfile() cannot serve.
# lib/libc/gen/mkstemp.c and ../README.md have the decision.
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# tool is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard that includes scripts/BesmCross.cmake -- it has to be, since BesmCross
# resolves the cross toolchain out of those very targets.  So b6_prog() does not
# exist yet at that point, and CMake will not take one directory twice.  This one
# is added from inside the guard, after lib/, exactly as cmd/ld/rootfs is.
#
# NOTE THE BOOTSTRAP: the tools BesmCross resolves are the in-tree b6as and b6ld,
# so this program is assembled and linked by the host build of its own sources.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the
# header dependency is the whole tree at once, as it is in lib/ and cmd/ld/rootfs.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and the headers that glob does not reach: ar's own pair, and the cross
# headers that define the archive and object formats both halves read and write.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../intern.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/../archive.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/ar.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

# The libaout sources, named by cmd/libaout/sources.cmake so this list and the
# host library's cannot drift apart.
include(${CMAKE_SOURCE_DIR}/cmd/libaout/sources.cmake)

# -I.. for "intern.h", -Icross for "besm6/ar.h".  Both must be absolute: CFLAGS
# goes onto the b6cc command line as written, and the compiler does not run in
# this directory.
b6_prog(ar DEST usr/bin/ar
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/.. -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../ar.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../command.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../archive.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../list.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../util.c
                ${B6_LIBAOUT_SOURCES})

add_subdirectory(test)
