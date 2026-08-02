# /etc/tartest -- the script kernel/test/tar drives, run as `sh /etc/tartest'.  Task C7.
#
# THIS FILE IS NOT IN root.manifest.  run-tar.sh grafts it onto a copy with `b6fsutil -a', as
# session.sh, files.sh, utils.sh, edit.sh, fsinfo.sh, dd.sh and mkfs.sh are grafted, so that
# editing the test does not rebuild the disk.  The path is /etc/tartest and not /etc/tar for
# mkfs.sh's reason, even though /bin/tar would not have collided: the convention is cheaper to
# keep than to remember the exception to.
#
# WHAT NEEDS A KERNEL HERE, and it is most of the program.  The eleven cmd_tar_* cases next
# door run these same bytes under b6sim and settle the header layout, the record arithmetic,
# the blocking factor and both directions of interchange with the host's tar -- in under a
# second and with no boot.  Four things they cannot touch at all:
#
#   * THE TREE WALK, which is the whole reason task C7 exists.  b6sim refuses to read a
#     directory descriptor (ls(1)'s, du(1)'s and find(1)'s limit), so `tar c' of a DIRECTORY
#     has never run anywhere but here.
#   * checkdir()'s exec of /bin/mkdir on extraction.  This system has no mkdir(2); the only
#     way a non-root tar gets a directory is to fork the setuid-root program, and under b6sim
#     that exec would be of the BUILD MACHINE's /bin/mkdir.
#   * THE OWNER, MODE AND MTIME a `tar x' restores.  chown(2) under b6sim is the developer's.
#   * /dev/rmd1, and physio()'s four alignment conditions on it -- the half of the port that
#     ../../cmd/df/README.md is about and that ../../cmd/TODO.md wrongly said worked already.
#
# TWO CHANNELS, fsinfo.sh's arrangement.  Everything the log holds is computed by tar, cmp,
# find or the shell from a corpus written in this same run, so run-tar.sh masks NOTHING.  The
# raw-device sections go to /dev/console, because a program writing to /dev/rmd1 must not have
# its report land on a filesystem the same run is about to hand to the host.
#
# NO `ls -l' AND NO `date' ANYWHERE, which is utils.sh's standing rule: the guest clock
# advances about two seconds over a whole run and anything it decides is a coin toss.  The one
# thing here that IS about time -- that `tar x' puts back the modification time it stored -- is
# asserted with find(1) -newer against a marker this script makes ITSELF, two seconds after the
# corpus, which is C1's rule about the only way to see a time change being a gap you put there.

echo tar start >/tmp/tar.log

# ---- 0.  THE CORPUS.  A tree with two levels, a file in each, a hard link, a name in
#          Cyrillic and a file big enough to cross several records.  Everything below is a
#          function of these and of nothing else.
mkdir /tmp/tree
mkdir /tmp/tree/sub
mkdir /tmp/tree/sub/deeper
echo one >/tmp/tree/one
echo two >/tmp/tree/sub/two
echo three >/tmp/tree/sub/deeper/three
echo привет мир >/tmp/tree/привет
cat /etc/termcap >/tmp/tree/big
chmod 600 /tmp/tree/one
chmod 755 /tmp/tree/sub/two
ln /tmp/tree/one /tmp/tree/hardlink

# The marker is made AFTER the corpus and after a gap, so that `find -newer' can tell an
# mtime that was restored from one that was invented at extraction time.  sleep 2 rather
# than 1: the clock here is coarse and a one-second gap is not reliably a gap.
sleep 2
touch /tmp/marker

# ---- 1.  THE TREE WALK.  One directory argument, and everything under it.
#
# THE NAME IS RELATIVE, and that is not a detail.  tar stores the path it was GIVEN, so
# `tar cf ... /tmp/tree' stores `/tmp/tree/one' and extracting it later writes back over the
# original wherever the extraction is done from -- v7 behaved this way and this port keeps it
# (a modern tar strips the leading slash and says so; tar.1's BUGS records the difference).
# `cd' first and name the tree relatively, which is the form tar.1's own pipeline idiom uses
# and the only form for which an extraction into a second directory means anything.
echo ---create--- >>/tmp/tar.log
cd /tmp
tar cf /tmp/t.tar tree
echo status $? >>/tmp/tar.log
cd /
tar tf /tmp/t.tar | sort >>/tmp/tar.log

# ---- 2.  EXTRACTION, which is where /bin/mkdir gets exec'd -- twice, for tree/sub and
#          tree/sub/deeper (checkdir() makes a directory only for a '/' it finds IN a name,
#          so the last component is never one).  cd first: the names are relative, which is
#          what lets the tree land somewhere other than on top of the corpus.
echo ---extract--- >>/tmp/tar.log
mkdir /tmp/back
cd /tmp/back
tar xf /tmp/t.tar
echo status $? >>/tmp/tar.log
cd /
find /tmp/back -print | sort >>/tmp/tar.log

# ---- 3.  WHAT CAME BACK IS WHAT WENT OUT.  cmp on every file, including the one whose name
#          is UTF-8 and the one that spans records.
echo ---cmp--- >>/tmp/tar.log
cmp /tmp/tree/one /tmp/back/tree/one
echo one $? >>/tmp/tar.log
cmp /tmp/tree/sub/two /tmp/back/tree/sub/two
echo two $? >>/tmp/tar.log
cmp /tmp/tree/sub/deeper/three /tmp/back/tree/sub/deeper/three
echo three $? >>/tmp/tar.log
cmp /tmp/tree/привет /tmp/back/tree/привет
echo cyr $? >>/tmp/tar.log
cmp /tmp/tree/big /tmp/back/tree/big
echo big $? >>/tmp/tar.log

