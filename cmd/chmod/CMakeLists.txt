# /bin/chmod -- Unix v7 chmod(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over chmod.c, links it `crt0.o chmod.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/chmod -- the /bin/chmod of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID, and it does not need to be: chmod(2) is gated on owner() rather than suser()
# (kernel/sys4.c), so the caller's own files are its own business.  ../mkdir/README.md is the
# account of the three programs on this image that ARE setuid, and why.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(chmod DEST bin/chmod SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/chmod.c)
