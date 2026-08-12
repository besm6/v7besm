# /etc/cron -- Unix v7 cron(8), built natively for the BESM-6.  Task C22 (../README.md);
# README.md beside this file is the account.
#
# /etc AND NOT /bin, which is v7's placement and cron.8.umm's SYNOPSIS: it is not a command a
# user types.  THE SECOND DAEMON ON THIS IMAGE, after ../update (task C20), and the one that
# finally gives ../atrun the periodic caller at.1 has always claimed it has.
#
# NOT SETUID, and it does not drop privilege either: v7's setuid(1) is gone, because
# /usr/lib/atrun needs suser() for the setuid(2) that gives a job its submitter's identity.
# README.md is the argument, and ../at/README.md is why a bit on atrun is not the answer.
#
# NO test/ HERE: ../README.md SS9 rules it out twice over -- a program that does not terminate
# cannot be a b6_progtest case, and every path this one names is the IMAGE's, so /usr/lib/crontab
# under b6sim would be the build machine's.  kernel/test/multi is the assertion, as it is for
# ../update and ../atrun.
#
# cron.8.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and ../../scripts/root.manifest
# has carried its stanza since before the program did.  This task adds the binary and the table.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/update/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(cron DEST etc/cron SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cron.c)

# The crontab, which is data and not C: copied, on the same footing as ../units's
# /usr/lib/units and ../bc's /usr/lib/lib.b.  A BUILD step rather than configure_file(), so
# that `rm -rf build/rootfs' restores it.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/crontab
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/crontab ${B6_ROOTFS}/usr/lib/crontab
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/crontab
    COMMENT "stage usr/lib/crontab" VERBATIM)

add_custom_target(rootfs_crontab ALL DEPENDS ${B6_ROOTFS}/usr/lib/crontab)
add_dependencies(rootfs rootfs_crontab)
