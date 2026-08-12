# /bin/dc -- Unix v7 dc(1), the arbitrary-precision desk calculator.  Task C15 (../README.md),
# and the engine C16's bc(1) drives.  ./README.md is the account.
#
# THE §1 BUILD BLIND SPOT APPLIES HERE, unlike expr and m4, which have no header of their own:
# b6_obj's header dependency is the SYSTEM tree (KHDRS), so editing dc.h rebuilds nothing.
# Touch dc.c after changing it.
#
# dc.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and ../../scripts/root.manifest
# has carried its stanza since before the program did.  This task adds the binary.
#
# NOT PURE: nothing runs two calculators at once.  NOT SETUID: dc opens the file the caller
# named and forks /bin/sh for `!' as the caller (../README.md §8).

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(dc DEST bin/dc SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dc.c)

add_subdirectory(test)
