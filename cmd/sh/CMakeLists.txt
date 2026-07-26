# /bin/sh -- the Unix v7 Bourne shell, built natively for the BESM-6.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the header
# dependency is the whole tree at once, as it is in lib/, kernel/ and cmd/init/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

# -I. so that defs.h's `#include "ctype.h"' finds the shell's own table header rather
# than the C11 <ctype.h> in the system tree.  It must be absolute: CFLAGS goes onto the
# b6cc command line as written, and the compiler does not run in this directory.
b6_prog(sh DEST bin/sh PURE
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES args.c blok.c builtin.c cmd.c ctype.c error.c expand.c fault.c
                glob.c io.c macro.c main.c msg.c name.c print.c service.c
                setbrk.c stak.c string.c word.c xec.c)

add_subdirectory(test)
