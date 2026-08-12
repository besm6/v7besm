# /bin/time -- Unix v7 time(1), built natively for the BESM-6.
#
# One of task C2b's five (../README.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over time.c, links it
# `crt0.o time.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/time.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID: fork, exec, wait and times(2) act on the caller's own process and children.
#
# The first program on this image that forks, execs and then MEASURES, and the first caller
# of times(2) anywhere -- which is where its one substantial change comes from: v7's
# arithmetic is built on a 60 Hz line clock and this machine's HZ is 250.  time.c's header
# and README.md are the account.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(time DEST bin/time SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/time.c)

add_subdirectory(test)
