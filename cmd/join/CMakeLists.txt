# /bin/join -- Unix v7 join(1), built natively for the BESM-6.
#
# Task C5f (../TODO.md).  join.c's header is the account: NFLD enforced nowhere, so a line of
# more than twenty fields ran out of its own row and into the next; a line with no final
# newline read past itself, which v7 records in a comment and does nothing about; a join
# field past the end of a line handed NULL to strcmp() and to printf("%s"); and file 2 being
# fseek()'d once per group of equal keys, so `-' there was a wrong answer rather than a
# diagnostic.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over join.c, links it
# `crt0.o join.o -lc -lruntime' against the libc built in ../../lib, and stages the result
# as build/rootfs/bin/join.  It also registers rootfs_join_size -- of which `char
# buf[2][BUFSIZ]' is 1,024 words, BUFSIZ being 3072 here and 512 on a PDP-11.
#
# join.1.umm is the manual page; nothing installs it and there is no renderer yet -- read it
# as it stands.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(join DEST bin/join SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/join.c)

add_subdirectory(test)
