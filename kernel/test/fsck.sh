# /etc/fscktest -- the guest half of kernel/test/fsck.  Task C4d.
#
# GRAFTED AS /etc/fscktest AND NOT /etc/fsck, which is the program: `b6fsutil -a' would
# write this script straight over it.  mkfs.sh has the same trap and the same note.
#
# WHAT THIS BOOTS FOR.  cmd/fsck/test next door already damages an image, repairs it and
# has `b6fsutil -c' pronounce on the result, thirteen ways, in a second and a half.  So the
# algorithm is settled before this file runs and what is left is the DEVICE: a repair is a
# scatter of small WRITES through physio() and mdwrite() -- a directory block here, an
# i-list block there, and in phase 6 the entire free list of a 2000-block volume -- and
# none of that exists under b6sim, whose write(2) is the host's.
#
# TWO OUTPUT CHANNELS, fsinfo.sh's convention:
#
#   /tmp/fsck.log     everything about the SCRATCH pack, diffed against fsck.expected with
#                     nothing masked.  Every number in it is arithmetic this test chose.
#                     Note that OWNER= is real here and is not masked as cmd/fsck/test has
#                     to mask it: getpw(3) reads THIS system's /etc/passwd, which is the
#                     image's, so uid 7 is `guest' deterministically.
#   /dev/console      the live root's check, and only that.  A program that measures a
#                     filesystem cannot write its answer into one -- df/README.md -- and
#                     here that is not a nicety: the log file's own blocks would be part of
#                     what the check counts.
#
# ############################################################################
# NOTHING MAY BE ADDED BELOW THE ---root--- SECTION.  READ THIS BEFORE EDITING.
# ############################################################################
#
# That section checks the filesystem this script is stored on, WHILE IT IS MOUNTED.  fsck
# calls sync(2) first, so the disk and the kernel's core agree at the instant it starts
# reading -- and the check is only sound for as long as nothing allocates a block after
# that.  Every line below writes to the console and to nothing else; a single `>>/tmp/...'
# added underneath would allocate, and the run would fail with `N BLK(S) MISSING' in some
# runs and not others.  Put new work ABOVE the sync, not below it.

echo fsck start >/tmp/fsck.log

# ---- 1.  THE REPAIR, over the real raw write path.  /dev/rmd1 is a different volume from
#          the one this log lives on, so nothing here is self-referential.
echo ---repair--- >>/tmp/fsck.log
/etc/fsck -y /dev/rmd1 >>/tmp/fsck.log 2>&1
echo status $? >>/tmp/fsck.log

# ---- 2.  IDEMPOTENCE.  A repair that has to be run twice did not finish the first time.
#          -n, so a second pass cannot write even if it wants to.
echo ---again--- >>/tmp/fsck.log
/etc/fsck -n /dev/rmd1 >>/tmp/fsck.log 2>&1
echo status $? >>/tmp/fsck.log

# ---- 3.  Two diagnostics, and both are about arguments rather than filesystems -- the
#          cheap half of what cmd/fsck/test asserts, run here as well because these are
#          the bytes on the image rather than the bytes b6sim was handed.
echo ---bad--- >>/tmp/fsck.log
/etc/fsck -n -s /dev/rmd1 >>/tmp/fsck.log 2>&1
echo status $? >>/tmp/fsck.log
/etc/fsck /dev/nosuch >>/tmp/fsck.log 2>&1
echo status $? >>/tmp/fsck.log

echo ---end--- >>/tmp/fsck.log
ls /tmp >>/tmp/fsck.log
sync

# ---- 4.  THE LIVE ROOT, READ ONLY, AND LAST.  This is the one-line statement of the whole
#          task: the machine reading, and pronouncing sound, the filesystem it is running
#          on.  df follows immediately, on the same device through the same free-list walk,
#          so the host can hold the two guest numbers against each other as well as against
#          its own.
echo ---root--- >/dev/console
/etc/fsck -n /dev/rmd0 >/dev/console
df /dev/rmd0 >/dev/console
echo ---endroot--- >/dev/console

sync
echo fsck done >/dev/console
