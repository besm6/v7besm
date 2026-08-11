# awk -- task C17, and the only program here that is a yacc grammar AND a lex scanner.
#
# TWO -I's, in opposite directions.  The source directory because the generated parser and
# scanner are compiled out of their own .dir and include awk.h; AWKGRAM_SRC_DIR because the
# seven hand-written units include y.tab.h, which b6_yacc() leaves beside the parser.
#
# THE GENERATED SOURCES ARE NAMED awkgram AND awklex: b6_obj() derives an object's name
# from its source's, so `awk' would collide and so would `b' or `run'.
#
# LIBS libm makes awk the first cmd/ program to link -lm; log, exp and sqrt are awk
# built-ins and fmod and trunc carry % and int().  README.md, "The float".
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/awk.h)

b6_yacc(AWKGRAM_SRC awkgram ${CMAKE_CURRENT_SOURCE_DIR}/awk.g.y)
b6_lex(AWKLEX_SRC awklex ${CMAKE_CURRENT_SOURCE_DIR}/awk.lx.l)

# y.tab.h is a generated header the hand-written units include, so it goes in KHDRS --
# b6_obj()'s only dependency list -- or they compile before b6yacc has run.
list(APPEND KHDRS ${AWKGRAM_SRC_DIR}/y.tab.h)

b6_prog(awk DEST bin/awk
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR} -I${AWKGRAM_SRC_DIR}
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/b.c
                ${CMAKE_CURRENT_SOURCE_DIR}/lib.c
                ${CMAKE_CURRENT_SOURCE_DIR}/main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/parse.c
                ${CMAKE_CURRENT_SOURCE_DIR}/proctab.c
                ${CMAKE_CURRENT_SOURCE_DIR}/run.c
                ${CMAKE_CURRENT_SOURCE_DIR}/tran.c
                ${AWKGRAM_SRC}
                ${AWKLEX_SRC}
        LIBS libm)

add_subdirectory(test)
