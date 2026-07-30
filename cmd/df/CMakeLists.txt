# /bin/df -- Unix v7 df(1), built natively for the BESM-6.
#
# The first of task C4a's three (../TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over df.c, links it
# `crt0.o df.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/df.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID, and it must not become one.  df reads /dev/rmd0, which is mode 0600 because
# that one node is every file's contents -- a setuid df would hand the whole filesystem to
# anybody who could think of an octal offset.  It is a root-only program here, df.1m says
# so, and ../README.md SS8 is the general rule.
#
# THIS IS THE PROGRAM THAT SETTLED HOW A USER PROCESS READS THE RAW DISK, which is why it
# is the one of the three that carries a README.md: physio() and mdstrategy() between them
# impose four alignment rules on a raw transfer, three of which fail with EFAULT and the
# fourth silently.  quot(1) obeys the same four and cites this account; C4b-C4e will too.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(df DEST bin/df SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/df.c)

add_subdirectory(test)
