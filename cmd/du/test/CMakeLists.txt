# du's b6sim cases, and there are none.
#
# EVERYTHING du DOES IS A DIRECTORY READ.  It has no library for that -- there is no
# opendir(3) in v7 or in this libc -- so it open(2)s a directory and read(2)s struct direct
# records straight out of it, and b6sim maps read(2) onto the host, where a directory
# descriptor refuses to be read at all.  That is ../../ls/README.md's limitation exactly,
# and it is why ls(1) has no cases here either.
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
