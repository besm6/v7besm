# /etc/fsinfo -- the script kernel/test/fsinfo drives, run as `sh /etc/fsinfo'.  Task C4a.
#
# THIS FILE IS NOT IN root.manifest.  run-fsinfo.sh grafts it onto a copy of the image with
# `b6fsutil -a', as session.sh, files.sh, utils.sh and edit.sh are grafted, so that editing
# the test does not rebuild the disk.
#
# THE QUESTION THIS VOLUME ASKS.  Every test above it asks what the guest can DO to the
# filesystem -- create files, rearrange them, re-permission them, author text into them --
# and then asks the HOST whether the result is still a filesystem.  This one asks whether
# the guest can say anything about the filesystem ITSELF: how much room is left, what a
# directory costs, and who owns the disk.  Nothing before it could.
#
# WHERE EACH NUMBER GOES, AND WHY IT IS NOT ALL ONE LOG.  There are two kinds of number
# here and they cannot be asserted the same way:
#
#   * du's numbers are about a tree THIS SCRIPT BUILT, of sizes it chose.  They are
#     arithmetic, they are reproducible, and they go in /tmp/fsinfo.log, which run-fsinfo.sh
#     diffs against fsinfo.expected with nothing masked.
#
#   * df's and quot's are about THE WHOLE IMAGE, and would change every time a program is
#     added to /bin.  A checked-in expectation for those would be a file somebody has to
#     update by hand for a reason that has nothing to do with this test, so there is none:
#     they go to THE CONSOLE, between markers, and run-fsinfo.sh diffs them against numbers
#     it RECOMPUTES from the image -- `b6fsutil -c -v' for df and an awk over `b6fsutil
#     -v -v' for quot.
#
# ...AND THAT IS ALSO WHY THEY GO TO THE CONSOLE RATHER THAN TO A FILE.  Both programs
# measure the filesystem they would be writing their answer into.  A `quot >/tmp/x' counts
# /tmp/x at the size it has BEFORE quot's own output reaches it, and a `df >/tmp/x' samples
# the free list before the block that output needs is allocated -- so either report,
# written to disk, disagrees with the disk by exactly its own size, and the host oracle
# would have to subtract a fudge factor nobody can check.  Written to the console, nothing
# is allocated after the measurement and the two agree exactly.
#
# THE ORDER OF THE LAST FIVE LINES IS LOAD-BEARING, for the same reason.  sync first, so
# that what df and quot read off the RAW DEVICE is what the kernel has in core -- these two
# bypass the buffer cache, which is the whole point of cmd/df/README.md -- then measure,
# then sync again and say so on the console.  Nothing between the first sync and the end of
# the run allocates a block.

echo fsinfo start >/tmp/fsinfo.log

# ---- 1.  A TREE OF KNOWN SIZE.  Every file here is either a here-document this script
# wrote or a concatenation of /etc/motd, so the byte counts are the test's own and not the
# image's -- except motd's 365, which sets `big'.  TWELVE copies, 4,380 bytes: that is two
# FILESYSTEM blocks, and it stays two for any motd between 257 and 512 bytes, so the margin
# is wide on both sides.  edit.expected already depends on motd's length, so this is not a
# new kind of dependency, only a much less sensitive one.
#
# THE MARGIN IS THE SAME WIDTH IN THE REPORTED UNIT, and that is not a coincidence: du(1)
# counts filesystem blocks and multiplies by KBPB at the printf, so `big' reports 6 and the
# bucket it sits in is still the 3073..6144-byte one.  Had the port divided sizes by 1024
# instead, every file here would sit on its own boundary and this margin would be a third
# as wide.  ../../cmd/df/README.md is the account.
mkdir /tmp/fs
cat >/tmp/fs/one <<\!
one
two
three
!
cat /etc/motd /etc/motd /etc/motd /etc/motd /etc/motd /etc/motd >/tmp/fs/big
cat /etc/motd /etc/motd /etc/motd /etc/motd /etc/motd /etc/motd >>/tmp/fs/big
mkdir /tmp/fs/sub
cat >/tmp/fs/sub/two <<\!
a single line
!

