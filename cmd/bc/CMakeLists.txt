# bc(1) -- task C16 (../TODO.md), the calculator language, and the front end /bin/dc
# exists for: it compiles this language into dc commands and execs dc to run them.
# C15 put the engine on the image; README.md beside this file is the account.
#
# ../expr/CMakeLists.txt explains b6_yacc()'s two directories; the same applies here,
# and bc.y likewise includes no y.tab.h, so there is no CFLAGS -I either.
#
# NO HEADER OF THE PROGRAM'S OWN, so ../README.md SS1's build blind spot never arises;
# bc.y is a DEPENDS of b6_yacc's custom command and editing it regenerates.
#
# NOT PURE: nothing runs two calculators at once.  NOT SETUID: bc opens the file the
# caller named and execs /bin/dc as the caller (../README.md SS8) -- ../dc's reasoning.
#
# rootfs_bc_size is the ../README.md SS6 check; the stack ceiling it cannot make is why
# routput() walks a bundle over an explicit stack instead of recursing.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_yacc(BC_SRC bc ${CMAKE_CURRENT_SOURCE_DIR}/bc.y)
b6_prog(bc DEST bin/bc SOURCES ${BC_SRC})

# The -l math library, which is a bc program and not a C one: data, so copied and not
# compiled, on the same footing as ../yacc/rootfs's /usr/lib/yaccpar.  A BUILD step
# rather than configure_file(), so that `rm -rf build/rootfs' restores it.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/lib.b
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/lib.b ${B6_ROOTFS}/usr/lib/lib.b
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib.b
    COMMENT "stage usr/lib/lib.b" VERBATIM)

add_custom_target(rootfs_libb ALL DEPENDS ${B6_ROOTFS}/usr/lib/lib.b)
add_dependencies(rootfs rootfs_libb)

add_subdirectory(test)
