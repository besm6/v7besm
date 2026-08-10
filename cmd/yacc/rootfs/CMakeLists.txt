#
# Two things live here.
#
# /usr/bin/yacc and /usr/lib/yaccpar -- cmd/yacc built a SECOND time, natively for
# the BESM-6 (task C10c, ../../TODO.md).  The sources are the host tool's own; the
# only thing that differs is the size profile in ../dextern.h keyed on `besm6'.
# ../README.md, "Building for the BESM-6".
#
# calct -- task C10a's native assertion, and NOT a program on the image: the one
# thing here that runs generated parser code.  ../README.md, "The native case".
#
# A subdirectory of its own because cmd/yacc is added outside the libruntime
# guard, where b6_prog() and b6_yacc() do not exist -- as cmd/libaout/rootfs is.
#

# b6_obj reads both from the caller's scope.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and yacc's own header, which the glob does not reach and b6cc has no -M for.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../dextern.h)

# -I.. for "dextern.h", absolute: b6cc does not run in this directory.
b6_prog(yacc DEST usr/bin/yacc
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../y1.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../y2.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../y3.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../y4.c)

# The parser skeleton, under the name find_parser()'s `besm6' arm looks for.  Data
# to yacc, so copied and not compiled.  A BUILD step rather than configure_file(),
# for the reason cmd/lorder/rootfs gives: `rm -rf build/rootfs' must restore it.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/yaccpar
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../yaccpar.c ${B6_ROOTFS}/usr/lib/yaccpar
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../yaccpar.c
    COMMENT "stage usr/lib/yaccpar" VERBATIM)

add_custom_target(rootfs_yaccpar ALL DEPENDS ${B6_ROOTFS}/usr/lib/yaccpar)
add_dependencies(rootfs rootfs_yaccpar)

b6_yacc(CALCT_SRC calct ${CMAKE_CURRENT_SOURCE_DIR}/calct.y)
b6_prog(calct DEST test/calct SOURCES ${CALCT_SRC})

b6_progtest(calct arith    DEST test/calct)
b6_progtest(calct prec     DEST test/calct)
b6_progtest(calct recover  DEST test/calct)
b6_progtest(calct eightbit DEST test/calct)

add_subdirectory(test)
