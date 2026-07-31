# /etc/mounttest -- the guest half of kernel/test/mount.  Task C4f.
#
# GRAFTED AS /etc/mounttest AND NOT /etc/mount, which is the program: `b6fsutil -a' would
# write this script straight over it.  fsck.sh and mkfs.sh have the same trap and the same
# note.
#
# WHAT THIS BOOTS FOR.  cmd/mount/test next door has four cases and all four stop before
# mount(2) is called, because b6sim services a system call on the HOST and mounting
# something on the build machine is not a test.  So unlike every other program of task C4,
# NOTHING about these two is asserted anywhere else: this file is the whole of it.
#
# And what it asserts that no earlier test could is the BUFFER CACHE on a second device.
# Everything C4 has written so far went to the raw device -- physio() -> mdstrategy() --
# and bdevsw[0] minor 1 had never carried a block.  Section 3 below writes files through
# bread()/bwrite()/bdwrite() onto a filesystem that is mounted, which is a different path
# through the same driver, and the host then has to find those files on the pack.
#
# TWO OUTPUT CHANNELS, fsinfo.sh's convention and fsck.sh's wording:
#
#   /tmp/mount.log    everything about the SCRATCH pack, diffed against mount.expected with
#                     nothing masked.  It lives on the ROOT, so measuring /mnt into it is
#                     not self-referential -- `df /dev/md1' below is exactly the thing
#                     df/README.md forbids doing to the filesystem you are writing to, and
#                     is sound here for that reason and no other.
#   /dev/console      the live root's check, and only that.
#
# NO `ls -l' AND NO `date' ANYWHERE: a mode and a time cannot be asserted in the guest,
# cmd/README.md's account of task C1.  Every line below is arithmetic or a name.
#
# ############################################################################
# NOTHING MAY BE ADDED BELOW THE ---root--- SECTION.  READ THIS BEFORE EDITING.
# ############################################################################
#
# fsck.sh's rule, imported whole and for its reason: that section measures the filesystem
# this script is stored on WHILE IT IS MOUNTED.  icheck calls sync(2) first, so the disk and
# the kernel's core agree at the instant it starts reading, and the reading is only sound
# for as long as nothing allocates a block afterwards.  A single `>>/tmp/...' added
# underneath would allocate, and the run would fail with a non-zero `missing' in some runs
# and not others.  Put new work ABOVE the sync, not below it.

echo mount start >/tmp/mount.log

# ---- 1.  WHAT IS REFUSED, and all of it before anything is mounted.  Most of these are the
#          kernel's answers rather than the program's, and the last two are the interesting
#          ones.  /dev/md2 is a blank pack -- SIMH's `attach -n' formats every zone and
#          leaves them zeros -- so mounting it is mounting garbage, which v7's mount(2)
#          believes: mount.1m says in so many words that doing so crashes the system.
#          sbcheck() (kernel/alloc.c) answers EINVAL instead and prints what it disliked on
#          the console, which is what run-mount.sh greps for.  /dev/swap is the other half of
#          the same question: a block device whose superblock cannot even be READ, which is
#          smount()'s bread() arm and answers EIO.
echo ---bad--- >>/tmp/mount.log
/etc/mount /dev/md1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/umount >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount /dev/rmd1 /mnt >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount /bin/cat /mnt >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount /dev/md1 /nosuchdir >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/umount /dev/md1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
echo ---garbage--- >/dev/console
/etc/mount /dev/md2 /mnt >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
echo ---endgarbage--- >/dev/console
/etc/mount /dev/swap /mnt >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log

# ---- 2.  THE MOUNT.  /dev/md1 and not /dev/rmd1: getmdev() wants IFBLK, which is the one
#          argument mistake this command invites, every other program of task C4 taking the
#          raw name.  The `sync' before df is not decoration -- the mounted superblock lives
#          in core (m_bufp) until update() writes it, and df reads the disk.
echo ---mount--- >>/tmp/mount.log
/etc/mount /dev/md1 /mnt >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount >>/tmp/mount.log 2>&1
cat /etc/mtab >>/tmp/mount.log 2>&1
ls /mnt >>/tmp/mount.log 2>&1
# A READ back through the mount, of a file the HOST put on the pack.  It goes to /tmp rather
# than into the log because the log would then carry a copy of etc/motd and have to be
# re-checked in whenever that file is edited; run-mount.sh cmp's the two instead, which is
# run-mkfs.sh's arrangement and asserts more.
cp /mnt/d/e/f /tmp/fromdisk
echo status $? >>/tmp/mount.log
sync
df /dev/md1 >>/tmp/mount.log 2>&1

