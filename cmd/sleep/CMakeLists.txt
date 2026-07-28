# /bin/sleep -- Unix v7 sleep(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over sleep.c, links it `crt0.o sleep.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/sleep -- the /bin/sleep of the
# root filesystem the kernel mounts.  Added from inside the top-level libruntime guard,
# after lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID: alarm(2) and pause(2) act on the calling process and need no privilege.
#
# The program is a decimal parse around libc's sleep(3) (../../lib/libc/gen/sleep.c); what
# it really exercises is signal delivery, which is why kernel/test/utils runs it under the
# booted kernel and not only under b6sim.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(sleep DEST bin/sleep SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sleep.c)

add_subdirectory(test)
