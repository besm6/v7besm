# /bin/file -- Unix v7 file(1), built natively for the BESM-6.
#
# Task C5f (../TODO.md), and the one program of the seven that ../TODO.md says gets a
# DELIBERATE change rather than a faithful port.  ./README.md is the account and file.c's
# header the summary: this machine's magic numbers instead of the PDP-11's, RELFLG telling a
# linked executable from a relocatable object, b6as's directives and Madlen mnemonics in
# place of PDP-11 ones, and -- the divergence -- well-formed UTF-8 taken as TEXT, where v7
# calls every byte above 0177 "data" and so reports this image's own Russian prose as binary.
#
# WHERE THE CONSTANTS COME FROM.  FMAGIC, NMAGIC and BADDR are <sys/param.h>'s, which is the
# kernel's documented duplicate of cross/besm6/b.out.h -- a guest program cannot reach the
# host cross tree.  ARMAG is written out in file.c with its source named, there being no
# guest-side copy of cross/besm6/ar.h at all.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over file.c, links it
# `crt0.o file.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/file.  It also registers rootfs_file_size.
#
# file.1.umm is the manual page; manview(1) formats it on the image, and it is
# legible as it stands.  test/page.1 is NOT one: it is a three-line fixture for the roff detector.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(file DEST bin/file SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/file.c)

add_subdirectory(test)
