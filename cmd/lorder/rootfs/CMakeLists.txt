# /usr/bin/lorder -- cmd/lorder staged onto the disk image (task C9c).
#
# The only program of this task that is not compiled: lorder is a shell script,
# and it came along with /usr/bin/nm because nm is the only thing it needed that
# the image had not got.  The rest of its pipe -- sed, sort, join, basename, and
# tsort at the far end -- has been on the image since task C5f.
#
# ONE SOURCE, TWO COPIES.  ../lorder.sh.in is configured twice: once one directory
# up with @NM@ = b6nm, which is the host b6lorder, and once here with @NM@ = nm,
# which is this file.  Nothing else differs, and the script is written to parse
# under v7's sh as well as the host's -- backquotes rather than $( ), and
# ${name-word} rather than ${name:-word}, `:' not being a substitution character
# in cmd/sh/ctype.h.
#
# WHY THIS IS A SUBDIRECTORY rather than more lines in ../CMakeLists.txt: the host
# half is added at the top of ../../CMakeLists.txt, ABOVE the if(B6RUNTIME_LIB)
# guard, since b6lorder installs whether or not there is a cross toolchain -- and
# B6_ROOTFS and the `rootfs' aggregate are both defined inside that guard.  Same
# shape as the four cmd/<x>/rootfs directories beside it, for the same reason.
#
# CONFIGURED INTO THE BUILD DIRECTORY AND THEN COPIED, rather than configured
# straight into ${B6_ROOTFS}: configure_file() runs at configure time only, so a
# `make' after `rm -rf build/rootfs' would leave this one file missing where every
# other staged file came back.  The copy is a build step, and copy_if_different, so
# an unchanged script does not re-trigger the image build (etc/CMakeLists.txt does
# the same for the static half of the tree).
set(NM nm)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../lorder.sh.in
               ${CMAKE_CURRENT_BINARY_DIR}/lorder @ONLY)

add_custom_command(OUTPUT ${B6_ROOTFS}/usr/bin/lorder
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/lorder ${B6_ROOTFS}/usr/bin/lorder
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lorder
    COMMENT "stage usr/bin/lorder" VERBATIM)

add_custom_target(rootfs_lorder ALL DEPENDS ${B6_ROOTFS}/usr/bin/lorder)
add_dependencies(rootfs rootfs_lorder)
add_subdirectory(test)