# ---- 4.  THE MODIFICATION TIME CAME BACK.  /tmp/marker is newer than every file in the
#          corpus, so if tar restored the stored mtimes then nothing under /tmp/back is
#          newer than the marker and find prints nothing at all.  Had utime() not been
#          called, every extracted file would carry the time of the extraction, which is
#          later than the marker, and all six would be listed.
echo ---mtime--- >>/tmp/tar.log
find /tmp/back -newer /tmp/marker -print | sort >>/tmp/tar.log
echo endmtime >>/tmp/tar.log

# ---- 5.  ... AND `m' IS THE NEGATIVE THAT PROVES IT.  Same archive, same marker, `m' key:
#          now the times ARE the extraction's, so the files must be newer than the marker.
#          Without this the section above would pass just as well on a tar that restored
#          nothing and a clock that never moved.
#          AND IT NEEDS A GAP OF ITS OWN.  The guest clock advances about two seconds over a
#          whole run, so without this sleep the extraction lands in the same second as the
#          marker and `-newer' -- which is strictly newer -- reports nothing, and the section
#          would pass whether tar restored the times or not.  That is exactly the tautology
#          this section exists to rule out, and it happened on the first run.
echo ---mtime-m--- >>/tmp/tar.log
sleep 2
mkdir /tmp/nom
cd /tmp/nom
tar xmf /tmp/t.tar
cd /
find /tmp/nom -newer /tmp/marker -print | sort >>/tmp/tar.log
echo endmtimem >>/tmp/tar.log

# ---- 6.  THE HARD LINK.  tar stores the second name it meets as a link to the first and
#          extraction calls link(2).  The i-numbers are what say so and they are the host's
#          business -- run-tar.sh reads nlink out of the finished image -- but the archive's
#          own account of it is here.
#          `tf' AND NOT `tvf': dotable() prints "linked to" whatever the v key says -- only
#          the mode/size/DATE line in front of it is gated on vflag -- and a date is the one
#          thing that may not reach this log (utils.sh's standing rule).
echo ---link--- >>/tmp/tar.log
tar tf /tmp/t.tar | grep 'linked to' >>/tmp/tar.log

# ---- 7.  A TREE DEEPER THAN THE WALK ALLOWS.  putfile()'s frame is 239 words measured and
#          it holds one open descriptor per level, so MAXDEPTH is NOFILE-8; past it the
#          program must say which limit it hit rather than failing on an arbitrary open.
echo ---deep--- >>/tmp/tar.log
mkdir /tmp/d
mkdir /tmp/d/d
mkdir /tmp/d/d/d
mkdir /tmp/d/d/d/d
mkdir /tmp/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d/d/d/d/d
mkdir /tmp/d/d/d/d/d/d/d/d/d/d/d/d/d
echo bottom >/tmp/d/d/d/d/d/d/d/d/d/d/d/d/d/leaf
cd /tmp
tar cf /tmp/deep.tar d 2>>/tmp/tar.log
echo status $? >>/tmp/tar.log
cd /

# ---- 8.  REAL BINARIES, because everything above is text and a record boundary in the
#          middle of an a.out is where a byte would go missing unnoticed.
echo ---binary--- >>/tmp/tar.log
cd /
tar cf /tmp/bin.tar bin/echo bin/tar
mkdir /tmp/binback
cd /tmp/binback
tar xf /tmp/bin.tar
cd /
cmp /bin/echo /tmp/binback/bin/echo
echo echo $? >>/tmp/tar.log
cmp /bin/tar /tmp/binback/bin/tar
echo tar $? >>/tmp/tar.log

# ---- 9.  THE RAW DEVICE.  /dev/rmd1 is the scratch pack tar.ini attaches, and this is the
#          section nothing else in this tree can run: physio() wants a count that is a whole
#          number of BSIZE and a buffer on a 512-word boundary, so the blocking factor must
#          be a multiple of 6 and tbuf has to have been stepped forward at startup.
#          run-tar.sh converts that pack afterwards and reads the archive off it with the
#          HOST's tar, which is the strongest thing that can be said about these bytes.
echo ---raw--- >/dev/console
cd /tmp
tar cfb /dev/rmd1 6 tree
echo create status $? >/dev/console
cd /
tar tfb /dev/rmd1 6 | sort >/dev/console

# ---- 10.  AND THE THREE REFUSALS, which bracket the bound from the other side.  A factor
#           physio() would take as EFAULT is refused here with a diagnostic that names the
#           cause; `r' on a character special is refused because backtape()'s 512-byte lseek
#           is the fourth condition and the fourth condition is the silent one.
echo ---rawbad--- >/dev/console
tar cfb /dev/rmd1 5 /tmp/tree
echo five status $? >/dev/console
tar rfb /dev/rmd1 6 /tmp/tree/one
echo append status $? >/dev/console
tar rf /dev/rmd1 /tmp/tree/one
echo append1 status $? >/dev/console
echo ---endraw--- >/dev/console

echo ---end--- >>/tmp/tar.log
sync
echo tar done >/dev/console
