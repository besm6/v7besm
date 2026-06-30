#
# strip for BESM-6 a.out objects: removes the symbol table and relocation bits.
#
# Built from the host C compiler: runs on the host and rewrites BESM-6 objects.
# The fget*/fput* helpers it needs come from cmd/libaout (aout).
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(strip STATIC strip.c)
target_include_directories(strip PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strip PUBLIC aout)

add_executable(b6strip main.c)
target_link_libraries(b6strip PRIVATE strip)
install(TARGETS b6strip RUNTIME DESTINATION bin)

add_subdirectory(test)
