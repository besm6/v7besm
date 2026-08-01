# comm's b6sim cases -- task C5b.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@, which
# is C5a's rule and applies here for its stated reason: `noent' asserts the diagnostic, and a
# diagnostic that quoted an absolute build path would put one machine's directory layout into
# a checked-in .expected.
#
# `utf8' ASSERTS SOMETHING NO LINE OF THE PORT CHANGED.  comm decides which of two lines
# sorts first with `*ra < *rb', and `char' is signed on a PDP-11 and unsigned here (§3), so a
# byte above 0177 collated BEFORE all of ASCII in v7 and collates after it now.  ua.txt holds
# `zebra' (0172) ahead of `мир' (0320 0274) -- which is sorted in this machine's byte order
# and is NOT sorted in v7's.  Run this case against a signed-char comm and the two files
# interleave differently and every column moves.  It is the cheapest possible assertion that
# comm agrees with the order sort(1) and the shell's globbing already use.
#
# THE THREE COLUMN CASES ARE WHAT THE LITERAL WRITES WERE FOR.  v7 shortened three string
# constants in place to indent columns 2 and 3, storing into the const segment on this
# machine (comm.c's header).  `all', `common' and `only1' pin the indentation those stores
# produced -- two tabs, one tab, none -- so the arithmetic that replaced them is held to
# v7's output rather than to a reading of v7's code.
#
# Every .expected here was computed by hand from the two fixtures, not captured from a run.

set(COMM_FIXTURES a.txt b.txt ua.txt ub.txt)
set(_copied)
foreach(f ${COMM_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/comm/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(comm_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests comm_fixtures)
endif()

b6_progtest(comm all)      # all three columns, and the tabs that indent two of them
b6_progtest(comm common)   # -12: the common lines alone, unindented
b6_progtest(comm only1)    # -23: what is in the first file and not the second
b6_progtest(comm stdin)    # `-' names the standard input for either file
b6_progtest(comm utf8)     # the collating order, which is unsigned here (see above)
b6_progtest(comm noent)    # a file that is not there
b6_progtest(comm badflag)  # an unknown flag letter
b6_progtest(comm argcount) # one file where two are wanted
