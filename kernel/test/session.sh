: /etc/session -- the script kernel/test/session drives, run as sh /etc/session.
:
: THIS FILE IS NOT IN root.manifest and is not part of the shipped root filesystem.
: run-session.sh grafts it onto its own converted copy of the image with b6fsutil -a,
: which is what keeps a test fixture out of the tree the kernel really boots.
:
: THE V7 SHELL HAS NO COMMENT CHARACTER -- that arrived with System III, so a line
: beginning with a hash is a COMMAND, and not found.  A colon is the null built-in and is
: what stands in for one, but its words are still PARSED, so no backquote, no apostrophe,
: no parenthesis, no dollar, no semicolon and no redirection may appear even in such a line.
: /etc/rc says the same thing at greater length.  This file learned it twice over --
: one draft put parentheses round a section number and the shell went looking for a
: subshell, the next put a semicolon mid-sentence and it ran the rest of the line.
:
: WHAT THIS PROVES, and why each line is the one it is.  Everything above this test stops
: at reading -- boot execs, console types.  Here the kernel WRITES -- creat, write through
: the buffer cache, an inode grown past its direct blocks, a directory extended -- and
: then sync forces it all out and the host fscks what actually reached the platter.
:
: /tmp/session.log is built up by three commands in turn, so it exercises append as well
: as create, and it is what run-session.sh diffs against session.expected.  The names it
: collects are all the test needs to see.  Nothing here prints a time or a size, because an
: expectation file may record only what is reproducible.

echo hello >/tmp/session.log
pwd >>/tmp/session.log
ls /bin >>/tmp/session.log

: /tmp/big is /bin/ls copied.  ls is about 33 KB, and an inode here holds 6 direct block
: addresses of 3072 bytes each -- NADDR in sys/param.h -- so a file this size is the
: smallest one that must reach through the SINGLE INDIRECT BLOCK -- bmap allocating it, and
: fsck having to follow it afterwards.  run-session.sh compares it byte for byte with the
: /bin/ls it was copied from, out of the same extracted image, so a cat or a bmap that
: dropped or duplicated a block cannot pass.

cat /bin/ls >/tmp/big
ls /tmp >>/tmp/session.log

: sync, and it must be the LAST thing that writes.  There is no update daemon on this
: system -- nothing flushes the cache on a timer -- so without this the delayed-write
: buffers holding everything above would still be in core when the simulator stops.  The
: echo that follows execs one more program and so dirties an access time that never
: reaches the disk.  That is not an inconsistency, and fsck does not care.

sync
echo session done >/dev/console
