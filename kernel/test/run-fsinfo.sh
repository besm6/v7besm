#!/bin/sh
# The guest measuring its own filesystem -- the host half of kernel/test/fsinfo.  Task C4a.
#
# Invoked by ctest as:
#
#	run-fsinfo.sh B6FSUTIL BESM6 SRCDIR
#
# and it is run-edit.sh's shape: copy the pristine image, graft the guest script onto the
# copy, convert it to a SIMH container at this test's own volume number, boot, convert what
# comes back, fsck it, extract it, and ask the host the questions the guest cannot answer
# about itself.
#
# FOUR ORACLES, AND TWO OF THEM RECOMPUTE RATHER THAN REMEMBER.
#
#   1.  `b6fsutil -c' -- five passes over the inodes, the free list and the directory tree.
#       Structural: what the run wrote is still a filesystem.  Nearly free, and it is the
#       reason every writing test here ends this way.
#
#   2.  df, AGAINST THE HOST'S OWN FREE-LIST WALK.  `b6fsutil -c -v' ends a clean run with
#       `N blocks in use, M free', and M is counted out of the bitmap pass 4 fills by
#       draining the free list exactly as the kernel's alloc() drains it -- which is also
#       exactly what cmd/df does on the guest.  Two independent implementations of one walk,
#       over the same disk, and the test is that they agree.  cmd/fsutil/test/check_test.cpp
#       pins the format of that line, since this script now depends on it.
#
#   3.  quot, AGAINST AN awk OVER `b6fsutil -v -v'.  This is the run-files.sh modes oracle
#       one step on: rather than a checked-in table, the host RECOMPUTES the per-uid sums
#       from the finished image and diffs quot's report against them.  Nothing here needs
#       updating when a program joins /bin, which a checked-in table would.
#
#       Two things the awk has to get right, and both are why this is worth doing at all:
#       it DEDUPES BY I-NUMBER, because the host walk visits a hard link once per name while
#       quot's i-list sweep counts an inode once; and it treats a device node as size zero,
#       because `-v -v' prints `dev major,minor' where a size would go and quot divides
#       di_size, which is 0 for a device.
#
#   4.  The log, diffed against fsinfo.expected: du's three forms over a tree the guest
#       built, its two diagnostics, and a closed listing of /tmp.  NOTHING IS MASKED -- du
#       prints arithmetic on sizes this test chose, not times or sizes the clock decided --
#       which is run-edit.sh's property and not run-utils.sh's or run-files.sh's.
#
# WHY THE CONSOLE IS CAPTURED, which no test here did before.  Oracles 2 and 3 are about the
# WHOLE image, so their numbers move whenever anything is added to it; and both programs
# would be measuring the file they were writing their answer into, so a report written to
# disk disagrees with the disk by its own size.  The guest prints them to the console
# instead, between markers, and this is where they are read back.  fsinfo.sh's header is the
# long form of both halves.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf fsinfo.img root3087.disk fsinfoafter.img fsinfo.out fsinfo0.drum fsinfo1.drum \
       fsinfo.console fsinfo.check fsinfo.modes fsinfo.quot.guest fsinfo.quot.host
cp root.img fsinfo.img
"$b6fsutil" -a fsinfo.img /etc/fsinfo "$srcdir/fsinfo.sh"

"$b6fsutil" -S --volume=3087 fsinfo.img root3087.disk

# The transcript is an oracle here, so it is kept -- but it is also where a failing run says
# what went wrong, so it is echoed either way.
if ! "$besm6" "$srcdir/fsinfo.ini" >fsinfo.console 2>&1; then
    cat fsinfo.console >&2
    exit 1
fi
cat fsinfo.console

"$b6fsutil" -S root3087.disk fsinfoafter.img

# Oracle 1, and oracle 2's input: -v so that the run ends with the block accounting.
"$b6fsutil" -c -v fsinfoafter.img | tee fsinfo.check

mkdir fsinfo.out
"$b6fsutil" -x fsinfoafter.img fsinfo.out

#
# Oracle 2.  The guest printed the same number twice -- once from its default device list
# and once from an explicit /dev/rmd0 -- and both must equal the host's.
#
# THE TWO SIDES ARE IN DIFFERENT UNITS, deliberately.  b6fsutil counts FILESYSTEM blocks --
# it is an inspector, and the rest of its report is on-disk block numbers -- while df(1M)
# reports the 1024-byte block cmd/README.md SS4 describes, KBPB of them per filesystem block.
# Converting here, once, in the place that already knows both sides, is better than making
# b6fsutil mix units inside one report.
KBPB=3
blocks=$(sed -n 's/^[0-9]* blocks in use, \([0-9]*\) free$/\1/p' fsinfo.check)
if [ -z "$blocks" ]; then
    echo "run-fsinfo.sh: b6fsutil -c -v printed no block accounting -- see fsinfo.check" >&2
    exit 1
