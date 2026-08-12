#
# Unit tests for the /usr/bin/calendar shell script -- task C23.
#
# NOT b6_progtest, and the reason is in ../README.md: b6sim's exec(2) reaches HOST binaries
# rather than BESM-6 a.out, so the script's own pipeline dies at the first egrep.  ../../lorder
# is the precedent -- a shell script has no engine to link, so the test drives the shipped file
# itself under the host shell.
#
# THE GENERATOR IS THE REAL ONE, run under b6sim through a wrapper the test writes: that is what
# makes this an assertion about the target binary and not about a stub.  Its output is today's
# date, which no static .expected file can hold, so the fixture holds EVERY date instead and the
# test computes what to look for.
#
add_executable(calendar_test calendar_test.cpp)
target_link_libraries(calendar_test PRIVATE GTest::gtest_main)
target_compile_definitions(calendar_test PRIVATE
    CALENDAR_SH="${CMAKE_CURRENT_SOURCE_DIR}/../calendar.sh"
    B6SIM_EXE="$<TARGET_FILE:b6sim>"
    LIBCAL_EXE="${B6_ROOTFS}/usr/lib/calendar")
add_dependencies(calendar_test b6sim b6prog_calendar)
gtest_discover_tests(calendar_test)
add_dependencies(build_tests calendar_test)
