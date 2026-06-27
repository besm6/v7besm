#
# Assembler for BESM-6.
#
# Built from the host C compiler: it runs on the host and emits BESM-6 a.out
# objects.  The fput*/fgeth helpers are shared with the rest of the toolchain
# and live in cmd/libaout (the old hand-written Makefile reached them via VPATH).
#
add_executable(besm6-as
    as.c
    tables.c
    lex.c
    symtab.c
    expr.c
    pass1.c
    pass2.c
    ${CMAKE_SOURCE_DIR}/cmd/libaout/fgeth.c
    ${CMAKE_SOURCE_DIR}/cmd/libaout/fputh.c
    ${CMAKE_SOURCE_DIR}/cmd/libaout/fputhdr.c
    ${CMAKE_SOURCE_DIR}/cmd/libaout/fputsym.c
)
target_include_directories(besm6-as PRIVATE ${BESM6_CROSS_INCLUDE})
install(TARGETS besm6-as RUNTIME DESTINATION bin)
