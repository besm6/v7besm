# /etc/dd -- the script kernel/test/dd drives, run as `sh /etc/dd'.  Task C4b.
#
# THIS FILE IS NOT IN root.manifest.  run-dd.sh grafts it onto a copy of the image with
# `b6fsutil -a', as session.sh, files.sh, utils.sh, edit.sh and fsinfo.sh are grafted, so
# that editing the test does not rebuild the disk.
#
# WHAT NEEDS A KERNEL HERE, and it is the whole reason this test exists: the twelve cmd_dd_*
# cases next door run the same bytes under b6sim in a tenth of a second each and cover the
# command language completely -- but b6sim's read(2) is the host's, so none of the four
# alignment conditions physio() and mdstrategy() impose exists there.  Everything below is
# about a RAW DEVICE, or about chaining two dd runs, which one b6sim case cannot do.
#
# THE RAW READ COMES FIRST, before this script writes anything.  run-dd.sh compares what dd
# pulled off /dev/rmd0 against the same offset in the image AS IT WAS HANDED TO THE BOOT, so
# nothing may have rewritten those blocks in between.  Block 100 is chosen for that: the data
# area of this image runs from block 33 to about 530 and is full of program text that nobody
# rewrites, while everything this run allocates comes off the free list well above it.  If
# that ever stops being true run-dd.sh says so in as many words rather than failing obscurely.
#
# TWO WAYS OF ASKING FOR THE SAME BLOCKS, because they are different code paths.  The first
# names no bs= at all and so goes through the per-byte conversion loop with the DEFAULT
# record size -- which is the whole point of this port's one divergence, a default of BSIZE
# where v7's was 512: with 512 this line could not reach the disk, 512 not being a whole
# number of words.  The second names bs=3072 and takes the fast path, where the input buffer
# is written out whole and no byte is ever looked at.
#
# STDOUT AND STDERR ARE NEVER MIXED IN ONE COMMAND, which is utils.sh's rule: dd's payload
# always goes to a file through of=, so only its report reaches the log.

echo dd start >/tmp/dd.log

echo ---raw--- >>/tmp/dd.log
dd if=/dev/rmd0 of=/tmp/blkA skip=100 count=2 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log
dd if=/dev/rmd0 of=/tmp/blkB skip=100 count=2 bs=3072 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log
ls -s /tmp/blkA /tmp/blkB >>/tmp/dd.log

# The two ways of getting the count wrong, and they fail differently.  bs=512 is not a whole
# number of 48-bit words, so physio() rejects it before the driver sees it; bs=6 is one word,
# which physio() passes, and mdstrategy() then rejects because it is not a whole half-zone.
# Both are the second of cmd/df/README.md's four conditions, enforced in two places.
echo ---misaligned--- >>/tmp/dd.log
dd if=/dev/rmd0 of=/tmp/bad1 bs=512 count=1 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log
dd if=/dev/rmd0 of=/tmp/bad2 bs=6 count=1 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log

# The round trip, which needs dd twice and so cannot be a b6sim case.  /etc/motd is the input
# because it is 365 bytes of UTF-8: the two tables are inverses over all 256 byte values, not
# just the ASCII half, and this is where that is asserted.  run-dd.sh cmp's the result against
# the original OUT OF THE SAME EXTRACTED IMAGE, so there is no fixture to keep in step.
echo ---ebcdic--- >>/tmp/dd.log
dd if=/etc/motd of=/tmp/motd.e conv=ebcdic 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log
dd if=/tmp/motd.e of=/tmp/motd.a conv=ascii 2>>/tmp/dd.log
echo status $? >>/tmp/dd.log

echo ---end--- >>/tmp/dd.log
ls /tmp >>/tmp/dd.log

sync
echo dd done >/dev/console
