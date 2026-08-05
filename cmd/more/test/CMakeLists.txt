#
# more's b6sim cases -- task C27.
#
# WHAT THESE CAN AND CANNOT SAY, and the reason is structural rather than a gap in the
# harness.  With standard output not a terminal, main() takes copy_file() and more is
# cat(1): -c, -d, -f, -l, -p, -s, -u and -lines are ALL INERT, and $MORE can only set
# those same flags, so there is no case for it and neither `env -i' in
# ../../../scripts/run-prog-test.sh nor ENV_WHITELIST in ../../sim/session.cpp had to
# be touched.  With standard INPUT not a terminal either, more.c clears firstf before
# the file loop, so +linenum and +/string are never reached for a named file -- and
# the ::::::::::::::: banner appears even for a single one.  The `inert' case below is
# what pins all of that: every option at once, and the file comes out unchanged.
#
# What is left is worth having, and it is most of what a pipeline ever asks of more:
# the banner, the ORDER of standard input against the named files, the two
# diagnostics, and eight-bit transparency.  The screen half is deferred out loud in
# ../README.md, for cmd/novi's reason and with cmd/novi's answer.
#
# THE FIXTURES ARE COPIED IN rather than named through @srcdir@, which is
# cmd/pr/test's rule and here it is forced: more prints a file name in its banner and
# again through perror(), so an absolute build path would land in a checked-in
# .expected and the tree would not build anywhere else.
#
set(FIXTURES cat.in both.in text.txt two.txt utf8.txt nonl.txt empty.txt)

set(_copied "")
foreach(_f ${FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_f}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/${_f}
                ${CMAKE_CURRENT_BINARY_DIR}/${_f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${_f}
        COMMENT "fixture ${_f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${_f})
endforeach()
add_custom_target(more_fixtures ALL DEPENDS ${_copied})
if(TARGET build_tests)
    add_dependencies(build_tests more_fixtures)
endif()

b6_progtest(more cat)       # no file argument: a byte copy of stdin, and NO banner
b6_progtest(more onefile)   # the banner appears even for ONE file
b6_progtest(more twofiles)  # two banners, in order
b6_progtest(more both)      # stdin first, THEN the named file with its banner
b6_progtest(more utf8)      # section 11: Cyrillic through unchanged
b6_progtest(more nonl)      # a last line with no newline is not invented
b6_progtest(more empty)     # banner, no body; file_size 0 takes the LONG_MAX arm
b6_progtest(more noent)     # perror() on descriptor 2, and more runs on: exit 0
b6_progtest(more dir)       # *** .: directory ***, ordered against the perror above
b6_progtest(more inert)     # every option and a +linenum, and nothing happens
