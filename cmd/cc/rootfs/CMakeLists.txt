# /usr/bin/cc -- cmd/cc built a SECOND time, natively for the BESM-6 (task C9e),
# and the last program of C9: the driver that runs the chain the other nine are.
#
# The source is ../cc.c, the very one the host tool b6cc is built from.  There is
# no copy and no fork; what the target changes is the PATH PROFILE at the head of
# that file, keyed on the `besm6' macro b6cpp always predefines exactly as the size
# profiles of cmd/cpp, cmd/as and cmd/ld are -- /usr/bin instead of ~/.local/bin
# and /usr/local/bin, /usr/include and /lib instead of share/besm6 under either,
# and no `b6' prefix on a sub-tool's name.  ../README.md has the account, and
# ../test/'s GoogleTest suite covers the HOST build only.
#
# THE SIMPLEST OF THE TEN, and in two ways nothing before it was.  It is ONE source
# file with no private header, so the b6_obj dependency below is the system tree and
# nothing else; and it is the FIRST native program that does not include
# cmd/libaout/sources.cmake, because a driver reads no a.out -- it only ever hands
# file names to programs that do.
#
# WHAT DID CHANGE IS IN LIBC, as it was for cmd/ar in C9d, and this time three
# times over: strdup(), which this libc had not got at all and which cc calls nine
# times; mkstemps(), because every stage of the pipeline is a temporary whose
# SUFFIX says what is in it and mkstemp() cannot fill a run of `X' that is not at
# the end; and atexit(), which <stdlib.h> had declared since these headers were
# written and nothing had ever implemented.  Putting all three in the library
# rather than behind an `#if besm6' here left the spawn code and the whole of
# main() character-identical in both builds.  lib/libc/gen/{strdup,mkstemp,atexit}.c
# and ../README.md have the decisions.
#
# THE ONE THING THIS PROGRAM CANNOT DO IS COMPILE C.  b6parse, b6lower and
# b6codegen are the external c-compiler repository's, which this one cannot add a
# b6_prog() to, so `cc foo.c' on the machine stops after the preprocessor and says
# so.  -E, -c on a .s or a .S, and a link all work.  ../README.md, "It cannot
# compile C".
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# tool is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard that includes scripts/BesmCross.cmake -- it has to be, since BesmCross
# resolves the cross toolchain out of those very targets.  So b6_prog() does not
# exist yet at that point, and CMake will not take one directory twice.  This one
# is added from inside the guard, after lib/, exactly as cmd/ld/rootfs is.
#
# NOTE THE BOOTSTRAP: the tools BesmCross resolves are the in-tree b6as and b6ld,
# so this program is assembled and linked by the host build of its own sources.
#
# b6_obj reads both of these from the caller's scope.  b6cc has no -M, so the
# header dependency is the whole tree at once, as it is in lib/ and cmd/ld/rootfs.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(cc DEST usr/bin/cc SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../cc.c)

# ...and the demo program the image ships beside it, ../hello.S, staged into the guest
# account's home exactly as cmd/look stages its dictionary and etc/ its passwd file.  It is
# NOT compiled here: it is source, and the whole point is that the machine compiles it.
#
# /home/guest BECAUSE cc WRITES INTO THE CURRENT DIRECTORY.  hello.o and a.out land in the
# cwd, and that home (owner 7, root.manifest) is the only directory on this image a non-root
# user can write in besides /tmp -- so it is the one place a logged-in user can build the
# demo where it sits rather than copying it out first.  It was /usr/guest until homes moved
# under /home; root.manifest is the account.
#
# copy_if_different, so an unchanged source does not re-trigger the whole image build.
add_custom_command(OUTPUT ${B6_ROOTFS}/home/guest/hello.S
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/home/guest
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../hello.S ${B6_ROOTFS}/home/guest/hello.S
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../hello.S
    COMMENT "stage cmd/cc/hello.S" VERBATIM)
add_custom_target(rootfs_ccdemo ALL DEPENDS ${B6_ROOTFS}/home/guest/hello.S)
add_dependencies(rootfs rootfs_ccdemo)

add_subdirectory(test)
