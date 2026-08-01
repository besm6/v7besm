# /bin/tail -- Unix v7 tail(1), built natively for the BESM-6.
#
# One of task C5b's seven (../TODO.md).  tail.c's header is the account: the `int errno;' that
# had to be deleted rather than renamed, the `-b' unit that ../TODO.md left to this task and
# which is BSIZE here rather than the PDP-11's 512, and the argument shuffle that formed a
# pointer before the start of argv.
#
# tail links NO stdio -- it is read(2) and write(2) end to end -- so it belongs with tee, test
# and getty at the bottom of ../README.md §6's size table rather than with the filters above
# it.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over tail.c, links it
# `crt0.o tail.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/tail.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tail DEST bin/tail SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tail.c)

add_subdirectory(test)
