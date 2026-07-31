# mount's b6sim cases -- task C4f.
#
# THIS IS THE SHORTEST test/ DIRECTORY IN cmd/, AND THE REASON IS THE POINT.  Every other
# program of task C4 reads or writes a FILESYSTEM, and a flat b6fsutil image is an ordinary
# host file, so ../../df/test's idea carried the whole task: a fixture image, a real free
# list, a real i-list, a tenth of a second per case.  mount reads no filesystem.  What it
# does is call mount(2) -- and b6sim services every system call on the HOST (scripts/
# run-prog-test.sh), so a case that got that far would be asking the build machine to graft
# a filesystem onto itself.  There is nothing to fix here and nothing to work around: the
# whole of what these two programs do lives under the booted kernel, in kernel/test/mount.
#
# So what is left, and all that is left, is the argument handling BEFORE mount(2) -- and it
# is only assertable because mount.c settles every argument before it opens /etc/mtab.  That
# ordering is a divergence from v7, which reads the table first, and mount.c's header says
# it was made for this.  Without it these three cases would read the build machine's
# /etc/mtab: absent on a Mac, a list of that machine's own filesystems on Linux, and in
# neither case anything a checked-in .expected could name.  ../../df/README.md records the
# same hazard about getpwent(3), which is the general form of it.
#
# WHAT THAT MEANS FOR A GREEN RUN: `ctest -L cmd' says nothing whatever about mount(2).  It
# is kernel/test/mount that holds these two programs, and it is the only thing that does.

# DEST, because mount is /etc/mount and not /bin/mount -- see ../CMakeLists.txt.
b6_progtest(mount usage   DEST etc/mount) # a special file and no mount point
b6_progtest(mount toomany DEST etc/mount) # a fifth argument, which v7 accepts and ignores
b6_progtest(mount badflag DEST etc/mount) # v7 reads ANY third argument as -r
b6_progtest(mount longdir DEST etc/mount) # a mount point /etc/mtab could not hold
