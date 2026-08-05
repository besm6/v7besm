# /etc/iostat -- Unix v7 iostat(1M), built natively for the BESM-6.  Task C8.
#
# /etc AND NOT /bin, as quot beside it: v7's page is section 1M and v7 put the binary in
# /etc.  Unlike quot this one needs no privilege at all -- kctl(2) is unprivileged and it
# opens no device -- so the placement is v7's convention rather than a permission.  The
# side effect is the same and is worth knowing: the two hard-coded `ls /bin' expectations
# (kernel/test/console.ini and kernel/test/session.expected) do not have to grow for it.
#
# NOT SETUID, and there is nothing for it to be setuid to.
#
# THE DISK COLUMNS ARE HERE, and there are two devices rather than v7's three: MD, which is
# all 64 units of kernel/dev/md.c together, and MB, the drums.  dk_busy is three bits wide
# because it is the low three of kernel/clock.c's dk_time subscript, so one slot per
# CONTROLLER is what fits; iostat.c's header is the account.  Each device gets %bsy, tps and
# wps, and cmd/vmstat prints one number per device instead -- the two halves of the same
# histogram, and neither is the other's summary.
#
# iostat.1m.umm is the manual page, corrected in place; manview(1) formats it on the image,
# and it is legible as it stands.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(iostat DEST etc/iostat SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/iostat.c)

add_subdirectory(test)
