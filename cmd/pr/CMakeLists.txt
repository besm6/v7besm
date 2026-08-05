# /bin/pr -- Unix v7 pr(1), built natively for the BESM-6.
#
# Task C5f (../TODO.md), the eighteenth of the text filters and the last of the four
# "deciding" programs ../README.md §2 has been counting byte cursors in.  pr.c's header is
# the account: the circular buffer up to 72 read cursors share, held as int offsets rather
# than as `char *' because every one of those comparisons runs once per byte; the two
# sentinel bytes 0375 and 0376 that live IN the character stream, which is §11's worst shape
# and which survives here only because neither byte can occur in valid UTF-8; the refill
# invariant nothing in v7 checked; and the terminal chmod that stays because write(1) is task
# C6 and still gives it a producer.
#
# ./README.md is the long form.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over pr.c, links it
# `crt0.o pr.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/pr.  It also registers rootfs_pr_size, the §6 measurement -- of which the
# 1,120 words of ring buffer are the largest single object of task C5f's seven.
#
# pr.1.umm is the manual page; nothing installs it and there is no renderer yet -- read it as
# it stands.
#
# NOT SETUID: it opens what the caller could open itself, and chmods only its own terminal.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(pr DEST bin/pr SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/pr.c)

add_subdirectory(test)