# ---- 3.  THE POINT OF THE TASK: a write through the BUFFER CACHE onto a second drive.
#          Nothing here goes near physio().  The host finds these files on the pack
#          afterwards -- run-mount.sh's oracle 4 -- and cmp's the copy against the original.
echo ---write--- >>/tmp/mount.log
cp /etc/motd /mnt/copy
mkdir /mnt/dir
echo written through the buffer cache >/mnt/dir/hello
cat /mnt/dir/hello >>/tmp/mount.log 2>&1
ls /mnt >>/tmp/mount.log 2>&1
ls /mnt/dir >>/tmp/mount.log 2>&1
sync
df /dev/md1 >>/tmp/mount.log 2>&1

# ---- 4.  BUSY, both kinds.  The second is the one worth having: the shell that reads this
#          script is a process, `cd' changes ITS working directory, and sumount() refuses
#          while any in-core inode still belongs to the device.
echo ---busy--- >>/tmp/mount.log
/etc/mount /dev/md1 /usr >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
cd /mnt
/etc/umount /dev/md1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
cd /
/etc/umount /dev/md1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount >>/tmp/mount.log 2>&1

# ---- 5.  READ-ONLY.  s_ronly reaches access() (kernel/fio.c), so the create fails with
#          EROFS and the shell says so -- and iupdat() silently skips the access time, which
#          is the thing mount.1m warns about from the other side.
echo ---ro--- >>/tmp/mount.log
/etc/mount /dev/md1 /mnt -r >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount >>/tmp/mount.log 2>&1
cat /mnt/dir/hello >>/tmp/mount.log 2>&1
# cp(1) and not a shell redirect: a redirect that fails is diagnosed by the SHELL, on the
# shell's own stderr, which is the console -- so the log would carry the status and not the
# reason.  cp says why on a stderr this line can redirect.
cp /etc/motd /mnt/nope >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
ls /mnt >>/tmp/mount.log 2>&1
/etc/umount /dev/md1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/mount >>/tmp/mount.log 2>&1
cat /etc/mtab >>/tmp/mount.log 2>&1

# ---- 6.  TASK C4e's LOOSE END, closed here.  icheck, dcheck, ncheck and clri were asserted
#          under b6sim alone, whose read(2) and write(2) are the host's, so none of the five
#          conditions of the raw path was ever exercised for any of them -- and two of them
#          WRITE.  This is that, on a pack that has just been mounted, written and unmounted,
#          which is a filesystem no host tool laid out.
#
#          THE PACK MUST BE UNMOUNTED BY HERE.  Raw reads bypass the buffer cache, so a
#          measurement taken through /dev/rmd1 while /dev/md1 is mounted is a measurement of
#          whatever the cache has not written back yet.
#
#          The chain is C4d's rule -- assert that there was something to repair -- said from
#          inside the guest: clean, then broken on purpose, then seen to be broken by two
#          different programs, then repaired, then clean again.  i-node 7 is /d/e/f and its
#          number is mount.manifest's stanza order; that file's header says so.
echo ---icheck--- >>/tmp/mount.log
/etc/icheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/dcheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/ncheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log

echo ---clri--- >>/tmp/mount.log
/etc/clri /dev/rmd1 7 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/icheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/dcheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log

echo ---repair--- >>/tmp/mount.log
/etc/fsck -y /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/icheck -s /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log
/etc/icheck /dev/rmd1 >>/tmp/mount.log 2>&1
echo status $? >>/tmp/mount.log

# THE LAST WORD ON THE PACK, and the one run-mount.sh holds against the host's own walk of
# the image that comes back.  It has to be here, after section 6 rather than after section
# 3: clri threw a file away and fsck reclaimed its blocks, so the free count df read through
# the mount is deliberately NOT the count the pack ends with.  The mounted reading is
# asserted too -- as a literal, by the diff against mount.expected.
echo ---free--- >>/tmp/mount.log
df /dev/rmd1 >>/tmp/mount.log 2>&1

echo ---end--- >>/tmp/mount.log
ls /tmp >>/tmp/mount.log
sync

# ---- 7.  THE LIVE ROOT, READ ONLY, AND LAST.  fsck.sh's section, with icheck and dcheck in
#          fsck's place -- and `icheck -s' and `clri' are NOT here and must never be: both
#          stop the machine on a hot root by design, so pointing either at /dev/rmd0 is a
#          1,800-second timeout with no diagnostic at all.  cmd/icheck/README.md SS5.
echo ---root--- >/dev/console
/etc/icheck /dev/rmd0 >/dev/console
/etc/dcheck /dev/rmd0 >/dev/console
echo ---endroot--- >/dev/console

sync
echo mount done >/dev/console
