#
# fsck -- file system consistency check and interactive repair.  Task C4d.
#
# /etc/fsck rather than /bin/fsck: section 1M, root-only, and NOT setuid -- it writes the
# raw device, which is mode 0600 because that one node is every file's contents.  df's,
# quot's and mkfs's placement and mkfs's reasons.
#
# NOT `PURE' either, and that is worth a line rather than an omission: a pure program's
# text is read-only shared, and physio() refuses a raw transfer whose base is below
# u.u_tsize (cmd/mkfs/README.md).  Nothing here writes from the text, so PURE would in
# fact be safe -- but nothing shares this program either, one fsck at a time being the
# whole idea, and FMAGIC keeps it out of the way of that trap entirely.
#
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(fsck DEST etc/fsck SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/fsck.c)

add_subdirectory(test)
