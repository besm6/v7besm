# /bin/comm -- Unix v7 comm(1), built natively for the BESM-6.
#
# One of task C5b's seven (../TODO.md).  comm.c's header is the account: the three leading-tab
# strings v7 shortened by writing into string literals, which land in the CONST segment on
# this machine, and the collating order that stopped being v7's without a line changing --
# `char' is unsigned here, so a byte above 0177 sorts after ASCII and not before it.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over comm.c, links it
# `crt0.o comm.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/comm.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(comm DEST bin/comm SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/comm.c)

add_subdirectory(test)
