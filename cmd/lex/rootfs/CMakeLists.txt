#
# scant -- task C10b's native assertion, and NOT a program on the image.  It is
# the only thing here that runs generated scanner code, so the only thing that
# says b6parse accepts the C11 ncform; it is also b6_lex()'s first caller.
# ../README.md, "The native case".
#
# A subdirectory of its own because cmd/lex is added outside the libruntime
# guard, where b6_prog() and b6_lex() do not exist -- as ../../yacc/rootfs is.
# Task C10d adds /usr/bin/lex and /usr/lib/lex/ncform here beside it.
#

# b6_obj reads both from the caller's scope.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_lex(SCANT_SRC scant ${CMAKE_CURRENT_SOURCE_DIR}/scant.l)
b6_prog(scant DEST test/scant SOURCES ${SCANT_SRC})

b6_progtest(scant tokens   DEST test/scant)
b6_progtest(scant eightbit DEST test/scant)
