# b6sim cases for egrep(1) -- task C26.  ../../README.md SS9 is the harness; one
# b6_progtest() call per case, files <case>.args / <case>.in / <case>.expected /
# <case>.status beside this file, ctest name cmd_egrep_<case> under label `cmd'.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@,
# for the reason ../../grep/test/CMakeLists.txt gives: egrep prints the file name it was
# given, under -l and under the multi-file prefix.
#
# text.txt IS ../../grep/test/text.txt BYTE FOR BYTE, which is what makes cmd_egrep_offset
# comparable with cmd_grep_offset and cmd_fgrep_offset.  All three say 19.
#
# An argument here is a regular expression, and run-prog-test.sh runs under `set -f' for
# that reason.  A pattern containing a SPACE still cannot be a case -- the line is split
# and there is no quoting -- and README.md records the ones checked by hand instead.

set(EGREP_FIXTURES text.txt lat.txt dash.txt pats.txt wrap.txt longpat.txt)
set(_copied)
foreach(f ${EGREP_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/egrep/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(egrep_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests egrep_fixtures)
endif()

b6_progtest(egrep plain)       # the ordinary case: one pattern, one file
b6_progtest(egrep stdin)       # no file argument at all, the text read off standard input
b6_progtest(egrep alt)         # a|b -- THE OPERATOR egrep exists for and grep has not got
b6_progtest(egrep paren)       # grouping
b6_progtest(egrep plus)        # +, also egrep's own
b6_progtest(egrep quest)       # ?, likewise
b6_progtest(egrep star)        # *
b6_progtest(egrep dot)         # .
b6_progtest(egrep anchor)      # ^, which yylex turns into a newline
b6_progtest(egrep dollar)      # $, the same newline at the other end
b6_progtest(egrep class)       # a class: a counted list of bytes, not grep's bitmap
b6_progtest(egrep negclass)    # [^...], whose loop now runs over all 256 byte values
b6_progtest(egrep utf8class)   # THE CASE THIS TASK EXISTS FOR: a Cyrillic byte in a class
b6_progtest(egrep utf8nomatch) # and the sharp half of it -- see the note below
b6_progtest(egrep utf8lit)     # a Cyrillic pattern outside a class, which v7 lost as well
b6_progtest(egrep count)       # -c
b6_progtest(egrep number)      # -n
b6_progtest(egrep offset)      # -b.  THE DIVERGENCE, and the 19 grep and fgrep also print
b6_progtest(egrep offsetwrap)  # -b for a match past the 1024-byte ring's first lap
b6_progtest(egrep offsetfirst) # -b -v: the other succeed path, and a line at offset 0
b6_progtest(egrep invert)      # -v
b6_progtest(egrep list)        # -l, which is why the fixture may not be an @srcdir@ path
b6_progtest(egrep silent)      # -s: nothing on stdout, the answer is the exit status
b6_progtest(egrep header)      # two files, so each line carries its file name
b6_progtest(egrep nohead)      # -h takes that prefix away
b6_progtest(egrep counttwo)    # -c over two files honours -h too, where v7's did not
b6_progtest(egrep fromfile)    # -f, which v7 could not finish reading at all here
b6_progtest(egrep eflag)       # -e, for a pattern beginning with a minus
b6_progtest(egrep longpat)     # a 120-character pattern: the walks that are no longer
                               # recursive.  See the note below -- it is a proxy, not proof.
b6_progtest(egrep toolong)     # 200 characters: MAXLIN, status 2
b6_progtest(egrep toomany)     # seven (a|b) groups: NSTATES, status 2, and the case v7
                               # answered wrongly on rather than diagnosing
b6_progtest(egrep badre)       # an unterminated class: status 2
b6_progtest(egrep badflag)     # status 2, and v7 went round the loop instead
b6_progtest(egrep noent)       # a file that is not there: status 2
b6_progtest(egrep nomatch)     # nothing found: status 1
b6_progtest(egrep usage)       # no expression at all; v7 exited 2 in silence

# WHY utf8nomatch IS THE SHARP ONE.  utf8class says the 256-wide table matches what it
# should; utf8nomatch says it does not match what it should not, over a file with no Cyrillic
# in it at all -- and v7's failures here are plausible wrong answers, not errors.  README.md.
#
# WHAT longpat DOES NOT SAY.  rootfs_egrep_size weighs const+text+data+bss and a stack frame
# is none of them (../../README.md SS6), so this shows one deep pattern working, not that the
# walks are bounded.  README.md has the measurement that does.
