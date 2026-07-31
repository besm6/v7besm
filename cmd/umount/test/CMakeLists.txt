# umount's b6sim cases -- task C4f.  ../../mount/test/CMakeLists.txt is the account of why
# there are so few of them, and it applies here twice over: umount(2) is the host's under
# b6sim, and so is the sync(2) on the line above it.
#
# Which is why NO CASE HERE MAY REACH sync(): umount.c canonicalizes its argument first and
# both cases below stop there.  The order is v7's own -- it checks argc before anything --
# and the name check was put in front of the sync deliberately.

# DEST, because umount is /etc/umount and not /bin/umount -- see ../CMakeLists.txt.
b6_progtest(umount usage    DEST etc/umount) # no argument at all
b6_progtest(umount toomany  DEST etc/umount) # v7 takes exactly one and says `arg count'
b6_progtest(umount longname DEST etc/umount) # a name /etc/mtab could not hold
