# b6sim cases for /bin/sort -- task C5d.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.in, <case>.expected and <case>.status, and run-prog-test.sh diffs
# stdout and stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): sort prints the file name it was given -- `sort: can't open nosuch.txt' -- so a
# @srcdir@ path would put an absolute build path into a checked-in .expected.
#
# EVERY CASE PASSES `-T .'.  sort makes a temp file at startup whatever it is asked to do,
# and under b6sim /tmp is the BUILD MACHINE's (§9's absolute-path rule, for the sixth time).
# -T points it at this directory instead, so a run leaves nothing on the host -- and, for the
# mergemany case below, so the host could see it if anything were left.
#
# WHERE THE EXPECTATIONS CAME FROM.  ../README.md named sort as one of the two programs whose
# oracle should be a second implementation, on od's reasoning.  That is right for one reason
# and wrong for another: sort's output is ordinary readable text, so a reviewer CAN check it
# -- but v7's `+m.n' key syntax, its -d/-i tables, its tie-break rule and its numeric
# comparison are not POSIX sort's, so the host cannot check most of it.  So every expectation
# here was produced by a Python implementation written from sort.1 and the table definitions,
# and the six cases where `LC_ALL=C sort' IS exactly equivalent (plain, twofiles, reverse,
# unique, merge, utf8) were cross-checked against it as a third opinion.

set(SORT_FIXTURES
    words.txt nums.txt passwd.txt keys.txt punct.txt ctrl.txt utf8.txt dup.txt
    sorted.txt unsorted.txt m1.txt m2.txt brackets.txt nonl.txt long.txt
    tie.txt tiedup.txt empty.txt
    e1.txt e2.txt e3.txt e4.txt e5.txt e6.txt e7.txt e8.txt)
set(_copied)
foreach(f ${SORT_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/sort/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(sort_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests sort_fixtures)
endif()

b6_progtest(sort plain)         # the ordinary sort of one file
b6_progtest(sort stdin)         # no file argument at all, the text read off standard input
b6_progtest(sort dash)          # `-' names standard input, mixed with a real file
b6_progtest(sort twofiles)      # two inputs concatenated into one pass
b6_progtest(sort reverse)       # -r.  The one ordering flag that stays on the cheap cmpa path
b6_progtest(sort unique)        # -u over identical lines
b6_progtest(sort numeric)       # -n: leading zeros, a minus, -0, 1.5 against 1.50
b6_progtest(sort numrev)        # -nr
b6_progtest(sort fold)          # -f, upper and lower case together
b6_progtest(sort brackets)      # -f over [ \ ] -- v7's table folded `]' onto `\'
b6_progtest(sort dict)          # -d: only letters, digits and blanks significant
b6_progtest(sort ignore)        # -i: tab stays significant, which the ignore loop needs
b6_progtest(sort keyfield)      # +1 -2, one whole field
b6_progtest(sort keyrest)       # +1 with no end position: to the end of the line
b6_progtest(sort keychar)       # +0.3, the m.n form -- field()'s `.' case and its subscript
b6_progtest(sort keytab)        # -t: +2n, sort.1's own example, over colon records
b6_progtest(sort keynum)        # +1n, a numeric key that is NOT at the start of the line
b6_progtest(sort keyrev)        # +1r -2, per-key reversal
b6_progtest(sort keyblank)      # +1b -2, per-key leading-blank suppression
b6_progtest(sort multikey)      # two keys, the second breaking ties in the first
b6_progtest(sort keyflags)      # -u +0f +0, sort.1's first example: per-key flags override
b6_progtest(sort tie)           # THE TIE-BREAK: equal keys ordered with all bytes significant
b6_progtest(sort tieuniq)       # -u suppresses that tie-break, so key-equal lines collapse
b6_progtest(sort utf8)          # Cyrillic collates by byte, after every ASCII line
b6_progtest(sort utf8dict)      # THE DIVERGENCE: -d keeps a Cyrillic letter significant
b6_progtest(sort utf8ignore)    # THE DIVERGENCE: -i does too.  v7 made these two lines EQUAL
b6_progtest(sort utf8fold)      # -f leaves a byte above 0177 alone: it has no case
b6_progtest(sort merge)         # -m over two sorted files
b6_progtest(sort mergeuniq)     # -mu: the duplicate marking msort() carries for the merge
b6_progtest(sort mergemany)     # EIGHT files against N == 7: a real temp file and two passes
b6_progtest(sort nonewline)     # the final line with no newline, which v7 threw away
b6_progtest(sort empty)         # nothing at all: no output, status 0
b6_progtest(sort check)         # -c on a sorted file: silence, status 0
b6_progtest(sort checkbad)      # -c on an unsorted one: sort: disorder:, status 1
b6_progtest(sort checkuniq)     # -cu: sort: nonunique:, status 1
b6_progtest(sort checktwo)      # -c with two files is refused
b6_progtest(sort noent)         # an input file that is not there
b6_progtest(sort toolong)       # a line past L on the SORT path: a diagnostic, not a truncation
b6_progtest(sort toolongmerge)  # ... and the same line past L on the MERGE path, which is the
                                # point: v7 had a bound on one and none on the other, so a file
                                # sorted correctly or silently wrongly depending on whether it
                                # happened to fit in the arena in one pass
b6_progtest(sort manykeys)      # eleven keys against NF == 10

# The FILE oracle: four things about sort are not visible on standard output at all.
# See run-sort-test.sh.
if(B6SIM)
    add_test(NAME cmd_sort_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-sort-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/sort ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_sort_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_sort)
        add_dependencies(build_tests b6prog_sort)
    endif()
endif()
