# /etc/session -- the script kernel/test/session drives, run as sh /etc/session.
#
# THIS FILE IS NOT IN root.manifest and is not part of the shipped root filesystem.
# run-session.sh grafts it onto its own converted copy of the image with b6fsutil -a,
# which is what keeps a test fixture out of the tree the kernel really boots.
#
# The hash is a real comment: this shell has one, which v7's had not -- see
# cmd/sh/README.md.  This file used to be written in `:' lines, whose words were still
# parsed, and it learned that the hard way twice over: one draft put parentheses round a
# section number and the shell went looking for a subshell, the next put a semicolon
# mid-sentence and it ran the rest of the line.  Neither can happen here.
#
# WHAT THIS PROVES, and why each line is the one it is.  Everything above this test stops
# at reading -- boot execs, console types.  Here the kernel WRITES -- creat, write through
# the buffer cache, an inode grown past its direct blocks, a directory made and unmade --
# and then sync forces it all out and the host fscks what actually reached the platter.
#
# /tmp/session.log is built up by several commands in turn, so it exercises append as well
# as create, and it is what run-session.sh diffs against session.expected.  The names it
# collects are all the test needs to see.  Nothing here prints a time or a size, because an
# expectation file may record only what is reproducible.

echo hello >/tmp/session.log
pwd >>/tmp/session.log
ls /bin >>/tmp/session.log

# mkdir and rmdir (task C1a), and this is the harness that can adjudicate them: what a
# directory IS on the disk is a question for fsck, and run-session.sh runs one.  Six lines,
# and each is a different claim:
#
#   pwd from inside /tmp/d prints /tmp/d only if `..' was linked correctly -- pwd walks UP
#       through it (cmd/pwd/pwd.c), so the entry mkdir made is what answers.
#   rmdir of the directory it is standing in must be refused; that is rmdir's stat("")
#       against stat(name), and stat("") naming the current directory is a kernel property
#       (kernel/nami.c faults an empty path only for create-or-delete).
#   rmdir of a name ending in `.' must be refused, and v7's own rmdir did NOT refuse it --
#       see cmd/rmdir/rmdir.c.  Without that fix this line unlinks /tmp's entry for d and
#       d's own, and the fsck below is what would catch it.
#   rmdir of a directory with anything in it must be refused, which is the read loop over
#       struct direct -- four words here, not v7's sixteen bytes.
#   /tmp/e is a whole create-and-remove round trip, and the assertion is negative: it must
#       not appear in the listing further down.
#
# /tmp/d IS LEFT BEHIND ON PURPOSE.  It is the only live directory the guest ever makes, so
# it is the only thing that puts b6fsutil -c's link-count pass to work: /tmp at nlink 3 and
# d at nlink 2, with `.' and `..' pointing where they should.

mkdir /tmp/d
cd /tmp/d
pwd >>/tmp/session.log
rmdir /tmp/d 2>>/tmp/session.log
cd /
rmdir /tmp/d/. 2>>/tmp/session.log
rmdir /tmp 2>>/tmp/session.log
mkdir /tmp/e
rmdir /tmp/e

# /tmp/big is /bin/ls copied.  ls is about 33 KB, and an inode here holds 6 direct block
# addresses of 3072 bytes each -- NADDR in sys/param.h -- so a file this size is the
# smallest one that must reach through the SINGLE INDIRECT BLOCK -- bmap allocating it, and
# fsck having to follow it afterwards.  run-session.sh compares it byte for byte with the
# /bin/ls it was copied from, out of the same extracted image, so a cat or a bmap that
# dropped or duplicated a block cannot pass.

cat /bin/ls >/tmp/big
ls /tmp >>/tmp/session.log

# sync, and it must be the LAST thing that writes.  There is no update daemon on this
# system -- nothing flushes the cache on a timer -- so without this the delayed-write
# buffers holding everything above would still be in core when the simulator stops.  The
# echo that follows execs one more program and so dirties an access time that never
# reaches the disk.  That is not an inconsistency, and fsck does not care.

sync
echo session done >/dev/console
