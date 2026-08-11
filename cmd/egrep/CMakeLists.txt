# egrep(1) -- task C26, the second v7 command on this image built from a GRAMMAR.
# ../expr/CMakeLists.txt explains b6_yacc()'s two directories; the same applies here,
# and egrep.y likewise includes no y.tab.h, so there is no CFLAGS -I either.
#
# y.output's CONFLICT COUNT IS NOT ZERO HERE, where ../expr's is: two shift/reduce, both
# on the `error' token in `r: error', both v7's own and both resolved by shifting
# (../yacc/README.md).  A THIRD would be a change to the grammar.
#
# NOT PURE, NOT SETUID.  rootfs_egrep_size is the ../README.md SS6 check; the stack
# ceiling it cannot make is why the three tree walks are iterative -- README.md.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_yacc(EGREP_SRC egrep ${CMAKE_CURRENT_SOURCE_DIR}/egrep.y)
b6_prog(egrep DEST bin/egrep SOURCES ${EGREP_SRC})

add_subdirectory(test)
