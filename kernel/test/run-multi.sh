#!/bin/sh
# Boot the kernel into multi-user, log TWO people in on the TWO Consul typewriters at once, and
# then ask the host what reached the disk.  Task 29c; multi.ini is the dialogue and its header is
# the account of it.
#
# Invoked by ctest as: run-multi.sh B6FSUTIL BESM6 SRCDIR TTYHOST, with the kernel test BUILD
# directory as the working directory -- where root.img and ../unix already are.
#
# THE FOURTH ARGUMENT IS WHAT MAKES THIS TEST DIFFERENT.  Consul 1 is bound to the simulator's own
# stdin/stdout by `attach tty25 console', which is what lets expect/send drive it with no tty on
# stdin under ctest.  Consul 2 has no such door: it is a mux line, and it is a terminal only while
# something is connected to it.  ttyhost is that something -- it binds the port, the simulator
# dials OUT to it as the .ini is parsed, and it writes down everything Consul 2 prints.  It types
# nothing; multi.ini does the typing, on both lines.  ttyhost.c's header says why round that way.
#
# THE READY FILE IS A HANDSHAKE, not a courtesy: ttyhost must own the port before the simulator
# tries to connect to it, and a background `&' does not promise that.
#
# WHAT THE HOST IS ASKED, and none of it is visible from either console:
#
#   the modes    /dev/console owned by root (0/1 -- /etc/passwd gives root gid 1, not 0) and
#                /dev/tty1 owned by guest (7/3), AT THE SAME TIME.  login chown()s its terminal
#                before it drops root and init's dfork() chown()s it back to 0/0 on every respawn,
#                so two different non-zero owners on two nodes at once cannot be produced by one
#                session however it is driven.  That line is the whole point of the test.
#                /dev/tty0 stays 0/0: a separate inode with no getty on it.  All three stay 622,
#                which is what dfork() sets and what nothing in this dialogue changes.
#
#   /etc/utmp    two live records.  A struct member of type char occupies a whole word here, so the
#                record is not a layout any host tool would parse -- but chars pack six to a word
#                with no gaps, so the names themselves are contiguous bytes and can simply be
#                looked for.
#
#   the fsck     two shells writing one filesystem from two terminals is the first concurrent
#                writer this port has had; a lost block or a bad link count would otherwise only
#                show up later, in some other test, for no visible reason.
#
#   the tty1     what Consul 2 printed, diffed against multi.expected.  The three lines the
#   transcript   simulator itself writes to a new connection are dropped: two are constant and the
#                third carries a wall clock and a port number.  CRs go too -- the line is in
#                cooked mode with CRMOD for all but the getty's own prompt.
#
# `-S' TAKES ITS DIRECTION FROM THE INPUT -- it looks for the SIMH zone mark and converts the
# other way if it finds one -- so the same option builds the container before the run and unpacks
# it after.  fsck (`-c') and the verbose listing (`-v -v') work on the flat image only.
#
# NOTHING IS GRAFTED ONTO THE IMAGE.  This test types every character it needs, because what it is
# testing is the thing that reads what is typed.
set -e
b6fsutil=$1
besm6=$2
srcdir=$3
ttyhost=$4

rm -rf multi.img root3085.disk multiafter.img multi.modes multi.out \
       multi.tty1 multi.tty1.raw multi.ready
cp root.img multi.img
"$b6fsutil" -S --volume=3085 multi.img root3085.disk

"$ttyhost" 4200 multi.tty1.raw multi.ready 600 &
host=$!
trap 'kill $host 2>/dev/null || true' EXIT

# Wait for the port to be bound before the simulator tries to dial it.
i=0
while [ ! -f multi.ready ]; do
    i=$((i + 1))
    if [ $i -gt 300 ]; then
        echo "run-multi.sh: ttyhost never bound the port" >&2
        exit 1
    fi
    sleep 0.1
done

"$besm6" "$srcdir/multi.ini"
wait $host

# What Consul 2 printed.  Three things come out of it.  The simulator's own connect banners:
# `Encoding is RAW' and the WRU line are constant, the third carries a ctime and a port.  The CRs:
# the line is in cooked mode with CRMOD from the moment getty execs login.  And control characters,
# mapped to `?' so that multi.expected stays a text file.
#
# HIGH BYTES ARE KEPT, and that is an assertion.  A raw Consul is a byte pipe and the guest supplies
# the character set (kernel/dev/sc.c): the first line of /etc/motd is Cyrillic, so if the transcript
# reads `БЭСМ-6' then every stage of the path -- ttyoutput, the clist, scstart, consul_print and the
# socket -- carried all eight bits.  It used to read `P?P-P!P?-6', which is that line with bit 8
# struck off each byte, and this filter used to flatten what was left.
sed -e '/^Encoding is /d' -e '/Break to sim> prompt character/d' -e '/ from 127\.0\.0\.1:/d' \
    multi.tty1.raw | tr -d '\r' | LC_ALL=C tr -c '\11\12\40-\176\200-\377' '?' >multi.tty1
diff -u "$srcdir/multi.expected" multi.tty1

"$b6fsutil" -S root3085.disk multiafter.img
"$b6fsutil" -c multiafter.img

# A projection of `b6fsutil -v -v': type, mode, uid/gid and the path, with the size and i-number
# dropped because neither is on trial.  Sorted on the path so the expectation does not encode the
# order of the walk.
"$b6fsutil" -v -v multiafter.img |
    awk '$NF == "/dev/console" || $NF == "/dev/tty0" || $NF == "/dev/tty1" || $NF == "/etc/utmp" { print $1, $2, $3, $NF }' |
    LC_ALL=C sort -k4,4 >multi.modes
diff -u "$srcdir/multi.modes" multi.modes

# Both utmp records live at once.  login writes at ttyslot()*sizeof(utmp), which /etc/ttys makes
# record 1 for console and record 2 for tty1, and init's rmut() blanks a record only when the
# shell on that line dies -- neither did.
mkdir multi.out
"$b6fsutil" -x multiafter.img multi.out
for who in root console guest tty1; do
    if ! LC_ALL=C grep -qa "$who" multi.out/etc/utmp; then
        echo "run-multi.sh: /etc/utmp has no record naming $who" >&2
        exit 1
    fi
done
