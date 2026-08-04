# du's b6sim cases, and there are none.
#
# EVERYTHING du DOES IS A DIRECTORY READ, and b6sim cannot serve one: it maps read(2) onto
# the host, where a directory descriptor refuses to be read at all.  du still does that read
# by hand -- open(2) and read(2) of struct direct records -- because it predates the library.
#
# THERE IS AN opendir(3) NOW (lib/libc/man/directory.3), which arrived with cmd/ls, and
# converting du to it is ../../TODO.md task C24.  IT WOULD NOT MAKE A CASE POSSIBLE HERE, and
# the reason is worth knowing before anybody tries: on the host, open(2) and fstat(2) on a
# directory both SUCCEED and only read(2) refuses, so under b6sim opendir() hands back a
# perfectly good DIR and the first readdir() returns NULL.  Every directory reads as empty,
# in exactly the way an empty one does, and a case would pass while proving nothing.  That is
# ../../ls/README.md's limitation still, and it is why ls(1) has no cases here either.
#
# So there is nothing left for this harness to hold: strip the directory walk from du and
# what remains is two flag tests and a stat(2).  A case asserting those would say the
# program loads, which the rootfs_du_size check already says.
#
# WHERE du IS ASSERTED: kernel/test/fsinfo, over a tree the guest script builds with known
# sizes, so `du', `du -a' and `du -s' are diffed as arithmetic and nothing is masked.  That
# test also covers the two things only a real filesystem can show -- a hard link counted
# once, and a subdirectory descended into and climbed back out of.
#
# ../../time/test/CMakeLists.txt is the precedent for a test directory whose whole content
# is an explanation; the difference is that time(1) had one reachable path left and du has
# none.
