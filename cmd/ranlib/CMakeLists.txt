#
# ranlib for BESM-6 archives: builds the __.SYMDEF table of contents.
#
# Built from the host C compiler: runs on the host and rewrites BESM-6
# archives.  The fget*/fput*/get*/put* helpers it needs come from cmd/libaout (aout).
#
add_executable(b6ranlib ranlib.c)
target_include_directories(b6ranlib PRIVATE ${BESM6_CROSS_INCLUDE})
target_link_libraries(b6ranlib PRIVATE aout)
install(TARGETS b6ranlib RUNTIME DESTINATION bin)
