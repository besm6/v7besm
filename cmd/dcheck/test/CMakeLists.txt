# dcheck's b6sim cases -- task C4e.
#
# dcheck reads a FILESYSTEM, and a flat b6fsutil image is an ordinary host file, so these
# run against real on-disk structure rather than only exercising argument handling.
# ../../df/test/CMakeLists.txt is where that idea was established.
#
# THE FIXTURE IS BUILT HERE AND ../../ncheck/test COPIES IT, which is the arrangement
# ../../quot/test has with df's.  dcheckimg.manifest says why df's own image would not do --
# no hard link, no device node, no setuid file, nothing nested -- and why this directory
# could not have depended on it in any case: cmd/dcheck is configured before cmd/df.
#
# WHAT THESE CASES CANNOT SAY, and for task C4e nothing else says it either: b6sim's read(2)
# is the host's, so none of the alignment conditions of the raw path (../../df/README.md)
# exists here.  A green cmd_dcheck_* says the arithmetic is right and nothing whatever about
# the device.  C4e deliberately has no SIMH test; ../../TODO.md carries that as this task's
# one named loose end.
#
# NOTHING NEEDS MASKING, unlike ../../fsck/test: dcheck prints i-numbers and counts and
# never maps a uid to a name, so it never reads the build machine's /etc/passwd.

# Byte-reproducible or the counts are not expectations; ../../df/test fixes its timestamp
# for the same reason and this is deliberately a different number, nothing correlating them.
set(DCHECKIMG_TIME 1700000000)
set(DCHECKIMG_BLOCKS 100)
set(DCHECKIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.d)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.d/small "${DCHECKIMG_LINE}")

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${DCHECKIMG_BLOCKS} -T ${DCHECKIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/dcheckimg.manifest dcheckimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dcheckimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/dcheck/test/dcheckimg.img" VERBATIM)
add_custom_target(dcheck_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img)

#
# One damaged copy of the fixture, made at BUILD time rather than inside a test script: a
# case that only READS a broken filesystem needs no driver of its own, which is what keeps
# every case here a plain b6_progtest().  ../../icheck/test has the same function and the
# same note about symbolic targets.
#
function(dcheck_damaged name)
    set(_img ${CMAKE_CURRENT_BINARY_DIR}/${name}.img)
    set(_cmds COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img ${_img})
    foreach(_spec ${ARGN})
        list(APPEND _cmds COMMAND $<TARGET_FILE:b6fsutil> -D ${_spec} ${_img})
    endforeach()
    add_custom_command(OUTPUT ${_img} ${_cmds}
        DEPENDS dcheck_fsimg b6fsutil
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "b6fsutil -D cmd/dcheck/test/${name}.img" VERBATIM)
    add_custom_target(dcheck_${name}img DEPENDS ${_img})
    if(TARGET build_tests)
        add_dependencies(build_tests dcheck_${name}img)
    endif()
endfunction()

# Inode 6 is /f, which has two names; inode 7 is /d/e/g; entry 4 of the root is /f.  All
# three are properties of dcheckimg.manifest and of a fixed timestamp.
dcheck_damaged(linkcnt  i6.nlink=7)  # a link count above the number of names
dcheck_damaged(zerolink i7.nlink=0)  # an allocated file with no links, and one entry
dcheck_damaged(badino   e2.4=99999)  # an entry naming an i-number off the end of the i-list

if(TARGET build_tests)
    add_dependencies(build_tests dcheck_fsimg)
endif()

# DEST, because dcheck is /etc/dcheck and not /bin/dcheck -- see ../CMakeLists.txt.
b6_progtest(dcheck clean    DEST etc/dcheck) # a sound filesystem: the name and NOTHING else,
                                             # which is the cheapest oracle in this task
b6_progtest(dcheck linkcnt  DEST etc/dcheck) # 2 names, di_nlink 7
b6_progtest(dcheck zerolink DEST etc/dcheck) # 1 name, di_nlink 0
b6_progtest(dcheck badino   DEST etc/dcheck) # `bad;', and the name printed %.*s at DIRSIZ
b6_progtest(dcheck ilist    DEST etc/dcheck) # -i: BOTH names of one i-number are reported
b6_progtest(dcheck nodev    DEST etc/dcheck) # the diagnostic, and exit 1
b6_progtest(dcheck badflag  DEST etc/dcheck) # v7 fell through this into check("-q")
