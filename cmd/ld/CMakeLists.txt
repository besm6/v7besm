#
# Linker for BESM-6 a.out objects.
#
# Built from the host C compiler: runs on the host and combines BESM-6 objects
# (and archives produced by cmd/ar) into an executable.  The fget*/fput*/get*/put*
# helpers it needs come from cmd/libaout (aout).
#
add_executable(b6ld ld.c)
target_include_directories(b6ld PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6ld PRIVATE aout)
install(TARGETS b6ld RUNTIME DESTINATION bin)
