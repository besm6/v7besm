# tee's b6sim cases -- task C5a, and the directory where this phase's second harness idea
# lives: an ORACLE OVER THE FILES A FILTER CREATED, which b6_progtest() cannot express.
# run-tee-test.sh's header is the argument; the short form is that b6_progtest diffs standard
# output and nothing else, and tee's job is the files.
#
# WHAT THE STREAM CASES BELOW STILL EARN.  `plain' is tee with no files at all, which is cat
# -- the copy path, the read/write loop and the BSIZE buffers that replaced v7's 512-byte
# ones.  `utf8' is the same path over bytes above 0177, which tee is transparent to because
# it copies char to char and never inspects one (§11); it costs a fixture and asserts that
# nothing in the port introduced a mask.
#
# `many' IS THE BOUND v7 DID NOT HAVE.  openf[] was twenty ints and the argument loop wrote
# past it into `n', `t' and `aflag' -- the next three objects -- so a twentieth output file
# corrupted the program's own state instead of being refused.  The case hands it twenty names
# and requires the diagnostic; nineteen empty files land in this directory's build tree as a
# side effect, which is the cheap price of asserting the edge.  Note the exit status is 0:
# tee reports and carries on with the outputs it has, which is v7's shape for `cannot open'
# and is kept for this.

set(TEE_FIXTURES data.txt)
set(_copied)
foreach(f ${TEE_FIXTURES})
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "cmd/tee/test: ${f}" VERBATIM)
    list(APPEND _copied ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(tee_fixtures DEPENDS ${_copied})

# ctest does not build, so the fixtures have to hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests tee_fixtures)
endif()

b6_progtest(tee plain) # no files: the copy path on its own
b6_progtest(tee utf8)  # ... and it is byte-transparent (§11)
b6_progtest(tee many)  # twenty output files: the bound v7 walked off the end of

if(B6SIM)
    add_test(NAME cmd_tee_files
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-tee-test.sh
                ${B6SIM} ${B6_ROOTFS}/bin/tee ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_tee_files PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_tee)
        add_dependencies(build_tests b6prog_tee)
    endif()
endif()
