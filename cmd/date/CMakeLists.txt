# /bin/date -- Unix v7 date(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over date.c, links it `crt0.o date.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/date -- the /bin/date of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID, and it must not become so: stime(2) is gated on suser() (kernel/sys4.c), and
# that gate is the whole reason an ordinary user cannot move the clock.  ../mkdir/README.md
# is the account of the three programs on this image that ARE setuid, and why.
#
# The first program here that changes state every other process can see; date.c's header
# is the account, including the two pieces of arithmetic that had to change.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(date DEST bin/date SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/date.c)

add_subdirectory(test)
