#
# ncheck -- generate names from i-numbers.  Task C4e.
#
# /etc/ncheck rather than /bin/ncheck: section 1M, root-only, and NOT setuid -- it reads
# the raw device, which is mode 0600 because that one node is every file's contents, and a
# program that prints every path name on the volume reports on directories the caller may
# not be able to read.  fsck's, mkfs's and quot's placement.
#
# NOT `PURE', for fsck's reason: nothing shares this program.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/, cmd/fsck/ and cmd/mkfs/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(ncheck DEST etc/ncheck SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ncheck.c)

add_subdirectory(test)
