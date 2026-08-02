#!/bin/sh
# Boot the kernel into multi-user, log guest in, change its password, become root, change
# group, log out and log back in with the new password -- then ask the host what reached the
# disk.  Task C6e; accounts.ini is the dialogue and its header is the account of it.
#
# Invoked by ctest as: run-accounts.sh B6FSUTIL BESM6 SRCDIR, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# WHAT THE HOST IS ASKED, and none of it is visible from either console:
#
#   /etc/passwd  guest's second field was EMPTY in the staged file and is thirteen characters
#                now -- the width crypt(3) produces.  The value itself cannot be asserted: the
#                salt is time() + getpid(), so it differs on every run, which is why this is a
#                check on the SHAPE.  Every other line must be byte-for-byte what etc/passwd
#                staged, because passwd rewrites the whole file through getpwent() and a field
#                it mangled on the way past would show up nowhere else.
#
#   /etc/ptmp    GONE.  passwd creates it 0600, copies through it and unlinks it; a leftover is
#                what makes every later run say `Temporary file busy', and it is the failure
#                the program's own error paths are known to leave behind (cmd/passwd/passwd.c).
#
#   the modes    /tmp/before owned 7/3 and /tmp/after owned 7/1, which is newgrp's ONLY visible
#                effect and cannot be asserted in the guest at all -- `ls -l' prints the owner
#                and what changed is the group.  Both files were created by the same user in
#                the same directory seconds apart; the group is the whole difference.
#                /dev/console 7/3 says login chown()ed the terminal, as run-login.sh asserts.
#
#                THE MODE IS 664 AND NOT 644: cmd/sh's `>' creates with 0666 and cmd/login
#                does umask(02) for every session it starts (login.c), so the group keeps its
#                write bit and only `other' loses one.  Worth knowing before reading 664 as a
#                surprise -- it is the one number here that neither program chose.
#
#   the fsck     passwd rewrote a file in /etc and two more were created in /tmp, all through
#                the buffer cache, and the run ends on a sync.
#
# `-S' TAKES ITS DIRECTION FROM THE INPUT -- it looks for the SIMH zone mark and converts the
# other way if it finds one -- so the same option builds the container before the run and
# unpacks it after.  fsck (`-c') and the verbose listing (`-v -v') work on the flat image only.
#
# NOTHING IS GRAFTED ONTO THE IMAGE.  Like login and multi, this test types every character it
# needs: passwd, su and newgrp all prompt through getpass(3), which opens /dev/tty, so a script
# on the image could not answer them however it was written.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3

rm -rf accounts.img root3098.disk accountsafter.img accounts.modes accounts.out
cp root.img accounts.img
"$b6fsutil" -S --volume=3098 accounts.img root3098.disk

"$besm6" "$srcdir/accounts.ini"

"$b6fsutil" -S root3098.disk accountsafter.img
"$b6fsutil" -c accountsafter.img

# run-login.sh's projection of `b6fsutil -v -v': type, mode, uid/gid and the path, with the
# size and i-number dropped because neither is on trial.  Sorted on the path so the
# expectation does not encode the order of the walk.
"$b6fsutil" -v -v accountsafter.img |
    awk '$NF == "/dev/console" || $NF == "/tmp/before" || $NF == "/tmp/after" { print $1, $2, $3, $NF }' |
    LC_ALL=C sort -k4,4 >accounts.modes
diff -u "$srcdir/accounts.modes" accounts.modes

mkdir accounts.out
"$b6fsutil" -x accountsafter.img accounts.out

if [ -f accounts.out/etc/ptmp ]; then
    echo "run-accounts.sh: /etc/ptmp was left behind -- passwd did not unlink it" >&2
    exit 1
fi

# guest's hash: thirteen characters where the staged file had none.  Nothing here knows what
# they are, and nothing can: the salt is the clock.
guest=$(LC_ALL=C sed -n 's/^guest:\([^:]*\):.*/\1/p' accounts.out/etc/passwd)
case "$guest" in
    ?????????????) ;;
    *)
        echo "run-accounts.sh: guest's password field is '$guest', wanted thirteen characters" >&2
        exit 1
        ;;
esac

# ...and every other line survived the rewrite untouched.  passwd copies the whole file through
# getpwent() and prints it back field by field, so a field it dropped or reordered would be
# invisible to the dialogue and visible only here.
LC_ALL=C sed 's/^guest:[^:]*:/guest::/' accounts.out/etc/passwd >accounts.passwd
diff -u "$srcdir/../../etc/passwd" accounts.passwd
