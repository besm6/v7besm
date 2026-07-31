# /bin/split -- Unix v7 split(1), built natively for the BESM-6.
#
# One of task C5a's six (../TODO.md).  b6_prog() (../../scripts/BesmCross.cmake) runs the b6*
# toolchain over split.c, links it `crt0.o split.o -lc -lruntime' against the libc built in
# ../../lib, and stages the result as build/rootfs/bin/split.
#
# NOT SETUID: it creates what the caller could create itself.
#
# Nothing here is this machine's business at all -- no §2, no §3, no §11.  What the port had
# to deal with is three upstream bugs, of which one is the reason split could not have been a
# b6sim case in the shape v7 left it: `split -0' does not terminate, and ../README.md §9 says
# a program that does not terminate cannot be a case.  split.c's header has all three.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(split DEST bin/split SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/split.c)

add_subdirectory(test)
