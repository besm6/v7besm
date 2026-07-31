#
# umount -- dismount a file system.  Task C4f, and ../mount/CMakeLists.txt is the account of
# why this is /etc and not /bin and why neither program is setuid.
#
# ../mount/mtab.c IS COMPILED HERE A SECOND TIME rather than copied: it is the one home of
# the /etc/mtab format, which both programs read and write.  b6_prog() puts every object in
# ${name}.dir, so the two builds of it cannot collide.  -I is for its header.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(umount DEST etc/umount
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/umount.c ${CMAKE_CURRENT_SOURCE_DIR}/../mount/mtab.c
    CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/../mount)

add_subdirectory(test)
