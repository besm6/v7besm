# b6sim cases for /bin/pr -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.in, <case>.expected and <case>.status, and run-prog-test.sh diffs
# stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): pr prints the file name it was given, both in its page heading and in
# `pr: can't open nosuch.txt', so a @srcdir@ path would put an absolute build path into a
# checked-in .expected.
#
# EVERY CASE HERE PASSES -t, AND THAT IS FORCED RATHER THAN CHOSEN.  pr's page heading holds
# the file's mtime -- the time of day, for standard input -- so no run of pr WITHOUT -t has a
# reproducible standard output, and no such case can exist under this harness at all.  The
# heading, the trailer, the page numbering, +n and -h therefore live in run-pr-test.sh, which
# fixes an mtime with touch first.  It is a new shape for this directory: C5a's file oracles
# (tee, split, uniq, and later sed and sort) exist because the output is a FILE, and this one
# exists because the output contains a CLOCK.
#
# -l IS ALSO ON EVERY CASE, AND FOR A LESS OBVIOUS REASON.  A v7 page is `length' lines
# whether or not there is that much input: putpage() pads the body out to plength with blank
# lines, so `pr -t' on a four-line file emits four lines and sixty-two blanks.  (Host pr does
# not, which is the largest of the three dialect differences below.)  Shortening the page
# with -l is what makes an expectation readable.
#
# WHERE THE EXPECTATIONS CAME FROM.  ../../README.md named pr as the one program left with
# od(1)'s property -- output no reviewer can check by eye -- and it is right, though not for
# od's reason.  pr's output is ordinary text; what nobody can check is the WHITESPACE in it:
# which run of spaces became a tab, which column a truncation fell in, how many blank lines
# padded a short page.  So every .expected below was produced by a Python implementation of
# v7's algorithm written from pr.1 plus the six things pr.1 does not state, and the two were
# compared byte for byte.  Fourteen of the nineteen cases are within the model's reach and
# all fourteen agree; the other five are -m, two files, and the three diagnostics, which the
# model does not implement.
#
# AND HOST pr IS THE CHEAP THIRD ORACLE (C5e's rule), which found three real dialect
# differences and no defect:
#
#   * v7 pads a short page out to `length' lines and BSD's does not, so every case here is
#     longer than the host's answer by the padding.
#   * v7 gives each column `length - margin' lines -- 56 on a default page -- so `-l6 -3'
#     is three columns of six, where BSD balances the input across the three.
#   * v7 truncates a column at its width but never truncates the LAST column, and never
#     truncates single-column output at all.
#
# THE TERMINAL chmod IS UNREACHABLE HERE AND THAT IS THE POINT.  fixtty() takes the tty to
# 0600 for the duration of a listing, and under b6sim the tty would be the BUILD MACHINE's
# (§9's whose-is-it rule).  run-prog-test.sh redirects standard output to a file, so
# ttyname(1) answers NULL and the whole path is a no-op in both harnesses.

set(PR_FIXTURES
    text.txt wide.txt tabs.txt spaced.txt utf8.txt ctrl.txt empty.txt nonl.txt
    m1.txt m2.txt)
set(_copied)
foreach(f ${PR_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/pr/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(pr_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests pr_fixtures)
endif()

b6_progtest(pr plain)       # one column, and the blank padding out to -l
b6_progtest(pr stdin)       # no file argument at all: the text read off standard input
b6_progtest(pr twofiles)    # two files one after the other, each starting its own page
b6_progtest(pr twocol)      # -2: the second cursor starts `length' lines into the file
b6_progtest(pr threecol)    # -3, so that two cursors trail the leading one rather than one
b6_progtest(pr sepchar)     # -s,: no padding at all, and a column that ran out emits nothing
b6_progtest(pr septab)      # -s with no character: a tab, which is what pr.1 promises
b6_progtest(pr narrow)      # -w20 -4: colw is 5, so `line10' is TRUNCATED to `line1'
b6_progtest(pr widelines)   # ... and the LAST column is never truncated, however wide
b6_progtest(pr tabs)        # a tab in the input: expanded by pgetc, re-compressed by put()
b6_progtest(pr spaces)      # a run of spaces compressed back to a tab where a stop carries it
b6_progtest(pr utf8)        # §11: a Cyrillic column through the ring, sentinels and all
b6_progtest(pr formfeed)    # a form feed ends the page early, which is the only way to
b6_progtest(pr nonl)        # a final line with no newline is still printed
b6_progtest(pr emptyfile)   # nothing at all in, nothing at all out -- not a page of blanks
b6_progtest(pr merge)       # -m: the second tpgetc path, which never touches the ring
b6_progtest(pr noent)       # a file that is not there
b6_progtest(pr nocol)       # -99 columns is refused
b6_progtest(pr zerocol)     # ... and so is -x, which v7 took as zero and then divided by

# The CLOCK-and-RING oracle: the page heading and the look-ahead overrun are invisible to
# every case above.  See run-pr-test.sh.
if(B6SIM)
    add_test(NAME cmd_pr_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-pr-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/pr ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_pr_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_pr)
        add_dependencies(build_tests b6prog_pr)
    endif()
endif()
