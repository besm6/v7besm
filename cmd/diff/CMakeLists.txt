# /bin/diff -- Unix v7 diff(1), built natively for the BESM-6.
#
# Task C5f (../README.md).  diff.c's header is the account: the line hash, which was a
# 1's-complement sum in 16-bit hunks of a 32-bit long and had no mask of its own, so a long
# line overflowed a 41-bit word here; readhash() answering 0 for BOTH end of file and a
# legitimate hash; a shellsort that forms a pointer below its own array and detects it with a
# comparison that need not fire on a word-address machine; `char c, d' against a getc() that
# answers EOF; isspace() over a raw file byte at six call sites; and mktemp() writing into a
# string literal.
#
# ../diffh is the other half and is NOT optional: `files too big, try -h' is what diff says
# when its vectors do not fit, and on a 28,672-word address space (../README.md §6) that is
# nearer than it was on a PDP-11.  diff -h execv's /usr/lib/diffh, so the path in
# ../../root.manifest and the one in diff.c are the same string and have to stay so.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over diff.c, links it
# `crt0.o diff.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/diff.  It also registers rootfs_diff_size -- which cannot see the six
# integer vectors this program allocates over its input, §6's fourth ceiling.
#
# diff.1.umm is the manual page, and it covers diffh too; manview(1) formats it on the image
# (../README.md C25a), and it is legible as it stands.
#
# NOT SETUID: it opens what the caller could open itself, and writes its temp file as the
# caller too.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(diff DEST bin/diff SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/diff.c)

add_subdirectory(test)
