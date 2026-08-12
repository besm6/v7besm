# du's b6sim cases, and there are none.
#
# EVERYTHING du DOES IS A DIRECTORY READ, and b6sim cannot serve one: it maps read(2) onto
# the host, where a directory descriptor refuses to be read at all.
#
# du IS ON opendir(3) SINCE ../../README.md TASK C24, AND THAT CHANGED NOTHING HERE.  The reason
# is worth knowing before anybody tries again: on the host, open(2) and fstat(2) on a directory
# both SUCCEED and only read(2) refuses, so under b6sim opendir() hands back a perfectly good
# DIR and the first readdir() returns NULL.  Every directory reads as empty, in exactly the way
# an empty one does, and a case would pass while proving nothing.  THAT IS WORSE THAN THE
# ERROR IT REPLACED, not better.  It is ../../ls/README.md's limitation still, and it is why
# ls(1) has no cases here either.
#
# So there is nothing left for this harness to hold: strip the directory walk from du and
# what remains is two flag tests and a stat(2).  A case asserting those would say the
# program loads, which the rootfs_du_size check already says.
#
# WHERE du IS ASSERTED: NOWHERE.  kernel/test/fsinfo built a tree with known sizes and diffed
# `du', `du -a' and `du -s' as arithmetic with nothing masked -- including the two things only
# a real filesystem can show, a hard link counted once and a subdirectory descended into and
# climbed back out of -- and it was deleted with the other seventeen typed dialogues.  A
# conversion of the walk is exactly what that test would have checked, so C24 was verified by
# hand at a booted machine instead.  ../../README.md SS9 prices a test with a boot of its own.
#
# ../../time/test/CMakeLists.txt is the precedent for a test directory whose whole content
# is an explanation; the difference is that time(1) had one reachable path left and du has
# none.
