#
# C preprocessor for BESM-6 (John F. Reiser cpp), installed as b6cpp.
#
# Built with the host compiler: it runs on the host and preprocesses source
# before the assembler.  main() lives in cpp.c; the engine is split across
# buffer/scan/direct/macro/diag.c (sharing intern.h) plus the #if expression
# lexer/parser (yylex.c/parser.c), all linked straight into the tool.
#
add_executable(b6cpp cpp.c buffer.c scan.c direct.c macro.c diag.c parser.c yylex.c)
target_include_directories(b6cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS b6cpp RUNTIME DESTINATION bin)

add_subdirectory(test)
