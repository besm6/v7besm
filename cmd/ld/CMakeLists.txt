#
# Linker for BESM-6 a.out objects.
#
# Built from the host C compiler: runs on the host and combines BESM-6 objects
# (and archives produced by cmd/ar) into an executable.  The fget*/fput*/get*/put*
# helpers it needs come from cmd/libaout (aout).
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(linker STATIC
    ld.c
    symtab.c
    library.c
    pass1.c
    pass2.c
    reloc.c
    output.c
)
target_include_directories(linker PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(linker PUBLIC aout)

add_executable(b6ld main.c)
target_link_libraries(b6ld PRIVATE linker)
install(TARGETS b6ld RUNTIME DESTINATION bin)

add_subdirectory(test)
