#
# nm for BESM-6 a.out objects: lists symbol-table entries.
#
# Built from the host C compiler: runs on the host and reads BESM-6 objects
# and archives.  The fget* helpers it needs come from cmd/libaout (aout).
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(nm STATIC nm.c)
target_include_directories(nm PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nm PUBLIC aout)

add_executable(b6nm main.c)
target_link_libraries(b6nm PRIVATE nm)
install(TARGETS b6nm RUNTIME DESTINATION bin)

add_subdirectory(test)
