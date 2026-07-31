# cmp's b6sim cases -- task C5a.
#
# Everything cmp does is reachable here: it opens two files by relative name, reads them a
# byte at a time and reports on standard output, so the harness has nothing to say `no' to.
# The fixtures are copied into the build directory for ../../wc/test/CMakeLists.txt's reason
# -- cmp PRINTS THE NAMES IT WAS GIVEN, so an @srcdir@ path would land in a .expected.
#
# THE THREE SKIP CASES ARE THE ONES WORTH NAMING.  cmp takes two undocumented positional
# operands after the file names, a count of bytes to skip in each, and v7's cmp.1 never
# mentioned them; ../cmp.1 does now.
#
#   skip     14 and 14, decimal, so the byte `differ' reports as char 15 is reported as
#            char 1 -- the assertion that both skips happened and that the counter restarts
#   skipoct  014 and 014.  A leading zero means octal, so TWELVE bytes go by rather than
#            fourteen and the difference lands at char 3.  A base-blind parse gives char 1,
#            which is why the number is 014 and not 016: 016 IS fourteen either way and the
#            case would have asserted nothing
#   skipbad  018 and 018, which is the regression for the parse itself.  v7 chose base 8 on
#            the leading zero and then accepted every digit isdigit() did, so `018' meant
#            sixteen -- an octal number with a digit octal has not got.  It stops at the `8'
#            now and means one, which is what every other C number parse does

set(CMP_FIXTURES a.txt b.txt c.txt short.txt)
set(_copied)
foreach(f ${CMP_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/cmp/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(cmp_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests cmp_fixtures)
endif()

b6_progtest(cmp same)    # identical files: silence and status 0, which is the whole contract
b6_progtest(cmp differ)  # the default report, and the char/line arithmetic behind it
b6_progtest(cmp silent)  # -s: status only, and it suppresses `cannot open' as well
b6_progtest(cmp list)    # -l: every differing byte, in octal, and it reads to the end
b6_progtest(cmp eof)     # one file is a prefix of the other
b6_progtest(cmp narg)    # no arguments at all -- and note the status is 2, not 1
b6_progtest(cmp noent)   # ... as it is for a file that is not there
b6_progtest(cmp skip)    # the undocumented positional operands, decimal
b6_progtest(cmp skipoct) # ... octal, which needs 014 rather than 016 to say anything
b6_progtest(cmp skipbad) # ... and an octal `8', which v7 accepted
