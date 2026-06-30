#
# Archiver for BESM-6 a.out objects.
#
# Built from the host C compiler: runs on the host and manipulates BESM-6
# archives.  The fget*/fput*/get*/put* helpers it needs come from cmd/libaout (aout).
#
add_executable(b6ar ar.c)
target_include_directories(b6ar PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6ar PRIVATE aout)
install(TARGETS b6ar RUNTIME DESTINATION bin)
