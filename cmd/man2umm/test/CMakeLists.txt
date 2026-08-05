#
# Unit tests for the cmd/man2umm engine.
#
add_executable(man2umm_test man2umm_test.cpp)
target_link_libraries(man2umm_test PRIVATE umm GTest::gtest_main)
gtest_discover_tests(man2umm_test PROPERTIES RUN_SERIAL TRUE)
add_dependencies(build_tests man2umm_test)

#
# THE CORPUS TESTS, one per manual page in the tree.  Two kinds, and they assert
# different things -- ../README.md and ../../../scripts/mancheck.sh have the argument:
#
#   man_lint_<page>    canonical shape (doc/Manual_Page_Format.md section 9), run on
#                      the .umm alone.  PERMANENT: it is what holds a hand-edited
#                      page to the format once the roff it came from is gone.
#   man_oracle_<page>  the .umm against the host groff's rendering of the .1 it was
#                      converted from, along three axes.  Registered only where both
#                      files still exist, so it retires page by page as the roff does.
#
# The oracle is guarded on groff being installed, the way the top-level CMakeLists.txt
# guards cppcheck: a tree without it still configures, and simply checks less.
#
find_program(GROFF NAMES groff)
if(GROFF MATCHES "NOTFOUND")
    message(STATUS "groff not found -- the manual-page oracle (man_oracle_*) is skipped")
endif()

file(GLOB_RECURSE B6_MAN_PAGES
     ${CMAKE_SOURCE_DIR}/cmd/*.umm
     ${CMAKE_SOURCE_DIR}/lib/*.umm
     ${CMAKE_SOURCE_DIR}/include/*.umm)

foreach(_page ${B6_MAN_PAGES})
    get_filename_component(_base ${_page} NAME_WE)
    get_filename_component(_full ${_page} NAME)
    string(REGEX REPLACE "\\.umm$" "" _roff_name ${_full})
    get_filename_component(_dir ${_page} DIRECTORY)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" _tag "${_roff_name}")

    add_test(NAME man_lint_${_tag}
             COMMAND $<TARGET_FILE:b6man2umm> -l ${_page})
    set_tests_properties(man_lint_${_tag} PROPERTIES LABELS man)

    if(NOT (GROFF MATCHES "NOTFOUND") AND EXISTS ${_dir}/${_roff_name})
        add_test(NAME man_oracle_${_tag}
                 COMMAND sh ${CMAKE_SOURCE_DIR}/scripts/mancheck.sh
                         ${_dir}/${_roff_name} ${_page} $<TARGET_FILE:b6man2umm>)
        set_tests_properties(man_oracle_${_tag} PROPERTIES LABELS man)
    endif()
endforeach()

# ctest does not build, and both kinds run b6man2umm.
if(TARGET build_tests)
    add_dependencies(build_tests b6man2umm)
endif()
