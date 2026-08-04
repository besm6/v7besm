#
# Archiver for BESM-6 a.out objects.
#
# Built from the host C compiler: runs on the host and manipulates BESM-6
# archives.  The fget*/fput*/get*/put* helpers it needs come from cmd/libaout (aout).
#
# The same sources are built a SECOND time, for the machine itself, into
# build/rootfs/usr/bin/ar -- see rootfs/, and task C9d in ../TODO.md.  The
# archiver library below is linked into the native ranlib as well, exactly as it
# is into b6ranlib here.
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(archiver STATIC
    ar.c
    command.c
    archive.c
    list.c
    util.c
)
target_include_directories(archiver PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(archiver PUBLIC aout)

add_executable(b6ar main.c)
target_link_libraries(b6ar PRIVATE archiver)
install(TARGETS b6ar RUNTIME DESTINATION bin)

add_subdirectory(test)
