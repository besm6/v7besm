#
# Two things live here.
#
# /usr/bin/lex and /usr/lib/lex/ncform -- cmd/lex built a SECOND time, natively
# for the BESM-6 (task C10d, ../../README.md).  The sources are the host tool's
# own; the only thing that differs is the size profile in ../ldefs.h keyed on
# `besm6'.  ../README.md, "Building for the BESM-6".
#
# scant -- task C10b's native assertion, and NOT a program on the image: the one
# thing here that runs generated scanner code.  ../README.md, "The native case".
#
# A subdirectory of its own because cmd/lex is added outside the libruntime
# guard, where b6_prog() and b6_lex() do not exist -- as ../../yacc/rootfs is.
#

# b6_obj reads both from the caller's scope.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and lex's own two headers, which the glob does not reach and b6cc has no -M
# for.  Without this, editing the size profile rebuilds nothing.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../ldefs.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/../once.h)

# The parser, through the in-tree b6yacc.  The host build cannot use b6_yacc()
# and has a custom command of its own; this one is inside the guard.
b6_yacc(LEXPARSE_SRC lexparse ${CMAKE_CURRENT_SOURCE_DIR}/../parser.y)

# -I.. for "ldefs.h", absolute: b6cc does not run in this directory.
b6_prog(lex DEST usr/bin/lex
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../lmain.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../sub1.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../sub2.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../header.c
                ${LEXPARSE_SRC})

# The scanner skeleton, under the name find_form()'s `besm6' arm looks for.  Data
# to lex, so copied and not compiled.  A BUILD step rather than configure_file(),
# for the reason cmd/lorder/rootfs gives: `rm -rf build/rootfs' must restore it.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/lex/ncform
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib/lex
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../ncform ${B6_ROOTFS}/usr/lib/lex/ncform
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../ncform
    COMMENT "stage usr/lib/lex/ncform" VERBATIM)

add_custom_target(rootfs_ncform ALL DEPENDS ${B6_ROOTFS}/usr/lib/lex/ncform)
add_dependencies(rootfs rootfs_ncform)

b6_lex(SCANT_SRC scant ${CMAKE_CURRENT_SOURCE_DIR}/scant.l)
b6_prog(scant DEST test/scant SOURCES ${SCANT_SRC})

b6_progtest(scant tokens   DEST test/scant)
b6_progtest(scant eightbit DEST test/scant)

add_subdirectory(test)
