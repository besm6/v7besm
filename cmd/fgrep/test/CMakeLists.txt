# b6sim cases for fgrep(1) -- task C5c, with ../../grep/test.  ../../README.md SS9 is the
# harness: one b6_progtest() call per case, files <case>.args / <case>.in / <case>.expected
# / <case>.status beside this file, ctest name cmd_fgrep_<case> under label `cmd'.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@ --
# task C5a's rule, and it binds for its exact reason: fgrep prints the file name it was
# given under -l and under the multi-file prefix.
#
# A KEYWORD LIST IS NEWLINE-SEPARATED, so the several-keyword form cannot be an argument at
# all here: a .args file is one line.  keys.txt and -f are what test it, which is the same
# shape the program's own -f exists for.
#
# FOUR CASES BRACKET THE ONE CEILING, in two pairs.  toomany/manykeys are the keyword-COUNT
# direction and toolong/longkey the keyword-LENGTH direction; in each pair one list is taken
# and the other gets `fgrep: wordlist too large'.  There used to be one case and it asserted
# the wrong thing: 400 keywords of `keyword%08d' build 856 states of 3000 and died in cfail's
# 400-entry QUEUE, never in cgotofn -- the same diagnostic from a ceiling no document in this
# tree named, so the case passed on the wrong cause for the whole of task C5c.  The queue is
# sized from MAXSIZ now (../fgrep.c's header), toomany.txt really does fill the table, and
# manykeys and longkey are the two lists v7's code rejected.  ../../README.md SS9's rule that
# a stated bound gets a program run against it is why there are four and not one: grep.1's
# BUGS states 3000 states and no keyword count, and these four are what say so.

set(FGREP_FIXTURES text.txt fold.txt exact.txt keys.txt
                   toomany.txt toolong.txt manykeys.txt longkey.txt longtext.txt
                   failchain.txt failtext.txt)
set(_copied)
foreach(f ${FGREP_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/fgrep/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(fgrep_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests fgrep_fixtures)
endif()

b6_progtest(fgrep plain)    # one keyword, one file
b6_progtest(fgrep stdin)    # no file argument, the text read off standard input
b6_progtest(fgrep several)  # -f: the newline-separated list, which is fgrep's whole subject
b6_progtest(fgrep exact)    # -x, whole-line match: `rebeta' and `beta two' are not it
b6_progtest(fgrep count)    # -c
b6_progtest(fgrep number)   # -n
b6_progtest(fgrep offset)   # -b.  The same 19 grep prints for the same match -- see below
b6_progtest(fgrep invert)   # -v
b6_progtest(fgrep list)     # -l
b6_progtest(fgrep silent)   # -s
b6_progtest(fgrep fold)     # -y, which fgrep has although grep.1 said it did not
b6_progtest(fgrep utf8)     # a Cyrillic keyword: fgrep compares bytes and always could
b6_progtest(fgrep header)   # two files, so each line carries its file name
b6_progtest(fgrep nohead)   # -h takes that prefix away
b6_progtest(fgrep toomany)  # 250 keywords x 15 chars: past 3000 states, `wordlist too large'
b6_progtest(fgrep toolong)  # ONE keyword of 3001 chars: the same ceiling, the other way round
b6_progtest(fgrep manykeys) # 500 keywords, ~1060 states: TAKEN.  v7's queue said no at 399
b6_progtest(fgrep longkey)  # 450 chars in one keyword: v7 wrote past cfail's own stack frame
b6_progtest(fgrep failchain) # a fail link two hops deep: v7 printed NOTHING for `bd' in `debd'
b6_progtest(fgrep badflag)  # status 2, where v7 searched for the bad flag instead
b6_progtest(fgrep noent)    # a file that is not there: status 2
b6_progtest(fgrep nomatch)  # nothing found: status 1
b6_progtest(fgrep usage)    # no keywords at all; v7 exited 2 in silence

# cmd_fgrep_offset AND cmd_grep_offset BOTH SAY 19, over the same fixture and the same
# match.  That is the point of the -b divergence rather than a coincidence: v7 divided by
# BSIZE in grep and by a hard-coded 512 here, so the two commands answered differently about
# the same byte.  If one of these two numbers ever moves without the other, the divergence
# has been half-undone.
