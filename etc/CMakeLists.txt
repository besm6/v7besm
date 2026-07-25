# /etc -- the static half of the root filesystem.
#
# Nothing here is compiled.  motd, passwd and rc are plain text that has to be ON the disk
# image, and the image is built out of ${B6_ROOTFS} (kernel/test/root.manifest), so they are
# STAGED there exactly as b6_prog() stages a program rather than being named in the manifest
# from the source tree.  One rule for where the contents of the image come from, and
# `ls build/rootfs' then shows what the image will hold.
#
# Added from inside the top-level libruntime guard with cmd/init, cmd/sh and the four
# commands -- not because anything here needs the cross toolchain, but because B6_ROOTFS and
# the `rootfs' aggregate are defined there.

set(B6_ETC motd passwd rc)

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
