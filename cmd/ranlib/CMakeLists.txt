#
# ranlib for BESM-6 archives: builds the __.SYMDEF table of contents.
#
# Built from the host C compiler: runs on the host and rewrites BESM-6
# archives.  The fget*/fput*/get*/put* helpers it needs come from cmd/libaout
# (aout); the archive rebuild is done in-process via cmd/ar's archiver library.
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(ranlib STATIC ranlib.c)
target_include_directories(ranlib PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
# archiver provides ar_run() and propagates the aout lib + cmd/ar include dir.
target_link_libraries(ranlib PUBLIC archiver aout)

add_executable(b6ranlib main.c)
target_link_libraries(b6ranlib PRIVATE ranlib)
install(TARGETS b6ranlib RUNTIME DESTINATION bin)

add_subdirectory(test)
