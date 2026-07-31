#
# icheck -- file system storage consistency check, and free-list salvage.  Task C4e.
#
# /etc/icheck rather than /bin/icheck: section 1M, root-only, and NOT setuid -- it reads
# the raw device, which is mode 0600 because that one node is every file's contents, and
# -s rewrites the free list of the whole volume.  fsck's, mkfs's and quot's placement, and
# the sharpest of their reasons.
#
# NOT `PURE', for fsck's reason: physio() refuses a raw transfer whose base is below
# u.u_tsize (cmd/mkfs/README.md), and FMAGIC keeps -s's write path clear of that trap
# entirely.  Nothing shares this program either.
#
# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/fsck/ and cmd/mkfs/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(icheck DEST etc/icheck SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/icheck.c)

add_subdirectory(test)
