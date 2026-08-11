# make(1) -- task C14, and the first program here whose grammar declares a %union.
# The union path was retired ahead of this port by ../yacc/rootfs/calcu.y.
#
# THE GENERATED SOURCE IS NAMED gram AND NOT make: b6_yacc's second argument names
# the object too, and `make.o' is not a name this program has to spare.
#
# NO CFLAGS -I for y.tab.h: no hand-written unit here names a token.  The -I on
# this directory is for the other direction -- gram.y includes defs.h and the
# generated source b6cpp sees is in gram.yacc.dir, not beside the header.
#
# THE LIBAOUT SOURCES are for lookarch(), which resolves a  lib.a(member)  or
# lib.a((entry))  dependency.  An archive header, an object header and a symbol
# are six-byte words on this machine, so they are read through cmd/libaout rather
# than fread() into a struct.  The _NATIVE subset: the four file-descriptor
# routines belong to ar and ranlib alone (../libaout/sources.cmake).
#
# NOT PURE, NOT SETUID.  rootfs_make_size is ../README.md SS6's check; the stack
# ceiling it cannot make is why doname() carries an explicit depth bound.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/ and cmd/m4.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# ...and the headers that glob does not reach: this program's own, and the cross
# headers that define the archive and object formats lookarch() reads.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/defs.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/ar.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/b.out.h
                  ${CMAKE_SOURCE_DIR}/cross/besm6/types.h)

include(${CMAKE_SOURCE_DIR}/cmd/libaout/sources.cmake)

b6_yacc(GRAM_SRC gram ${CMAKE_CURRENT_SOURCE_DIR}/gram.y)

# -I${CMAKE_SOURCE_DIR}/cross for "besm6/ar.h", absolute: CFLAGS goes onto the
# b6cc command line as written and the compiler does not run in this directory.
b6_prog(make DEST bin/make
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR} -I${CMAKE_SOURCE_DIR}/cross
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/doname.c
                ${CMAKE_CURRENT_SOURCE_DIR}/files.c
                ${CMAKE_CURRENT_SOURCE_DIR}/misc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/dosys.c
                ${CMAKE_CURRENT_SOURCE_DIR}/ident.c
                ${GRAM_SRC}
                ${B6_LIBAOUT_SOURCES_NATIVE})

add_subdirectory(test)
