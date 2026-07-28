# /bin/chown -- Unix v7 chown(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over chown.c, links it `crt0.o chown.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/chown -- the /bin/chown of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID, deliberately: chown(2) opens with `if (!suser() ...)' in kernel/sys4.c, so only
# the super-user can change an owner at all -- which is v7's rule and chown.1's own words.  A
# setuid chown would hand every user the ability to give a file away, which is the thing that
# rule exists to prevent.  ../mkdir/README.md is the account of the three that ARE setuid.
#
# It reads /etc/passwd through getpwnam(3), which ../../etc/ stages onto the image.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(chown DEST bin/chown SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/chown.c)
