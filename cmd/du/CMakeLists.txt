# /bin/du -- Unix v7 du(1), built natively for the BESM-6.
#
# The second of task C4a's three (../README.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over du.c, links it
# `crt0.o du.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/du.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID, and the odd one of C4a's three in needing no privilege at all: du opens no
# device.  It stat(2)s and reads directories as its caller, so it reports what its caller
# can see, which is the whole of what it should do.  ../README.md SS8 is the rule.
#
# THE ONE OF THE THREE THAT CANNOT BE TESTED UNDER b6sim, for ../ls/README.md's reason: it
# reads a directory, and a host directory descriptor refuses that.  opendir(3) does not rescue
# it and makes it worse -- every directory reads as EMPTY rather than failing.  test/ holds a
# CMakeLists.txt saying so at length and registering nothing; ../time/test/ is the precedent
# for a test directory whose content is an explanation.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(du DEST bin/du SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/du.c)

add_subdirectory(test)
