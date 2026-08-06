# /etc/edit -- the script kernel/test/edit drives, run as `sh /etc/edit'.  Task C3.
#
# THIS FILE IS NOT IN root.manifest.  run-edit.sh grafts it onto a copy of the image with
# `b6fsutil -a', as session.sh, files.sh and utils.sh are grafted, so that editing the test
# does not rebuild the disk.
#
# WHAT IS ASSERTED HERE AND NOT NEXT DOOR.  cmd/ed/test/ has nineteen b6sim cases over the
# command language and the regex engine, and they are the same staged bytes -- so this script
# deliberately does NOT repeat them.  What it covers is the five things b6sim cannot do: a
# /tmp that belongs to this machine rather than the build host, a `!' that needs a real
# /bin/sh to exec, a filesystem that can be fscked afterwards, files big enough to work ed's
# own 512-byte temp-file pager against a real disk, and the one file on this image that v7's
# ed could not have opened at all.
#
# AN EXPECTATION FILE MAY RECORD ONLY WHAT IS REPRODUCIBLE, which is session.sh's rule and
# run-files.sh's at length.  So the log carries names, line counts, byte counts and
# diagnostics -- all of which ed prints exactly -- and no times, no sizes from ls and no
# ordering that a one-second clock could decide.  ed is unusually well suited to it: `w'
# prints a byte count and `=' a line number, and both are arithmetic rather than weather.
#
# HERE-DOCUMENTS, AND TWO RULES ABOUT THEM.  ed reads its command language on standard input,
# and a here-document is how a script feeds one.  cmd/sh/test/heredoc covers the mechanism
# under b6sim, but NO HERE-DOCUMENT HAD EVER RUN UNDER THE BOOTED KERNEL before this script:
# the shell writes one to /tmp/sh-<pid><serial> and unlinks it at once (cmd/sh/io.c), which is
# a creat, an open and an unlink in the directory this test is about anyway.  It worked first
# time.  The two rules:
#
#   * THE DELIMITER IS QUOTED -- `<<\!' and not `<<!' -- so that the shell leaves the document
#     alone.  ed's address language is full of `$', and an unquoted document would have the
#     shell substitute every one of them for an empty string.
#
#   * ed MUST NOT BE GIVEN A FILE THAT DOES NOT EXIST.  The startup read fails, error() prints
#     `?name' and then calls lseek(0, 0, SEEK_END) to throw away the rest of the script -- and
#     a here-document is a FILE, so the seek succeeds and takes the whole document with it.
#     So a file is authored by an ed with no argument at all and a `w name', which is the more
#     honest spelling of writing something from nothing in any case.  cmd/ed/test/nofile is
#     where the `?name' path is asserted, on one line, with nothing after it to lose.
#
# The first line below is a plain echo, so that a here-document failure and a script that
# never started look different in the log.

echo edit start >/tmp/edit.log

# ---- 1.  AUTHOR A FILE FROM NOTHING.  The assertion the whole task exists for: before ed,
# every byte on this image was written on the build host and staged in by b6fsutil.  An empty
# buffer, three lines typed into it -- two of them Cyrillic -- and a `w' that says how many
# bytes reached the disk.
ed >>/tmp/edit.log <<\!
a
Строка первая
line two
Строка третья
.
w /tmp/poem
q
!
cat /tmp/poem >>/tmp/edit.log

# ---- 2.  EDIT A FILE ANOTHER PROGRAM WROTE, AND THE REVERSE.  cat makes it, ed changes it,
# ed changes it back, cat reads it -- which is cmd/TODO.md's own prescription for this task.
# The `$=' before and after says the line count did not move.
cat /etc/motd >/tmp/m1
ed /tmp/m1 >>/tmp/edit.log <<\!
$=
1,$s/^/| /
w
q
!
ed /tmp/m1 >>/tmp/edit.log <<\!
/Cyrillic/p
1,$s/^| //
$=
w
q
!
cat /tmp/m1 >>/tmp/edit.log

# ---- 3.  THE FILE v7's ed COULD NOT HAVE OPENED.  /etc/motd is 365 bytes of UTF-8, and
# v7's getfile() calls error() on any byte with 0200 set -- so `ed /etc/motd' was `?' and
# nothing else, on a file cat has always printed.  Read it and write it straight back out;
# run-edit.sh does the cmp on the host, so the assertion is byte-for-byte identity and needs
# no fixture checked in anywhere.
ed /etc/motd >>/tmp/edit.log <<\!
$=
w /tmp/motd.ed
q
!

# ---- 4.  THREE COPIES OF IT, 1,095 bytes, so that lines land on both sides of ed's own
# 512-byte temp blocks and getblock()/blkio() have to splice a line across two of them
# against a real disk rather than the build host's /tmp.  Then a global substitute over the
# lot, into Cyrillic, and the same identity cmp on the untouched copy.
cat /etc/motd /etc/motd /etc/motd >/tmp/three
ed /tmp/three >>/tmp/edit.log <<\!
$=
w /tmp/three.ed
g/UNIX/s/UNIX/ЮНИКС/
$=
w /tmp/three.ed2
q
!

# ---- 5.  EIGHT BITS, on the machine, against the same engine the b6sim cases drove.  The
# two harnesses running the same bytes is the point: the libc suite disagreeing between them
# is what found two bugs nothing else had exercised (../README.md).
ed /tmp/poem >>/tmp/edit.log <<\!
/Строка/p
1s/первая/ПЕРВАЯ/p
$s/третья/&-&/p
1,$p
w
q
!

# ---- 6.  THE SHELL ESCAPE, which is the one thing here that needs another process: ed forks
# and execs `/bin/sh -t', and that shell reads one command from the descriptor ed is reading
# -- the oneflg path in cmd/sh (defs.h).  Nothing under b6sim can reach it, there being no
# /bin/sh on the build machine.
ed /tmp/poem >>/tmp/edit.log <<\!
!echo shell escape ok
q
!

# ---- 7.  /tmp, CLOSED.  ed unlinks its temp file on a clean quit and the shell unlinks each
# here-document as it opens it, so this listing is exactly what the run meant to leave.  A
# leaked /tmp/e<pid> or /tmp/sh-<pid> shows up here and nowhere else.
ls /tmp >>/tmp/edit.log

sync
echo edit done >/dev/console
