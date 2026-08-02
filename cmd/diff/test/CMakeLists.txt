# b6sim cases for /bin/diff -- task C5f.  ../../README.md §9 is the harness; the files are
# <case>.args, <case>.expected and <case>.status, and run-prog-test.sh diffs stdout and
# stderr together.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ (C5a's
# rule): diff prints the name it was given in `diff: cannot open nosuch.txt'.
#
# HOST diff IS NOT THE ORACLE HERE, AND THAT IS A FINDING RATHER THAN A LIMITATION.  A diff
# is not unique: v7 finds a longest common subsequence by Hunt-Szymanski over Harold Stone's
# k-candidates, GNU diff uses Myers, and the two pick different -- equally valid -- scripts.
# Over 150 generated file pairs and four option sets they disagree textually 79 times in 600
# runs and NOT ONCE about whether the files differ.  So these .expected files assert THIS
# program's answer, and the oracle that says the answer is right lives in run-diff-test.sh:
# every `diff -e' script the guest writes is applied to the first file with the host's ed(1)
# and must produce the second, byte for byte.  150 out of 150 do.
#
# WHAT `-b' COST, and it is worth reading before writing any other <ctype.h> replacement.
# v7 calls isspace() on a raw file byte at six sites in diff and four in diffh, off the end
# of a 129-entry table (C5b's uniq/look finding).  The obvious fix -- a six-way macro --
# is WRONG here, because every call site passes an expression with a side effect:
# `BLANK(c = getc(input[0]))' reads six characters where isspace() read one.  `diff -b' then
# reported two identical files as wholly different and printed truncated text with it.  It is
# a static function in both programs now, and cmd_diff_blanks is the case that would have
# caught it.

set(DIFF_FIXTURES a.txt b.txt same.txt add.txt del.txt sp1.txt sp2.txt
                  u1.txt u2.txt nonl1.txt nonl2.txt empty.txt one.txt)
set(_copied)
foreach(fx ${DIFF_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${fx} ${CMAKE_CURRENT_BINARY_DIR}/${fx}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fx}
        COMMENT "cmd/diff/test: ${fx}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${fx})
endforeach()
add_custom_target(diff_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests diff_fixtures)
endif()

b6_progtest(diff plain)       # a change and an append: status 1
b6_progtest(diff identical)   # ... and no output at all, status 0
b6_progtest(diff added)       # a pure insertion
b6_progtest(diff deleted)     # a pure deletion
b6_progtest(diff edscript)    # -e, the ed script.  run-diff-test.sh is what APPLIES one
b6_progtest(diff fscript)     # -f, the reversed form
b6_progtest(diff blanks)      # -b: two files that differ only in how much white space they
                              # hold are the same file.  This is the case the six-way macro
                              # described above got wrong in both directions at once
b6_progtest(diff blanksoff)   # ... and without -b they differ
b6_progtest(diff utf8)        # §11: Cyrillic lines hash and compare as bytes
b6_progtest(diff utf8blank)   # ... and -b over them, which is where v7 read past the end of
                              # the 129-entry ctype table
b6_progtest(diff nonewline)   # a last line with no newline, which v7's skipline() looped on
                              # for ever and whose EOF its `char c' truncated to 0377
b6_progtest(diff emptyleft)   # an empty first file
b6_progtest(diff emptyright)  # an empty second file
b6_progtest(diff emptyboth)   # both empty
b6_progtest(diff noent)       # a file that is not there: status 2
b6_progtest(diff argcount)    # one file name
b6_progtest(diff nodiffh)     # -h under b6sim: /usr/lib/diffh is on the IMAGE, so this
                              # harness can only assert the diagnostic.  The working path is
                              # run-diff-test.sh and kernel/test/filters

# The APPLY oracle, the directory argument and diffh itself.  See run-diff-test.sh.
if(B6SIM)
    add_test(NAME cmd_diff_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-diff-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/diff ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_diff_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_diff AND TARGET b6prog_diffh)
        add_dependencies(build_tests b6prog_diff b6prog_diffh)
    endif()
endif()
