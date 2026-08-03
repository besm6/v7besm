# /bin/dmesg -- print the kernel message ring.  Task C8.
#
# IN /bin AND NOT /etc, which is where v7 kept it.  v7's dmesg opened /dev/kmem and needed
# root; this one asks kctl(2) for the value of msgbuf and msgbufp and opens nothing at all,
# so it is an ordinary user command and is placed as one.  That is a divergence and it is
# recorded here and in ../../root.manifest.
#
# NOT SETUID: kctl(2) is unprivileged by design (../../include/sys/kctl.h).
#
# dmesg.1 is NEW.  v7 shipped this program with no manual page anywhere in the tree -- the
# same situation yes(1) was in -- so the page beside it was written for this port and is not
# a correction of an upstream one.  Nothing installs it; read it with `nroff -man dmesg.1'.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(dmesg DEST bin/dmesg SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dmesg.c)

add_subdirectory(test)
