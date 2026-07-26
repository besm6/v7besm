# /etc -- the static half of the root filesystem.
#
# Nothing here is compiled.  motd, passwd, rc and termcap are plain text that has to be ON
# the disk image, and the image is built out of ${B6_ROOTFS} (root.manifest, at the top of the tree), so they are
# STAGED there exactly as b6_prog() stages a program rather than being named in the manifest
# from the source tree.  One rule for where the contents of the image come from, and
# `ls build/rootfs' then shows what the image will hold.
#
# Added from inside the top-level libruntime guard with cmd/init, cmd/sh and the four
# commands -- not because anything here needs the cross toolchain, but because B6_ROOTFS and
# the `rootfs' aggregate are defined there.

# termcap is BSD's termcap.small, verbatim, and lib/libtermcap/README.md says what it does
# and does not describe.  It is also the file lib/test/termcapt reads under b6sim -- through
# $TERMCAP, naming THIS source copy -- so that the b6sim run and the run off the image
# adjudicate against one expectation.
set(B6_ETC group motd passwd rc termcap)

set(_staged "")
foreach(f ${B6_ETC})
    # copy_if_different, so an unchanged file does not re-trigger the whole image build.
    add_custom_command(OUTPUT ${B6_ROOTFS}/etc/${f}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/etc
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${B6_ROOTFS}/etc/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "stage etc/${f}" VERBATIM)
    list(APPEND _staged ${B6_ROOTFS}/etc/${f})
endforeach()

add_custom_target(rootfs_etc ALL DEPENDS ${_staged})
add_dependencies(rootfs rootfs_etc)
