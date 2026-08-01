# uniq's b6sim cases -- task C5b.  Nine stdout cases plus one file oracle.
#
# NO FIXTURE IS COPIED INTO THE BUILD DIRECTORY here, unlike most of task C5b's directories:
# not one of the nine cases below names a file on its command line -- `noent' names one that
# does not exist -- so C5a's rule has nothing to apply to.  dup.txt is read by
# run-uniq-test.sh through $srcdir, which is right for a script the host runs.
#
# `nonewline' IS AN UPSTREAM BUG, and the same one rev(1) had.  v7's gline() answers `end of
# file' the moment it sees EOF, whatever it has already read, so a final line with no newline
# was thrown away -- and the buffer was left unterminated on that path besides.  The fixture
# is `a\nb' with no trailing newline: v7 prints `a' alone, this prints both.  Nothing shorter
# says it, and a fixture ending in a newline says nothing at all.
#
# `count' IS THE CASE THAT PINS THE COUNTER, which is the part of uniq that is easy to get one
# out on.  The four counts are 2, 1, 3, 1 over a seven-line fixture, so an off-by-one anywhere
# in the two nested loops shows up as a wrong number rather than as a wrong line.
#
# `fields' AND `letters' ARE THE TWO SKIP FORMS and they are tested separately on purpose:
# `-n' counts blank-delimited FIELDS and `+n' counts BYTES, and the two share skip() but reach
# it by different paths through the option loop -- one through isdigit(), which is also where
# the 129-entry class table is indexed (uniq.c's header).
#
# `utf8' asserts nothing subtle and is here for what it does NOT do: uniq compares whole
# bytes and never masks one, so a Cyrillic line is compared like any other.  It would fail
# the day somebody made the comparison ASCII.
#
# Every .expected here was computed by hand from the fixture, not captured from a first run.

b6_progtest(uniq plain)     # the ordinary case: one of each run of equal lines
b6_progtest(uniq count)     # -c, and the four counts a seven-line fixture produces
b6_progtest(uniq dupes)     # -d: only the lines that repeated
b6_progtest(uniq unique)    # -u: only the lines that did not
b6_progtest(uniq fields)    # -n skips n blank-delimited fields
b6_progtest(uniq letters)   # +n skips n bytes
b6_progtest(uniq utf8)      # whole bytes are compared, so a Cyrillic line compares
b6_progtest(uniq nonewline) # the final line v7 threw away
b6_progtest(uniq noent)     # an input file that is not there

# The two-file form, whose answer is a file rather than a stream.  See run-uniq-test.sh.
if(B6SIM)
    add_test(NAME cmd_uniq_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-uniq-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/uniq ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_uniq_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_uniq)
        add_dependencies(build_tests b6prog_uniq)
    endif()
endif()
