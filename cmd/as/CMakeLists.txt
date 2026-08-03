#
# Assembler for BESM-6.
#
# This directory builds the HOST tool b6as: compiled by the host C compiler, it
# runs on the host and emits BESM-6 a.out objects.  The fput*/fgeth helpers it
# needs are shared with the rest of the toolchain and come from the cmd/libaout
# (aout) library.
#
# The same sources are built a SECOND time, for the machine itself, into
# build/rootfs/usr/bin/as -- see rootfs/, and task C9b in ../TODO.md.
#
# The assembler engine, split into a reusable library so it can be linked into
# both the command-line tool and the unit tests.  uerror() is left undefined
# here on purpose: the front end (main.c) or the test harness provides it.
add_library(assembler STATIC
    as.c
    tables.c
    lex.c
    symtab.c
    expr.c
    pass1.c
    pass2.c
)
target_include_directories(assembler PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(assembler PUBLIC aout)

add_executable(b6as main.c)
target_link_libraries(b6as PRIVATE assembler)
install(TARGETS b6as RUNTIME DESTINATION bin)

add_subdirectory(test)
