# sum's b6sim cases -- task C5a.
#
# THE EXPECTED CHECKSUMS ARE COMPUTED, NOT CAPTURED.  Every number in a .expected here came
# out of an independent implementation of ../sum.c's rotate-add-mask loop rather than out of
# a first run of the program, because a checksum captured from the thing under test asserts
# only that it is deterministic.  What makes that worth insisting on is the claim sum.c's
# header makes: the answer on this 41-bit machine is bit for bit the PDP-11's, and these
# files are what says so.
#
# `big' IS THE §4 ASSERTION and the reason this directory generates a fixture at all.  v7
# divided the byte count by BUFSIZ, which was 512 there and is 3072 here, and this port
# multiplies by KBPB so that the number is in the 1024-byte blocks df, du, quot and `ls -s'
# all report (../../README.md §4).  The fixture is 3073 bytes -- one byte into a second
# filesystem block -- so the answer is 2 * KBPB == 6.  v7 would have printed 7 for the same
# file and a port that forgot the multiply would print 2; the case tells all three apart,
# which a fixture of under one block could not do.
#
# `empty' is the other edge and costs nothing: no bytes at all is checksum 00000 -- the one
# case where the %05u zero padding is visible -- and zero blocks rather than one.

# `cmake -E' cannot make a file of a given size, so this is dd, as ../../mkfs/test does it.
# tr turns the zeros into `a' so that the checksum is a real one rather than the checksum of
# nothing repeated.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/big.bin
    COMMAND sh -c "dd if=/dev/zero bs=3073 count=1 2>/dev/null | tr '\\0' 'a' >big.bin"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "cmd/sum/test: big.bin, 3073 bytes -- one past a filesystem block" VERBATIM)

set(SUM_FIXTURES text.txt utf8.txt)
set(_copied ${CMAKE_CURRENT_BINARY_DIR}/big.bin)
foreach(f ${SUM_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/sum/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(sum_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests sum_fixtures)
endif()

b6_progtest(sum stdin) # standard input, and no name printed because there is none
b6_progtest(sum file)  # ONE named file, which v7 also prints no name for: the test is
                       # `argc > 2', not `argc > 1'.  Kept, and sum.1 now says it
b6_progtest(sum two)   # two files, so both names appear
b6_progtest(sum empty) # 00000 and zero blocks
b6_progtest(sum big)   # §4: 3073 bytes is 2 blocks is 6 reported blocks (see above)
b6_progtest(sum noent) # the status is 10, v7 having added ten per unreadable file
