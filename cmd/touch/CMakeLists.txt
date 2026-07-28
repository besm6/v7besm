# /bin/touch -- Unix v7 touch(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over touch.c, links it `crt0.o touch.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/touch -- the /bin/touch of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID: utime(2) is gated on owner() (kernel/sys4.c), so a caller's own files are its
# own business, and creat(2) needs nothing but a writable directory.
#
# THIS ONE DIVERGES FROM v7 IN WHAT IT DOES, not merely in how it is written -- it calls
# utime(2) where v7 rewrote the file's first byte.  touch.c's header is the account and
# touch.1 was rewritten to match.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(touch DEST bin/touch SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/touch.c)
