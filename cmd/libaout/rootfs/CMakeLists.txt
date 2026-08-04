# aoutt -- cmd/libaout's word path, exercised ON THE BESM-6.
#
# WHY A NATIVE TEST AT ALL.  ../test/ is GoogleTest and builds for the HOST, where
# the `besm6' macro is not defined and the whole of ../fastio.h compiles to nothing.
# It remains the authority on the on-disk byte layout of the fallback path, but it
# cannot see the branch fgetw()/fputw() take when the stdio cursor happens to sit on
# a word boundary -- and that branch is the one that reads a 48-bit word out of the
# buffer with a single load instead of six fat-pointer byte extractions.  Nothing
# else in the daily suite covers it: the weekly `toolchain' test does, byte for
# byte, but it is opt-in and is not a commit gate.
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# library is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard, so b6_prog() does not exist yet at that point and CMake will not take one
# directory twice.  ../../as/rootfs/CMakeLists.txt says it at length; this is the
# same shape.
#
# WHY NOT lib/test/.  b6_libtest() compiles exactly one source, ${prog}.c, and there
# is no native libaout archive to link against -- each cmd/*/rootfs names
# B6_LIBAOUT_SOURCES itself.  b6_prog() takes SOURCES, so this needs no change to
# scripts/BesmCross.cmake and no root.manifest, progs.cmake or libtest.sh entry.
#
# THE PROGRAM IS STAGED UNDER build/rootfs/test/ AND DOES NOT REACH THE DISK, for
# the reason cmd/novi/test/CMakeLists.txt gives: root.manifest names every file on
# the image explicitly, so a binary staged outside the paths it names is built,
# size-checked and run, and nothing more.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is everywhere else.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and the headers that glob does not reach: fastio.h is the whole subject of this
# test, and the cross headers define the object format and word_t/uword_t.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../fastio.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

# The libaout sources, named by ../sources.cmake so this list and the host library's
# cannot drift apart.  The _NATIVE subset: the four file-descriptor routines are
# ar/ranlib's alone, and b6_prog() links every object it is handed.
include(${CMAKE_CURRENT_SOURCE_DIR}/../sources.cmake)

# -I${CMAKE_SOURCE_DIR}/cross must be ABSOLUTE: CFLAGS goes onto the b6cc command
# line as written, and the compiler does not run in this directory.
b6_prog(aoutt DEST test/aoutt
        CFLAGS -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/aoutt.c ${B6_LIBAOUT_SOURCES_NATIVE})

# The case name has to differ from the program name: b6_progtest() takes both the
# expectation and the .out it writes from the case name.
b6_progtest(aoutt words DEST test/aoutt)
