# b6sim cases for grep(1) -- task C5c.  ../../README.md SS9 is the harness; one
# b6_progtest() call per case, files <case>.args / <case>.in / <case>.expected /
# <case>.status beside this file, ctest name cmd_grep_<case> under label `cmd'.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@,
# which is task C5a's rule and binds here for its exact reason: grep PRINTS THE FILE NAME
# IT WAS GIVEN under -l and under the multi-file prefix, so an absolute build path would
# otherwise land in a checked-in .expected and the case would pass on one machine.
#
# AN ARGUMENT HERE IS A REGULAR EXPRESSION, which is what made run-prog-test.sh grow a
# `set -f': `[bg]', `g*amma' and `^[^abg]' are pathname patterns too, and a file happening
# to sit in the working directory would have replaced the pattern with its name.  What is
# still not possible is a pattern CONTAINING A SPACE -- the line is split and there is no
# quoting -- so those belong in kernel/test/filters.sh, where a real shell quotes them.

set(GREP_FIXTURES text.txt fold.txt repeat.txt deep.txt)
set(_copied)
foreach(f ${GREP_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/grep/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(grep_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests grep_fixtures)
endif()

b6_progtest(grep plain)       # the ordinary case: one pattern, one file
b6_progtest(grep stdin)       # no file argument at all, the text read off standard input
b6_progtest(grep count)       # -c.  v7 printed the two characters %D here (SS3)
b6_progtest(grep number)      # -n
b6_progtest(grep offset)      # -b.  THE DIVERGENCE: a byte offset, not a block number
b6_progtest(grep invert)      # -v
b6_progtest(grep list)        # -l, which is why the fixture may not be an @srcdir@ path
b6_progtest(grep silent)      # -s: nothing on stdout, the answer is the exit status
b6_progtest(grep class)       # a character class, the 256-bit bitmap's ordinary use
b6_progtest(grep negclass)    # [^...], which inverts all 32 bytes of it
b6_progtest(grep utf8class)   # THE CASE THIS TASK EXISTS FOR: a Cyrillic byte in a class
b6_progtest(grep utf8nomatch) # and the sharp half of it -- see the note below
b6_progtest(grep utf8lit)     # a Cyrillic pattern outside a class, which always worked
b6_progtest(grep utf8fold)    # -y over a multi-byte letter: no case, so unchanged
b6_progtest(grep fold)        # -y over ASCII, which is what the flag is for
b6_progtest(grep backref)     # \(...\)\1
b6_progtest(grep star)        # * after a literal
b6_progtest(grep dot)         # .
b6_progtest(grep anchor)      # ^
b6_progtest(grep dollar)      # $
b6_progtest(grep header)      # two files, so each line carries its file name
b6_progtest(grep nohead)      # -h takes that prefix away
b6_progtest(grep counttwo)    # -c over two files honours -h too, where v7's did not
b6_progtest(grep badre)       # an unterminated class: status 2
b6_progtest(grep badflag)     # status 2, and v7 went round the loop instead
b6_progtest(grep noent)       # a file that is not there: status 2
b6_progtest(grep nomatch)     # nothing found: status 1
b6_progtest(grep usage)       # no expression at all; v7 exited 2 in silence
b6_progtest(grep deep)        # sixteen nested stars: a diagnostic, not a blown stack

# WHY utf8nomatch IS THE SHARP ONE.  utf8class says the new bitmap MATCHES what it should;
# utf8nomatch says it does not match what it should not, and that is the assertion v7's code
# fails.  `п' is the two bytes 320 277, and v7's advance() masked every input byte with 0177
# -- which folds 320 onto 0120, the letter `P'.  So v7's grep would find `[п]' inside ALPHA.
# It comes back empty here, status 1.
