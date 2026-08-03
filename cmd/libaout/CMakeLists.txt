#
# Library of helpers to serialize/deserialize BESM-6 a.out object files.
# Shared by as, ld, ar, nm, size and the rest of the toolchain.
#
# This target is the HOST one: built by the host C compiler, it runs on the host
# and manipulates BESM-6 objects.  The same sources are also cross-compiled into
# the native /usr/bin/as and /usr/bin/ld (task C9b) -- see sources.cmake, which
# names them once so the two builds cannot drift apart.
#
# The fd-based ar/ranlib helpers (getint.c, putint.c, getarhdr.c, putarhdr.c)
# keep their file-descriptor interface for in-place archive rewrites, but now
# share the BESM-6 half-word on-disk layout used by the fget*() routines.
#
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
add_library(aout STATIC ${B6_LIBAOUT_SOURCES})
target_include_directories(aout PUBLIC ${BESM6_CROSS_INCLUDE})

add_subdirectory(test)
