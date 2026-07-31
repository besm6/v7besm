# /etc/mkfstest -- the script kernel/test/mkfs drives, run as `sh /etc/mkfstest'.  Task C4c.
#
# THE PATH IS /etc/mkfstest AND NOT /etc/mkfs, which is where every other test here would
# have put it: /etc/mkfs is the PROGRAM, and `b6fsutil -a' would have written this script
# straight over it.  The only test in this directory whose name collides with something on
# the image, and the only one that has to say so.
#
# THIS FILE IS NOT IN root.manifest.  run-mkfs.sh grafts it onto a copy with `b6fsutil -a',
# as session.sh, files.sh, utils.sh, edit.sh, fsinfo.sh and dd.sh are grafted, so that
# editing the test does not rebuild the disk.
#
# WHAT NEEDS A KERNEL HERE.  cmd_mkfs_layout next door runs these same bytes under b6sim in
# a tenth of a second and compares the result with `b6fsutil -n' byte for byte -- so the
# LAYOUT is not what this test is about.  What b6sim cannot reach is the device: none of the
# four alignment conditions physio() and mdstrategy() impose exists there, mdwrite() is not
# involved at all, and there is no second drive.  Everything below is about /dev/rmd1.
#
# THE ORDER MATTERS IN TWO PLACES.
#
#   The dd write comes FIRST, and it targets BLOCK 2.  Block 2 is the first i-list block, so
#   mkfs in section 3 rewrites it -- which is what lets the byte-for-byte compare on the
#   host still hold afterwards: nothing this section leaves behind survives.  Block 0 is
#   deliberately NOT used, and that is load-bearing rather than arbitrary: kernel/dev/md.c
#   maintains only the sector address in the service words it writes, leaving the magic mark
#   and THE VOLUME NUMBER as the last read of any drive on the controller left them, and
#   zone 0 track 0 -- block 0 -- is the only half-zone whose volume number b6fsutil's
#   from_simh() reads back.  Write block 0 from the guest and the scratch pack comes back
#   claiming to be the root pack.  cmd/mkfs/README.md is the account.
#
#   The diagnostics come BEFORE the real mkfs, because each of them must leave the volume
#   untouched and the only way to say so is that what follows still works.
#
# STDOUT AND STDERR ARE NEVER MIXED IN ONE COMMAND, which is utils.sh's rule: dd's payload
# always goes to a file through of=, so only its report reaches the log.
#
# TWO CHANNELS, and fsinfo.sh's reason for them.  Anything that is a number about a WHOLE
# VOLUME -- mkfs's summary and both df lines -- goes to /dev/console between markers, where
# run-mkfs.sh recomputes it from the finished image; a program that measures a filesystem
# cannot write its answer into one.  Everything else goes to /tmp/mkfs.log and is diffed
# against mkfs.expected with nothing masked.

echo mkfs start >/tmp/mkfs.log

# ---- 1.  THE FIRST RAW WRITE THIS MACHINE HAS EVER DONE.
# It is dd's rather than mkfs's on purpose: `dd of=/dev/rmd0' has been written and
# documented since task C4b and has never once run, mdwrite() being dead code with no
# scratch volume to aim at.  One block of /etc/motd out and straight back in; run-mkfs.sh
# cmp's the two against the SAME extracted /etc/motd, so there is no fixture.
echo ---dd--- >>/tmp/mkfs.log
dd if=/etc/motd of=/dev/rmd1 seek=2 count=1 conv=sync 2>>/tmp/mkfs.log
echo status $? >>/tmp/mkfs.log
dd if=/dev/rmd1 of=/tmp/back skip=2 count=1 2>>/tmp/mkfs.log
echo status $? >>/tmp/mkfs.log

# ... and a raw write REFUSED, which nothing here had asserted either.  kernel/test/dd
# asserts two refused READS -- bs=512, not a whole number of words, and bs=6, a whole word
# but not a whole half-zone -- and the write side goes through exactly the same two gates,
# physio() answering EFAULT for the first and mdstrategy() EIO for the second.  Nothing is
# written when they fire, which is what lets these sit before the mkfs below.
dd if=/etc/motd of=/dev/rmd1 seek=2 bs=512 count=1 conv=sync 2>>/tmp/mkfs.log
echo status $? >>/tmp/mkfs.log
dd if=/etc/motd of=/dev/rmd1 seek=2 bs=6 count=1 conv=sync 2>>/tmp/mkfs.log
echo status $? >>/tmp/mkfs.log

# ---- 2.  THE TWO WAYS OF GETTING THE SIZE WRONG, and neither writes a byte.
#   4      -- below the floor; refused before open(2), so not even the descriptor is taken.
#   2001   -- one more block than an EC-5052 holds.  mkfs does not know MDNBLK and must not:
#             it reads the LAST block before writing the first and lets mdstrategy() answer,
#             which is why this fails as a READ of block 2000 and not as a size check.
#
# (A drive nobody attached is the third thing the probe catches, and it is NOT asserted here:
# it would need a device node for a drive that does not exist, and there is no mknod(1) on
# this image to make one at test time.  kernel/test/mdtest reaches that path at the driver
# level instead.  cmd_mkfs_nodev covers a special that is not there at all.)
echo ---bad--- >>/tmp/mkfs.log
/etc/mkfs /dev/rmd1 4 >>/tmp/mkfs.log 2>&1
echo status $? >>/tmp/mkfs.log
/etc/mkfs /dev/rmd1 2001 >>/tmp/mkfs.log 2>&1
echo status $? >>/tmp/mkfs.log

# ---- 3.  THE FILESYSTEM.  Once, and once only: a second mkfs at another size would leave
# this one's chain blocks behind in the data area and the host's byte compare would fail on
# blocks neither program had any reason to disagree about.
echo ---mkfs--- >/dev/console
/etc/mkfs /dev/rmd1 2000 >/dev/console
echo status $? >/dev/console

# ---- 4.  READ IT BACK, through another program and the other direction of the same path.
echo ---read--- >>/tmp/mkfs.log
dd if=/dev/rmd1 of=/tmp/newfs count=2 2>>/tmp/mkfs.log
echo status $? >>/tmp/mkfs.log

# ---- 5.  df ON A FILESYSTEM THAT DID NOT EXIST WHEN THE MACHINE BOOTED, which is the whole
# point of the task in one line.  The second df is the control: drive 0 must be untouched.
df /dev/rmd1 >/dev/console
df /dev/rmd0 >/dev/console
echo ---endmkfs--- >/dev/console

echo ---end--- >>/tmp/mkfs.log
ls /tmp >>/tmp/mkfs.log

sync
echo mkfs done >/dev/console
