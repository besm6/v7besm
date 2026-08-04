# /usr/bin/ranlib -- cmd/ranlib built a SECOND time, natively for the BESM-6
# (task C9d), beside /usr/bin/ar.
#
# IT CARRIES THE WHOLE ARCHIVER.  ranlib_run() rebuilds the archive in process
# through ar_run() (../ranlib.c), so this program's SOURCES list is ranlib's two
# files plus ALL FIVE of cmd/ar's engine files -- exactly what the host b6ranlib
# links when it links the `archiver' library, written out because b6_prog() takes
# a flat source list and there is no native archive to link against.  Note
# cmd/ar/main.c is NOT in the list: it has a main() of its own.
#
# So there are two copies of the archiver on the disk, and that was the choice.
# An exec of /usr/bin/ar would save one -- about 26 blocks of the 830 the image
# has free -- at the price of a hard-coded absolute path this program does not
# have today, a fork per archive where the host build does the work in process,
# and an `#if besm6' around the two ar_run() calls: a fork in the sources, which
# is what every one of C9a-C9c was arranged to avoid.  ../README.md says so.
#
# The libaout list is the FULL one for the same reason cmd/ar/rootfs's is: the
# four file-descriptor routines getarhdr/getint/putarhdr/putint exist for these
# two programs and no others.
#
# The one number the target changes is TABSZ in ../ranlib.c, and it is not an
# address-space cut: it matches b6ld's own RANTABSZ, so that this ranlib cannot
# write an index the machine's own linker would refuse to read.  ../README.md has
# the measurements, and ../test/'s GoogleTest suite covers the HOST build only.
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

# ...and the headers that glob does not reach: ranlib's own, cmd/ar's pair (this
# program compiles all of ar), and the cross headers for the three on-disk
# formats it touches -- the archive, the object, and the __.SYMDEF entry.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../symdef.h
                  ${CMAKE_SOURCE_DIR}/cmd/ar/intern.h
                  ${CMAKE_SOURCE_DIR}/cmd/ar/archive.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/ar.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/ranlib.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

# The libaout sources, named by cmd/libaout/sources.cmake so this list and the
# host library's cannot drift apart.
include(${CMAKE_SOURCE_DIR}/cmd/libaout/sources.cmake)

# Three include directories rather than the usual two: -I.. for "symdef.h",
# -Icross for "besm6/ranlib.h", and -Icmd/ar for "archive.h" -- ar_run()'s
# declaration, which is what the host build gets from linking the `archiver'
# target.  All absolute: CFLAGS goes onto the b6cc command line as written, and
# the compiler does not run in this directory.
b6_prog(ranlib DEST usr/bin/ranlib
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
               -I${CMAKE_SOURCE_DIR}/cmd/ar
               -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../ranlib.c
                ${CMAKE_SOURCE_DIR}/cmd/ar/ar.c
                ${CMAKE_SOURCE_DIR}/cmd/ar/command.c
                ${CMAKE_SOURCE_DIR}/cmd/ar/archive.c
                ${CMAKE_SOURCE_DIR}/cmd/ar/list.c
                ${CMAKE_SOURCE_DIR}/cmd/ar/util.c
                ${B6_LIBAOUT_SOURCES})

add_subdirectory(test)
