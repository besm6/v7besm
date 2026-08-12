# /bin/look -- Unix v7 look(1), built natively for the BESM-6, and /usr/dict/words with it.
#
# One of task C5b's seven (../README.md).  look.c's header is the account: the two-argument
# puts() that does not compile here, the three unbounded writes §6 asks every port to bound,
# and the rule this port takes for -d and -f over a byte above 0177.
#
# THE DICTIONARY IS STAGED HERE, which is what makes the bare `look word' form mean anything
# on the image: v7's /usr/dict/words is not in this tree (../README.md says so of spell(1)'s
# word list), so `words' beside this file is a small sorted one written for the purpose.  It
# is staged rather than named in ../../scripts/root.manifest from the source directory, which is
# etc/CMakeLists.txt's rule and its reason: one place the contents of the image come from, so
# that `ls build/rootfs' shows what the image will hold.
#
# SORTED IN BYTE ORDER, which is the order look compares in -- `char' is unsigned here, so
# every Cyrillic entry follows every ASCII one.  `LC_ALL=C sort -c words' is the check, and it
# is the same order comm(1) and the shell's filename generation use.
#
# b6_prog() (../../scripts/BesmCross.cmake) runs the b6* toolchain over look.c, links it
# `crt0.o look.o -lc -lruntime' against the libc built in ../../lib, and stages the result as
# build/rootfs/bin/look.
#
# NOT SETUID: it opens what the caller could open itself.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(look DEST bin/look SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/look.c)

# The dictionary, staged into ${B6_ROOTFS} exactly as etc/ stages /etc/passwd and /etc/motd.
# copy_if_different, so an unchanged list does not re-trigger the whole image build.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/dict/words
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/dict
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/words ${B6_ROOTFS}/usr/dict/words
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/words
    COMMENT "stage cmd/look/words" VERBATIM)
add_custom_target(rootfs_dict ALL DEPENDS ${B6_ROOTFS}/usr/dict/words)
add_dependencies(rootfs rootfs_dict)

add_subdirectory(test)
