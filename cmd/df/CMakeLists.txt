# /bin/df -- df(1M), built natively for the BESM-6: v7's program with 4.4BSD's report.
#
# The first of task C4a's three (../TODO.md): a cmd/ subdirectory that is NOT a host tool.
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over df.c, links it
# `crt0.o df.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/df.  Added from inside the top-level libruntime guard, after lib/, and
# not with the other cmd/ subdirectories.
#
# NOT SETUID, AND IT NEVER WAS -- AND FOR A MOUNTED FILESYSTEM IT NEEDS NOTHING TO BE.
# /dev/rmd0 is mode 0600 because that one node is every file's contents, and a setuid df
# would hand the whole filesystem to anybody who could think of an octal offset.  That is
# still true, which is why the fix was to stop needing the device rather than to lend it
# out: statfs(2) (<sys/statfs.h>, kernel/sys3.c) answers the four counts for the filesystem
# holding a path, so a bare `df', `df /mnt' and `df /some/file' are ordinary user commands.
# `df /dev/rmd0', `df image.img' and `df -w' still read a descriptor and are still the
# super-user's, with open(2) the whole of the gate.  df.1m says so and ../README.md SS8 is
# the general rule.
#
# THIS IS THE PROGRAM THAT SETTLED HOW A USER PROCESS READS THE RAW DISK, which is why it
# is the one of the three that carries a README.md: physio() and mdstrategy() between them
# impose four alignment rules on a raw transfer, three of which fail with EFAULT and the
# fourth silently.  quot(1) obeys the same four and cites this account; C4b-C4e will too.

# ../mount/mtab.c IS COMPILED HERE A THIRD TIME rather than df growing a parser of its own:
# that file is the one home of the /etc/mtab format, and its header records that v7's four
# copies of one layout across two programs were the bug.  ../umount/CMakeLists.txt is the
# precedent and b6_prog() puts every object in ${name}.dir, so the three builds of it cannot
# collide.  -I is for its header.  What df does NOT inherit is the timing: mtabread() must not
# run before every argument is settled, or a b6sim case reads the build machine's table --
# see df.c's loadtab() and test/CMakeLists.txt.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(df DEST bin/df
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/df.c ${CMAKE_CURRENT_SOURCE_DIR}/../mount/mtab.c
    CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/../mount)

add_subdirectory(test)
