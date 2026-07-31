# split's b6sim cases -- task C5a.
#
# THREE OF THE FOUR CASES BELOW ARE DIAGNOSTICS THAT DID NOT EXIST, which is unusual and is
# the shape of this port: nothing in split is this machine's business -- no §2, no §3, no §11
# -- and what the work went into was three upstream bugs and one piece of sloppiness.
# ../split.c's header has all four; the cases are what holds them.
#
# `zero' IS THE ONE THAT MATTERS.  `split -0' made the per-piece loop zero-trip, so v7 read
# nothing, opened nothing, called fclose(NULL) and went round again -- forever.  ../../
# README.md §9 says a program that does not terminate cannot be a b6sim case at all, so in
# the shape v7 left it this directory could not have held this case: the ctest would have hung
# rather than failed.  Refusing the count is what makes it assertable.
#
# THE FILE ORACLE IS run-split-test.sh, for ../../tee/test/CMakeLists.txt's reason -- a filter
# whose output is a directory of files needs an assertion b6_progtest() cannot make.  It is
# also where the 676-piece bound is reached, which needs a 677-line input the script builds
# rather than a fixture anybody has to look at.

b6_progtest(split zero)     # the loop that did not terminate
b6_progtest(split badopt)   # v7 ignored an unknown option and split into 1000-line pieces
b6_progtest(split noinput)  # an input that is not there -- v7's message, unchanged
b6_progtest(split longname) # 98 bytes of prefix into a 100-byte buffer, plus two suffix
                            # letters and a NUL: v7 wrote past the end

if(B6SIM)
    add_test(NAME cmd_split_pieces
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-split-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/split
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_split_pieces PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_split)
        add_dependencies(build_tests b6prog_split)
    endif()
endif()
