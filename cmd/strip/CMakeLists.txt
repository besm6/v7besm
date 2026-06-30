#
# strip for BESM-6 a.out objects: removes the symbol table and relocation bits.
#
# Built from the host C compiler: runs on the host and rewrites BESM-6 objects.
# The fget*/fput* helpers it needs come from cmd/libaout (aout).
#
add_executable(b6strip strip.c)
target_include_directories(b6strip PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6strip PRIVATE aout)
install(TARGETS b6strip RUNTIME DESTINATION bin)
