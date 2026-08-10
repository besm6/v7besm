#
# calct -- task C10a's native assertion, and NOT a program on the image.  It is
# the only thing here that runs generated parser code, so the only thing that
# says b6parse accepts the C11 yaccpar.c; it is also b6_yacc()'s first caller.
# ../README.md, "The native case".
#
# A subdirectory of its own because cmd/yacc is added outside the libruntime
# guard, where b6_prog() and b6_yacc() do not exist -- as cmd/libaout/rootfs is.
# Task C10c adds /usr/bin/yacc and /usr/lib/yaccpar here beside it.
#

# b6_obj reads both from the caller's scope.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_yacc(CALCT_SRC calct ${CMAKE_CURRENT_SOURCE_DIR}/calct.y)
b6_prog(calct DEST test/calct SOURCES ${CALCT_SRC})

b6_progtest(calct arith    DEST test/calct)
b6_progtest(calct prec     DEST test/calct)
b6_progtest(calct recover  DEST test/calct)
b6_progtest(calct eightbit DEST test/calct)
