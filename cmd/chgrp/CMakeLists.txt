# /bin/chgrp -- Unix v7 chgrp(1), built natively for the BESM-6.
#
# Like cmd/init and cmd/sh, this is a cmd/ subdirectory that is NOT a host tool: nothing
# here is compiled by the build machine.  b6_prog() (../../scripts/BesmCross.cmake) runs the
# b6* toolchain over chgrp.c, links it `crt0.o chgrp.o -lc -lruntime' against the libc built
# in ../../lib, and stages the result as build/rootfs/bin/chgrp -- the /bin/chgrp of the root
# filesystem the kernel mounts.  Added from inside the top-level libruntime guard, after
# lib/, and not with the other cmd/ subdirectories.
#
# NOT SETUID, for the reason ../chown/CMakeLists.txt gives: there is no chgrp(2), the group is
# the third argument of chown(2), and chown(2) is gated on suser().
#
# It reads /etc/group through getgrnam(3), which ../../etc/ stages onto the image.  Its manual
# page is ../chown/chown.1, v7 having documented the two programs together.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(chgrp DEST bin/chgrp SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/chgrp.c)