# THE HARD LINK, which is the one thing about du that only a real filesystem can show: the
# same inode under two names, in two different directories, and du(1) must charge for it
# once.  It counts it where it MEETS it first -- /tmp/fs/big, since that directory is read
# before sub is descended into -- so `link' below is worth zero blocks and the total is not
# six larger for its being there.
#
# THE PROOF OF THAT IS AN ABSENCE, and it is worth pointing at because a missing line reads
# like an accident: /tmp/fs/sub/link does not appear in the `du -a' listing at all.
# descend() returns at the already-counted test, which is ahead of the Aflag print, so a
# repeated link is not merely charged zero -- it is not mentioned.  That is v7's behaviour
# and it is asserted here rather than worked around.
ln /tmp/fs/big /tmp/fs/sub/link

# ---- 2.  du, three ways.  Bare: one line per directory, deepest first, because descend()
# prints on the way out.  -a: a line per file as well.  -s: the total alone.  The three
# totals must agree, and that they do is an assertion the log makes just by holding them
# next to each other.
echo ---du--- >>/tmp/fsinfo.log
du /tmp/fs >>/tmp/fsinfo.log
echo ---du-a--- >>/tmp/fsinfo.log
du -a /tmp/fs >>/tmp/fsinfo.log
echo ---du-s--- >>/tmp/fsinfo.log
du -s /tmp/fs >>/tmp/fsinfo.log

# ---- 2a.  ls -s OVER THE SAME TREE, because it counts the same block and nothing else in
# the suite asserts that it does.  The three sizes here must be the three du printed for the
# same objects, and the `total' must be their sum -- which is the whole point of moving all
# four programs to one unit in the same change.  It is here rather than in a test of its own
# because the tree is already built and the assertion is a diff of two adjacent blocks in one
# log.  (`ls' is also the only one of the four with no b6sim case at all: it reads a
# directory, which b6sim's host read(2) refuses.)
echo ---ls-s--- >>/tmp/fsinfo.log
ls -s /tmp/fs >>/tmp/fsinfo.log

# A non-directory argument, which prints nothing without -a -- du.1's first BUG, asserted
# rather than merely documented -- and then the same file with -a, which does print.
echo ---du-file--- >>/tmp/fsinfo.log
du /tmp/fs/one >>/tmp/fsinfo.log
du -a /tmp/fs/one >>/tmp/fsinfo.log

# The diagnostics.  A name that is not there, and du's exit status either way.
echo ---du-bad--- >>/tmp/fsinfo.log
du /tmp/no/such/thing >>/tmp/fsinfo.log 2>>/tmp/fsinfo.log

echo ---end--- >>/tmp/fsinfo.log
ls /tmp >>/tmp/fsinfo.log

# ---- 3.  THE WHOLE-IMAGE REPORTS, on the console, after a sync.  See the header for both
# halves of why.  df is run five ways and all five must agree on the same volume -- and
# TWO OF THEM DO NOT READ THE DISK AT ALL, which is what makes this the sharpest assertion
# available on statfs(2): the kernel's in-core superblock and the raw device's, one volume,
# one run, after a sync.
#
#   bare		the default list, which is the ROOT plus /etc/mtab.  Nothing is
#			mounted here and mtab is not on the image, so this is one row, and
#			it names /dev/md0 -- the block device, which is what the kernel
#			mounted.  It is a MOUNTED filesystem, so this row comes from
#			statfs(2) and no device is opened.  Nothing else exercises the
#			default list.
#   /tmp		a PATH and not a device: the same volume reached through st_dev,
#			the argument shape an ordinary user actually types.
#   /dev/rmd0		the raw device named outright -- the READ route, and the one this
#			whole section exists to hold the other two against
#   -i /dev/rmd0	the i-node columns, which no free-list walk could have produced:
#			they come from s_tinode and from (s_isize - (SUPERB+1)) * INOPB
#   -w /dev/rmd0	the same volume counted the OLD way, by walking the free list.
#			It must print the row -i and the bare run printed, and must say
#			nothing on stderr -- df complains there when the walk and s_tfree
#			disagree, and run-fsinfo.sh asserts the absence of that complaint.
sync
echo ---df--- >/dev/console
df >/dev/console
df /tmp >/dev/console
df /dev/rmd0 >/dev/console
df -i /dev/rmd0 >/dev/console
df -w /dev/rmd0 >/dev/console
echo ---quot--- >/dev/console
/etc/quot -f /dev/rmd0 >/dev/console
echo ---endquot--- >/dev/console

sync
echo fsinfo done >/dev/console
