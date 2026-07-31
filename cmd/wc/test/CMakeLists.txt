# wc's b6sim cases -- task C5a, and the first text filter this harness has ever been pointed
# at.  ../../README.md §9 is the account of what it can and cannot say; for a program that
# reads bytes off standard input and writes numbers to standard output it can say everything,
# in a tenth of a second, with no boot.
#
# THE FIXTURES ARE COPIED INTO THE BUILD DIRECTORY rather than named through @srcdir@, and
# that is not tidiness: wc PRINTS THE FILENAME IT WAS GIVEN, so an argument of
# @srcdir@/text.txt would put an absolute build path into a .expected and the test would pass
# on one machine only.  Copying makes the argument a bare relative name, which is the same on
# every machine, and the working directory is this directory's binary dir (b6_progtest sets
# it).  ../../dd/test/CMakeLists.txt is the precedent for the copy; the reason is new here,
# and every later filter that names a file inherits it.
#
# utf8.txt IS THE POINT OF THE `utf8' CASE and not decoration.  v7's word test is
# `' '<c&&c<0177', which makes every byte of a Cyrillic letter a DELIMITER: upstream would
# count `привет мир' as zero words.  The case asserts two words and thirty-two characters --
# characters being BYTES, which is the other half of what wc.1 now says.

set(WC_FIXTURES text.txt utf8.txt)
set(_copied)
foreach(f ${WC_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/wc/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(wc_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build --
# the same reason b6_progtest() hangs the program itself there.
if(TARGET build_tests)
    add_dependencies(build_tests wc_fixtures)
endif()

b6_progtest(wc stdin) # no arguments at all: the counts, then a bare newline
b6_progtest(wc lines) # -l, -w and -c one at a time.  Note that -w prints a TRAILING SPACE
b6_progtest(wc words) # and -l and -c do not; that is v7's wcp() and the .expected files
b6_progtest(wc chars) # pin it, because it is the kind of thing a rewrite silently loses
b6_progtest(wc file)  # a named file, so the name is printed after the counts
b6_progtest(wc utf8)  # §11: a byte above 0177 is a word constituent, not a delimiter
b6_progtest(wc total) # two files, and the `total' line that only appears above argc 2
b6_progtest(wc noent) # v7 printed this and exited 0; the status file is the port's fix
