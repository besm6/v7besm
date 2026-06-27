#
# Assembler for BESM-6.
#
# Built from the host C compiler: it runs on the host and emits BESM-6 a.out
# objects.  The fput*/fgeth helpers it needs are shared with the rest of the
# toolchain and come from the cmd/libaout (aout) library.
#
add_executable(besm6-as
    as.c
    tables.c
    lex.c
    symtab.c
    expr.c
    pass1.c
    pass2.c
)
target_include_directories(besm6-as PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(besm6-as PRIVATE aout)
install(TARGETS besm6-as RUNTIME DESTINATION bin)