fi
want=$((blocks * KBPB))
# The console has CR line endings; strip them before matching.
got=$(tr -d '\r' <fsinfo.console |
      sed -n '/^---df---$/,/^---quot---$/s/^\/dev\/rmd0 \([0-9]*\)$/\1/p')
if [ -z "$got" ]; then
    echo "run-fsinfo.sh: no df output between the markers -- see fsinfo.console" >&2
    exit 1
fi
for n in $got; do
    if [ "$n" != "$want" ]; then
        echo "run-fsinfo.sh: df says $n free 1K-blocks, the host's free-list walk says" \
             "$want ($blocks filesystem blocks x $KBPB)" >&2
        exit 1
    fi
done
echo "run-fsinfo.sh: df and b6fsutil agree on $want free 1K-blocks ($blocks filesystem blocks) from $(echo $got | wc -w | tr -d " ") df runs"

#
# Oracle 3.  quot -f prints `blocks<TAB>files<TAB>name', sorted by blocks; the host's table
# is sorted by uid, so both sides are reduced to `uid blocks files' and sorted the same way.
# A name is mapped back through the IMAGE's /etc/passwd -- not the build machine's, which is
# the very thing cmd/quot/test/CMakeLists.txt says makes the per-uid report untestable under
# b6sim -- and quot's `#nnn' fallback needs no map at all.
#
tr -d '\r' <fsinfo.console |
    sed -n '/^---quot---$/,/^---endquot---$/p' |
    awk -v pw=fsinfo.out/etc/passwd '
        BEGIN {
            while ((getline line < pw) > 0) {
                n = split(line, f, ":")
                if (n >= 3)
                    uid[f[1]] = f[3]
            }
        }
        # quot separates its columns with TABS, and by the time they reach here they are
        # SPACES: the console runs with XTABS (kernel/dev/tty.c), so the driver expands a
        # tab on the way out and a transcript cannot be split on one.  Hence the default
        # field separator, and hence this filter -- the two markers and the `<device>:"
        # line are one field each, a report line is three.
        NF != 3 { next }
        {
            name = $3
            if (substr(name, 1, 1) == "#")
                u = substr(name, 2)
            else if (name in uid)
                u = uid[name]
            else {
                print "quot named [" name "] and the image /etc/passwd does not" > "/dev/stderr"
                exit 1
            }
            print u, $1, $2
        }' | sort -n >fsinfo.quot.guest

"$b6fsutil" -v -v fsinfoafter.img |
    awk '
        # `-v -v" IS TWO REPORTS: the superblock summary, then the tree.  Only the second is
        # wanted, and without this guard the header lines parse as inodes -- `Magic:
        # 0123456701234" has no third field, so it becomes a uid-0 object of size 0 with an
        # empty i-number, and adds one to root`s file count.  That phantom is what made the
        # host and the guest disagree by exactly one file the first time this ran.
        #
        # A tree line is <type> <mode> <uid>/<gid> <size | dev M,N> ino <n> nlink <k>  <path>
        $1 !~ /^[-dcb?]$/ { next }
        $3 !~ /^[0-9]+\/[0-9]+$/ { next }
        {
            split($3, o, "/")
            if ($4 == "dev") { size = 0; ino = $7 } else { size = $4; ino = $6 }
            if (ino in seen)                    # a hard link, visited once per NAME here
                next                            # and once per INODE by quot
            seen[ino] = 1
            u = o[1] + 0
            if (u >= 300)                       # quot ignores uids at or above NUID
                next
            # ceil into FILESYSTEM blocks, then out into the reported unit -- the same
            # two steps, in the same order, that quot makes.  3072 is the block on the
            # disk and 3 is KBPB; neither is the other.  (No apostrophes in here: the awk
            # program is single-quoted and one would end it.)
            blocks[u] += int((size + 3071) / 3072) * 3
            files[u]++
        }
        END {
            for (u in files)
                if (blocks[u] > 0)              # quot stops at the first zero-block user
                    print u, blocks[u], files[u]
        }' | sort -n >fsinfo.quot.host

diff -u fsinfo.quot.host fsinfo.quot.guest
echo "run-fsinfo.sh: quot agrees with the host, per uid, on $(wc -l <fsinfo.quot.host | tr -d " ") owners"

# Oracle 4.
diff -u "$srcdir/fsinfo.expected" fsinfo.out/tmp/fsinfo.log
