#
# lex, the lexical-analyser generator, installed as b6lex.  Task C10b (../README.md).
# README.md is the account of what the port changed and why.
#
# ncform is the seventh source and is NOT compiled: it is the scanner skeleton,
# copied verbatim into every lex.yy.c, so it is data to this tool and source to
# whatever compiles the result.  Hence installed rather than linked, exactly as
# ../yacc/yaccpar.c is.
#
# parser.y is compiled by the b6yacc built beside it -- the first consumer of
# task C10a that is not a test, and the first HOST one.  b6_yacc() cannot serve:
# it lives inside the libruntime.a guard and hands its output to b6_obj(), which
# cross-compiles.  Three details are load-bearing, all three b6_yacc()'s:
#   * a working directory of its own, since y.tab.c/y.tab.h/y.output are fixed
#     names that cannot be moved on the command line;
#   * the generated source RENAMED off y.tab.c, because CMake's NAME_WE cuts it
#     at the first dot and it would compile to y.o;
#   * b6yacc AND yaccpar.c both in DEPENDS -- a $<TARGET_FILE:> in COMMAND buys
#     ordering, not staleness, and the skeleton is a data file no target-level
#     dependency can see.  Editing either regenerates this parser.
#
set(_pdir ${CMAKE_CURRENT_BINARY_DIR}/parser.yacc.dir)
file(MAKE_DIRECTORY ${_pdir})
set(_yaccpar ${CMAKE_CURRENT_SOURCE_DIR}/../yacc/yaccpar.c)

add_custom_command(OUTPUT ${_pdir}/lexparse.c
    COMMAND ${CMAKE_COMMAND} -E env B6YACCPAR=${_yaccpar}
            $<TARGET_FILE:b6yacc> ${CMAKE_CURRENT_SOURCE_DIR}/parser.y
    COMMAND ${CMAKE_COMMAND} -E rename y.tab.c ${_pdir}/lexparse.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/parser.y ${_yaccpar} $<TARGET_FILE:b6yacc>
    WORKING_DIRECTORY ${_pdir}
    COMMENT "b6yacc lexparse.c" VERBATIM)

add_executable(b6lex lmain.c sub1.c sub2.c header.c ${_pdir}/lexparse.c)
target_include_directories(b6lex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Two properties of yacc's output rather than of this grammar, and the same pair
# ../yacc/test turns off to compile a generated parser: yyerrlab is unreferenced
# unless the grammar uses YYERROR, and yypvt is set but unused unless an action
# names $n.  Everything hand-written here builds -Wall -Werror -Wshadow.
# ...and cppcheck sees the same two, plus more of the skeleton's shapes, so it is
# not run over machine-written C at all.  This is the only generated source the
# build compiles; everything hand-written here is linted.
set_source_files_properties(${_pdir}/lexparse.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-label;-Wno-unused-but-set-variable"
    SKIP_LINTING TRUE)

install(TARGETS b6lex RUNTIME DESTINATION bin)

# Beside the header tree the top-level CMakeLists.txt installs, in a lex/
# subdirectory because C10d's /usr/lib/lex/ncform has one.  find_form() in
# lmain.c searches here; B6LEXFORM overrides it, which is how the tests and
# b6_lex() reach the skeleton with nothing installed.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ncform DESTINATION share/besm6/lex)

add_subdirectory(test)
