# cmd/ps/test -- ps(1) under b6sim.  Task C8.
#
# TWO LITERAL CASES AND ONE ORACLE.  The two below are the paths that print no process at
# all, and so no pid, no uid and no clock: an unknown option, and a pid selector that
# matches nothing.  Everything else about a ps line is a number the host supplies, which is
# what run-ps-test.sh is for and what its header explains.
#
# WHAT THIS HARNESS CANNOT REACH, and the reason ../ps.c's three-case u-area rule is only
# half asserted here: b6sim's proc[] holds ONE live entry and its p_addr is uhome, so every
# run takes the UBASE branch through /dev/kmem.  The /dev/mem branch -- another process's
# image, above KREACH -- and the swapped-out branch are kernel/test/inspect's.
# lib/test/memt.c is what proves the /dev/mem arithmetic itself on the image.

b6_progtest(ps badflag)
b6_progtest(ps nosuchpid)

if(B6SIM)
    add_test(NAME cmd_ps_format
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-ps-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/ps
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_ps_format PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_ps)
        add_dependencies(build_tests b6prog_ps)
    endif()
endif()
