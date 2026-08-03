# cmd/iostat/test -- iostat(1M) under b6sim.  Task C8.
#
# ONE b6_progtest CASE AND ONE ORACLE, and run-iostat-test.sh's header is the account of why
# the split falls there: every number this program prints comes off b6sim's instruction
# counter at a resolution of a thousand instructions per tick, so the denominator is 0 or 1
# depending on how far the program has got, and a checked-in .expected would be pinning the
# code generator rather than iostat.  The diagnostic below has no such dependence and is a
# literal.
#
# WHAT NEEDS THE BOOT: any number with a second process in it.  b6sim bills every tick to
# dk_time[0], there being no kernel under it to charge system time to, nothing idle and no
# nice -- so `user' is 100.00 or nothing and the other three columns never move.
# kernel/test/inspect is where they do.

b6_progtest(iostat badopt DEST etc/iostat)

if(B6SIM)
    add_test(NAME cmd_iostat_format
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-iostat-test.sh
                ${B6SIM} ${B6_ROOTFS}/etc/iostat
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_iostat_format PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_iostat)
        add_dependencies(build_tests b6prog_iostat)
    endif()
endif()
