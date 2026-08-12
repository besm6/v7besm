# cmd/pstat/test -- pstat(1M) under b6sim.  Task C8.
#
# SEVEN LITERALS AND ONE ORACLE.  The literals are the empty tables, and they are literals
# because b6sim's imitation kernel leaves inode, file, text, mount, coremap, swapmap and sc
# ZERO on purpose -- "a plausible fiction is worse than an empty one", cmd/sim/kernel.h.  So
# this half of pstat's testing is exactly what ../README.md asked it to be: the flags, and the
# empty-table output.  Two of them do more than that:
#
#   `tty' pins NSC and the 29-word stride of struct tty at once -- two rows and no more.
#   `swap' pins three scalars, because b6sim stores kernel/conf.c's own numbers for the
#   paging store rather than inventing any: swapdev 1,0, swplo 0, nswap 1024.
#
# run-pstat-test.sh takes the two modes whose numbers come from the host, and its real
# subject is the cross-reference between them.  Its header is the account.
#
# WHAT NEEDS THE BOOT is every number this program exists to print.  kernel/test/inspect.

b6_progtest(pstat inode DEST etc/pstat)
b6_progtest(pstat text DEST etc/pstat)
b6_progtest(pstat file DEST etc/pstat)
b6_progtest(pstat mount DEST etc/pstat)
b6_progtest(pstat tty DEST etc/pstat)
b6_progtest(pstat swap DEST etc/pstat)
b6_progtest(pstat badopt DEST etc/pstat)

if(B6SIM)
    add_test(NAME cmd_pstat_tables
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-pstat-test.sh
                ${B6SIM} ${B6_ROOTFS}/etc/pstat
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_pstat_tables PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_pstat)
        add_dependencies(build_tests b6prog_pstat)
    endif()
endif()
