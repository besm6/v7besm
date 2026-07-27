# /bin/rmdir -- Unix v7 rmdir(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over rmdir.c, links it `crt0.o rmdir.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/rmdir -- the /bin/rmdir of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# THE SETUID BIT IS NOT SET HERE; ../../root.manifest is where this program's `mode 04755'
# lives, as it is for mkdir.  See ../mkdir/README.md.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(rmdir DEST bin/rmdir SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/rmdir.c)
