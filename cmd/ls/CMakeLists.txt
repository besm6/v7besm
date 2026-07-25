# /bin/ls -- Unix v7 ls(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over ls.c, links it `crt0.o ls.o -lc -lruntime' against the libc built in
# ../../lib, and stages the result as build/rootfs/bin/ls -- the /bin/ls of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# This is the one of the four that pulls in most of libc: stdio and doprnt, malloc, qsort,
# ctime and strcmp.  Its size check (rootfs_ls_size, ctest label `rootfs') is therefore the
# one worth watching as the header tree grows.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(ls DEST bin/ls SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ls.c)
