# tar(1) -- archive files.  Task C7 (../TODO.md), and the whole of it.
#
# b6_prog() cross-compiles tar.c with the b6* toolchain, links it against the libc built in
# ../../lib, stages it into build/rootfs/bin/tar for ../../root.manifest to pick up, and
# registers the ctest `rootfs_tar_size' that holds the two address-space ceilings of
# ../README.md SS6 -- const+text+data+bss under 28,672 words, and no relocatable symbol above
# word 32,767.  Neither is otherwise checked and both fail silently.
#
# NOT PURE: tar runs one at a time and a page-aligned data segment would buy nothing.
#
# NOT SETUID: it reads and writes what the caller could reach itself.  It does exec the
# setuid-root /bin/mkdir when extracting a nested tree, which is the only way to get a
# directory on a system with no mkdir(2) -- ../mkdir/README.md SS8.
#
# tar.1.umm is the manual page; nothing installs it and there is no renderer yet -- read it
# as it stands.

set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(tar DEST bin/tar SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tar.c)

add_subdirectory(test)
