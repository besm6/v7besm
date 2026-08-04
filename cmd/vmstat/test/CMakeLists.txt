# cmd/vmstat/test -- vmstat(1) under b6sim.
#
# ONE b6_progtest CASE AND ONE ORACLE, and run-vmstat-test.sh's header is the account of
# why the split falls there: the interval every rate is divided by comes off b6sim's
# instruction counter at a thousand instructions to the tick, so the denominator is 0 or 1
# depending on how far the program has got, and a checked-in .expected would be pinning the
# code generator rather than vmstat.  The usage diagnostic below has no such dependence and
# is a literal.
#
# WHAT NEEDS THE BOOT: every column with a second process, a device or an interrupt in it.
# b6sim has one process, no block device, no 0500 vector and no scheduler, so `b', `w',
# `md', `mb', `in', `tr' and `cs' are STRUCTURALLY zero there -- and the oracle asserts
# that, because a harness with no kernel under it must not invent traffic.  Where they move
# is kernel/test/inspect.

b6_progtest(vmstat badopt DEST bin/vmstat)

if(B6SIM)
    add_test(NAME cmd_vmstat_format
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-vmstat-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/vmstat
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_vmstat_format PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_vmstat)
        add_dependencies(build_tests b6prog_vmstat)
    endif()
endif()
