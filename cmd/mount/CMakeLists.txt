#
# mount -- mount a file system.  Task C4f.
#
# /etc/mount rather than /bin/mount: section 1M, and root-only because /dev/md1 is mode 0600
# and root's.  NOT setuid -- mount(2) asks for no privilege of its own (v7 gates it on the
# mode of the special file and this kernel kept that), so the 0600 node IS the gate, and a
# setuid mount would hand it to anybody.  cmd/README.md SS8.
#
# NOT `PURE'.  Nothing here goes near a raw device, so mkfs's u.u_tsize trap cannot fire
# either way; FMAGIC is what every other program on this image is.
#
# mtab.c is shared with ../umount, which compiles it a second time out of this directory.
# One home for the format -- see its header, and include/man/mtab.5.
#
# b6_obj reads both of these from the caller's scope.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(mount DEST etc/mount
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mount.c ${CMAKE_CURRENT_SOURCE_DIR}/mtab.c)

add_subdirectory(test)
