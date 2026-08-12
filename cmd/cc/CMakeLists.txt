#
# C compiler driver for BESM-6, installed as b6cc.
#
# A host tool: it runs on the build machine and drives the BESM-6 pipeline
# (b6cpp -> b6parse -> b6lower -> b6codegen -> b6as -> b6ld) to turn C into
# BESM-6 a.out objects/executables.  Sub-tools are located via per-tool
# environment overrides (B6CPP, B6PARSE, ...) or under ~/.local/bin | /usr/local/bin.
#
# The same source is built a SECOND time, for the machine itself, into
# build/rootfs/usr/bin/cc -- see rootfs/, and task C9e in ../README.md.  There the
# search is /usr/bin, /usr/include and /lib, and the sub-tools carry no `b6'
# prefix; the profile at the head of cc.c is the whole of the difference.
#
add_executable(b6cc cc.c)
target_include_directories(b6cc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS b6cc RUNTIME DESTINATION bin)

add_subdirectory(test)
