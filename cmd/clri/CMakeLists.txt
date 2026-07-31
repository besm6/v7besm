#
# clri -- clear an i-node.  Task C4e.
#
# /etc/clri rather than /bin/clri: section 1M, root-only, and NOT setuid -- it WRITES the
# raw device, which is mode 0600 because that one node is every file's contents, and a
# setuid clri would hand the whole volume to anybody who could think of an i-number.
# fsck's, mkfs's and quot's placement, and the sharpest of their reasons.
#
# NOT `PURE', for fsck's reason and mkfs's: a pure program's text is read-only shared and
# physio() refuses a raw transfer whose base is below u.u_tsize (cmd/mkfs/README.md).
# Nothing here writes from the text, so PURE would in fact be safe -- but nothing shares
# this program either, and FMAGIC keeps it out of the way of that trap entirely.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/, cmd/fsck/ and cmd/mkfs/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(clri DEST etc/clri SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/clri.c)

add_subdirectory(test)
