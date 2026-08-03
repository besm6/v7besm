# Test for /usr/bin/lorder, the staged copy of cmd/lorder (task C9c).
#
# One case, and it is an AGREEMENT test of the same kind as the four beside it:
# host b6lorder and the staged script over one set of objects, the two edge-lists
# diffed live.  What makes it different is that the native side is not a program
# but a PIPELINE -- the image's own sh running the image's own lorder over the
# image's own nm, sed, sort, join and basename -- which is the only way to find out
# whether the two v7-shell accommodations in ../../lorder.sh.in actually hold.
# run-lorder-test.sh's header has the rest of the argument, including why every one
# of those programs has to be copied into the working directory first.
#
# Label `rootfs', with the rest of the staged-program family.  There is no
# rootfs_lorder_size beside it: b6_prog() registers that check and a shell script
# is not built by one -- the address-space ceilings are the compiler's business and
# this file has no text segment to measure.
if(B6SIM)
    add_test(NAME rootfs_lorder_deps
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-lorder-test.sh
                ${CMAKE_CURRENT_BINARY_DIR}/../../lorder.sh $<TARGET_FILE:b6nm>
                ${B6SIM} ${B6_ROOTFS}/bin/sh ${B6_ROOTFS}
                $<TARGET_FILE:b6as>
                ${CMAKE_CURRENT_SOURCE_DIR} deps dep)
    set_tests_properties(rootfs_lorder_deps PROPERTIES LABELS rootfs)
endif()

# ctest does not build, and this case needs more of the image than any other in
# C9c: the shell, nm and four filters, all staged, plus the two host tools that
# make the fixture.
if(TARGET build_tests)
    add_dependencies(build_tests b6nm b6as
                     b6prog_sh b6prog_nm b6prog_sed b6prog_sort b6prog_join
                     b6prog_basename b6prog_rm rootfs_lorder)
endif()
