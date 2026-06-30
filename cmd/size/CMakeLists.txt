#
# size for BESM-6 a.out objects: prints const/text/data/bss segment sizes.
#
# Built from the host C compiler: runs on the host and reads BESM-6 objects.
# The fget* helpers it needs come from cmd/libaout (aout).
#
add_executable(b6size size.c)
target_include_directories(b6size PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6size PRIVATE aout)
install(TARGETS b6size RUNTIME DESTINATION bin)
