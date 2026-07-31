#
# dcheck -- file system directory consistency check.  Task C4e.
#
# /etc/dcheck rather than /bin/dcheck: section 1M, root-only, and NOT setuid -- it reads
# the raw device, which is mode 0600 because that one node is every file's contents.
# fsck's, mkfs's and quot's placement, and quot's reasons.
#
# NOT `PURE', for fsck's reason: nothing shares this program, and FMAGIC keeps it clear of
# physio()'s u_tsize gate (cmd/mkfs/README.md) should it ever grow a write.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/, cmd/fsck/ and cmd/mkfs/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(dcheck DEST etc/dcheck SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dcheck.c)

add_subdirectory(test)
