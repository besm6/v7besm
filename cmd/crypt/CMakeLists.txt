# /bin/crypt -- Unix v7 crypt(1), the one-rotor machine.  Task C19 (../README.md).
# ./README.md is the account.
#
# rotor.c IS SHARED WITH ../ed, which links it for -x: ../df and ../umount share
# ../mount/mtab.c the same way and for the same reason.  b6_prog() puts every object in
# ${name}.dir, so the two builds of it cannot collide.
#
# THE §1 BUILD BLIND SPOT APPLIES: b6_obj's header dependency is the SYSTEM tree (KHDRS),
# so editing rotor.h rebuilds nothing.  Touch crypt.c and ../ed/ed.c after changing it.
#
# crypt.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and
# ../../scripts/root.manifest has carried its stanza since before the program did.
#
# NOT PURE: nobody runs two of these at once.  NOT SETUID (../README.md §8): it reads its
# own standard input.  NO LIBS: crypt(3) and getpass(3) are in libc.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(crypt DEST bin/crypt
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crypt.c ${CMAKE_CURRENT_SOURCE_DIR}/rotor.c)

add_subdirectory(test)
