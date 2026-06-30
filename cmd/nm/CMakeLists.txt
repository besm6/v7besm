#
# nm for BESM-6 a.out objects: lists symbol-table entries.
#
# Built from the host C compiler: runs on the host and reads BESM-6 objects
# and archives.  The fget* helpers it needs come from cmd/libaout (aout).
#
add_executable(b6nm nm.c)
target_include_directories(b6nm PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6nm PRIVATE aout)
install(TARGETS b6nm RUNTIME DESTINATION bin)
