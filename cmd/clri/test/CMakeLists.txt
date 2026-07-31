# clri's b6sim cases -- task C4e.
#
# WHAT IS HERE IS THE ARGUMENT HANDLING AND THE RANGE CHECK, and the range check is the
# reason there is a filesystem at all: v7's clri seeks wherever atoi() names, so
# `clri /dev/rmd0 99999' writes 3,072 bytes of zeros over a DATA block with no diagnostic.
# This port bounds the i-number against the superblock's i-list, and `badarg' is that.
#
# WHAT clri DOES is asserted in ../../icheck/test, as cmd_icheck_cleared: what it leaves
# behind is exactly what icheck and dcheck exist to notice, so the case needs all three
# programs -- and cmd/clri is configured before both of them, so it cannot hang their
# targets on build_tests.  ../../icheck/test/run-icheck-cleared.sh's header says so at
# length.  It also records the polarity, which is unlike anything else in the tree: clri is
# the only program here whose success is `b6fsutil -c' FAILING afterwards.
#
# WHAT THESE CASES CANNOT SAY, and for task C4e nothing else says it either: b6sim's
# write(2) is the host's, so none of the five conditions of the raw path
# (../../df/README.md, ../../mkfs/README.md) exists here -- and clri WRITES.  C4e
# deliberately has no SIMH test; ../../TODO.md carries that as this task's loose end.

# Byte-reproducible, ../../df/test's rule.  Twenty blocks: nothing here cares what is on the
# volume, only that there is an i-list to be outside of.  clriimg.manifest says why this
# directory builds its own fixture rather than copying one.
set(CLRIIMG_TIME 1700000000)
set(CLRIIMG_BLOCKS 20)
set(CLRIIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/clriimg.d)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/clriimg.d/small "${CLRIIMG_LINE}")

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/clriimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${CLRIIMG_BLOCKS} -T ${CLRIIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/clriimg.manifest clriimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/clriimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/clri/test/clriimg.img" VERBATIM)
add_custom_target(clri_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clriimg.img)

# A superblock with no magic number, made at build time so the case needs no driver script.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/notafs.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/clriimg.img ${CMAKE_CURRENT_BINARY_DIR}/notafs.img
    COMMAND $<TARGET_FILE:b6fsutil> -D sb.magic=0 ${CMAKE_CURRENT_BINARY_DIR}/notafs.img
    DEPENDS clri_fsimg b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil -D cmd/clri/test/notafs.img" VERBATIM)
add_custom_target(clri_notafsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/notafs.img)

# ctest does not build, so the fixtures hang on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests clri_fsimg clri_notafsimg)
endif()

# DEST, because clri is /etc/clri and not /bin/clri -- see ../CMakeLists.txt.
b6_progtest(clri usage  DEST etc/clri) # no arguments at all
b6_progtest(clri nodev  DEST etc/clri) # the diagnostic, and exit 4
b6_progtest(clri badarg DEST etc/clri) # zero, not-a-number, and the range check v7 has not.
                                       # All three are reported and NOTHING is written --
                                       # clri checks every argument before it clears any
b6_progtest(clri notafs DEST etc/clri) # s_magic: without a superblock there is no i-list to
                                       # bound an i-number against, so this must refuse
