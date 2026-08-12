#
# yacc, the parser generator, installed as b6yacc.  Task C10a (../README.md).
# README.md is the account of what the port changed and why.
#
# yaccpar.c is the fifth source and is NOT compiled: it is the parser skeleton,
# copied verbatim into every y.tab.c, so it is data to this tool and source to
# whatever compiles the result.  Hence installed rather than linked.
#
add_executable(b6yacc y1.c y2.c y3.c y4.c)
target_include_directories(b6yacc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS b6yacc RUNTIME DESTINATION bin)

# Beside the header tree the top-level CMakeLists.txt installs.  find_parser()
# in y1.c searches here; B6YACCPAR overrides it, which is how the tests and
# b6_yacc() reach the skeleton with nothing installed.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/yaccpar.c DESTINATION share/besm6)

add_subdirectory(test)
