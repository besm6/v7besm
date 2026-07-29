#!/bin/sh
# Boot the kernel into MULTI-USER, log in twice over the console, and then ask the host what
# reached the disk.  Task 29b; login.ini is the dialogue and its header is the account of it.
#
# Invoked by ctest as: run-login.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD directory
# as the working directory -- where root.img and ../unix already are.
#
# WHY THERE IS A HOST-SIDE HALF AT ALL, when the dialogue already asserts what the operator
# sees.  This test writes more of the root filesystem than any other in the suite, and none of
# what it writes is visible from the console:
#
#   /etc/utmp      does not exist on the shipped image.  cmd/init's merge() creat()s it on the
#                  way into multi-user and login writes a record into it -- the first time
#                  anything on this machine has.  That it exists afterwards is merge() saying
#                  so; nothing in the guest can be made to print it, since a struct member of
#                  type char occupies a whole word here and the record is not the byte layout
#                  any host tool would read.
#
#   /dev/console   is chown()ed by login to whoever logged in, and back to root by init's
#                  dfork() on every respawn.  The dialogue ends with guest still logged in, so
#                  the console must end up 7/3 -- and THAT LINE IS THE ONLY PROOF IN THE TREE
#                  that login's chown() ran, because it is the one privileged thing login does
#                  before it drops root and the one thing a `# ' prompt does not imply.
#
#   /dev/tty1      must come back UNCHANGED at 0/0, which is dfork() having chown()ed and
#                  chmod()ed the second Consul's line and no login having happened there.
#
# And the fsck is the structural half: init and login are the first writers of this filesystem
# that are not a shell running a command, and a lost block or a bad link count would otherwise
# only show up as a later test failing for no reason.
#
# `-S' TAKES ITS DIRECTION FROM THE INPUT -- it looks for the SIMH zone mark and converts the
# other way if it finds one -- so the same option builds the container before the run and
# unpacks it after.  fsck (`-c') and the verbose listing (`-v -v') work on the flat image only.
#
# NOTHING IS GRAFTED ONTO THE IMAGE.  session, files, libtest and utils each put a driver
# script on their copy with `b6fsutil -a'; this test types every character it needs, because
# what it is testing is the thing that reads what is typed.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf login.img root3084.disk loginafter.img login.modes
cp root.img login.img
"$b6fsutil" -S --volume=3084 login.img root3084.disk

"$besm6" "$srcdir/login.ini"

"$b6fsutil" -S root3084.disk loginafter.img
"$b6fsutil" -c loginafter.img

# The same projection run-files.sh uses over `b6fsutil -v -v': type, mode, uid/gid and the
# path, with the size and i-number dropped because neither is what is on trial -- /etc/utmp's
# size is a word count this machine chose and its i-number moves whenever anything earlier in
# the manifest changes.  Sorted on the path so the expectation does not encode the order of
# the walk.  Four paths, and each says something different; see the header.
"$b6fsutil" -v -v loginafter.img |
    awk '$NF == "/dev/console" || $NF == "/dev/tty0" || $NF == "/dev/tty1" || $NF == "/etc/utmp" { print $1, $2, $3, $NF }' |
    LC_ALL=C sort -k4,4 >login.modes
diff -u "$srcdir/login.modes" login.modes
