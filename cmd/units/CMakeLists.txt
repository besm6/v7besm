# units(1) -- task C18 (../TODO.md), and the first program on this image whose whole
# subject is floating point.  README.md beside this file is the account.
#
# LIBS libm for fabs() alone: the range gates in units.c want a magnitude and nothing
# else, so ../awk's -lm is the precedent but not the appetite.
#
# NO HEADER OF THE PROGRAM'S OWN, so ../README.md SS1's build blind spot never arises.
#
# NOT PURE: nobody runs two converters at once.  NOT SETUID: units opens the table the
# caller named, or a world-readable one, and needs no privilege (../README.md SS8).
#
# units.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and
# ../../root.manifest has carried its stanza since before the program did.  This task adds
# the binary and the table.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(units DEST bin/units SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/units.c LIBS libm)

# The table, which is data and not C: copied, on the same footing as ../bc's /usr/lib/lib.b
# and ../look's /usr/dict/words.  A BUILD step rather than configure_file(), so that
# `rm -rf build/rootfs' restores it.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/units
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/units ${B6_ROOTFS}/usr/lib/units
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/units
    COMMENT "stage usr/lib/units" VERBATIM)

add_custom_target(rootfs_units ALL DEPENDS ${B6_ROOTFS}/usr/lib/units)
add_dependencies(rootfs rootfs_units)

add_subdirectory(test)
