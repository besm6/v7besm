#
# size for BESM-6 a.out objects: prints const/text/data/bss segment sizes.
#
# Built from the host C compiler: runs on the host and reads BESM-6 objects.
# The fget* helpers it needs come from cmd/libaout (aout).
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(size STATIC size.c)
target_include_directories(size PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(size PUBLIC aout)

add_executable(b6size main.c)
target_link_libraries(b6size PRIVATE size)
install(TARGETS b6size RUNTIME DESTINATION bin)

add_subdirectory(test)
