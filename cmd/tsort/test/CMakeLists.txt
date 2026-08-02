# b6sim cases for /bin/tsort -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.in, <case>.expected and <case>.status, and run-prog-test.sh diffs
# stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): tsort prints the name it was given in `tsort: cannot open nosuch.txt'.
#
# WHAT AN ORACLE FOR tsort HAS TO BE.  A topological order is not unique, so a checked-in
# .expected asserts THIS program's order and not the answer -- and host tsort really does
# print a different (equally valid) one for `dag'.  So each expectation below was also
# checked mechanically against the input: every node appears exactly once, and every edge
# a-before-b holds in the printed order.  All four orderable cases pass that check.  It is
# the shape C4d's rule wants -- an oracle that can disagree -- reached without a second
# implementation, because the PROPERTY is checkable where the ANSWER is not.

set(TSORT_FIXTURES dag.txt lib.txt cyc.txt odd.txt utf8.txt long.txt empty.txt)
set(_copied)
foreach(f ${TSORT_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/tsort/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(tsort_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests tsort_fixtures)
endif()

b6_progtest(tsort dag)        # a chain plus an isolated node named by a pair of itself
b6_progtest(tsort lib)        # lorder(1)'s shape: object files, one of them self-paired
b6_progtest(tsort stdin)      # no file argument: the graph read off standard input
b6_progtest(tsort cycle)      # findloop(): `cycle in reverse order' and the nodes in it,
                              # and the two error() calls on that path that v7 gave ONE
                              # argument to a function of two
b6_progtest(tsort odd)        # an odd number of fields
b6_progtest(tsort emptyfile)  # nothing at all in, nothing at all out
b6_progtest(tsort noent)      # a file that is not there
b6_progtest(tsort utf8)       # §11: a Cyrillic node name.  getname()'s blank set is written
                              # out rather than taken from a table, so a byte above 0177 is
                              # an ordinary name character
b6_progtest(tsort longname)   # a name past NAMESIZE is a DIAGNOSTIC.  v7 wrote it into
                              # char[50] with an unbounded %s; `%255s' would have been worse
                              # than the overrun, splitting one name into two and re-pairing
                              # every line after it
