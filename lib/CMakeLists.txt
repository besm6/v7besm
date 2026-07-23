# The user-level libraries for programs running under this kernel: libc.a, libm.a and the
# crt0.o beside them, plus the b6sim test harness.  Cross-compiled by the b6* toolchain (not
# the host compiler), so -- like kernel/ -- this is a `project(... NONE)' build that drives
# the tools through custom commands.  It shares kernel/'s toolchain resolution: integrated it
# uses the in-tree tool targets, standalone (`cmake -S lib') the installed ones.
#
# The old three-step bootstrap (`make && make install', then `make -C lib', then
# `make -C lib install') is gone: compiling with the in-tree tools means lib no longer needs
# the toolchain installed first, so it builds under the top-level `make' and installs under
# `make install', beside the c-compiler's libruntime.a in share/besm6/lib.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.10)
    project(lib NONE)
endif()

# The cross toolchain, the external runtime, and the b6_obj helper come from the shared module.
# Integrated, the top-level project already included it; standalone, include it now.
if(NOT COMMAND b6_obj)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../scripts/BesmCross.cmake)
endif()

# The one system-header tree.  -I names it in the source tree, ahead of the installed copy
# b6cc appends, so a build here sees what it just edited.  b6cc has no -M, so every object
# takes a coarse dependency on the whole tree (headers.c in test/ includes all of it anyway).
# KINC/KHDRS are read by b6_obj from this scope, and inherited by the libc/libm/test subdirs.
get_filename_component(KINC ${CMAKE_CURRENT_SOURCE_DIR}/../include ABSOLUTE)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

enable_testing()
add_subdirectory(libc)
add_subdirectory(libm)
add_subdirectory(test)
