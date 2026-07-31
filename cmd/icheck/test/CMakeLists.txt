# icheck's b6sim cases -- task C4e.
#
# THIS IS WHERE THE PROGRAM IS DEBUGGED, and each case costs about a tenth of a second.
# ../../df/test established that a program which reads a device can be handed a whole
# filesystem here, because a flat b6fsutil image is an ordinary host file; ../../fsck/test
# established that it can be handed a BROKEN one, with `b6fsutil -c' -- a separate
# implementation of the same checks, in C++ on the host -- as the oracle for the repair.
# Both are used below.
#
# WHAT THESE CASES CANNOT SAY, and for task C4e nothing else says it either: b6sim's read(2)
# and write(2) are the host's, so none of the five conditions of the raw path
# (../../df/README.md) exists here.  A green cmd_icheck_* says the arithmetic is right and
# nothing whatever about the device.  ../../fsck and ../../mkfs each have a SIMH test that
# closes that gap for themselves; C4e deliberately had none, and task C4f closed it --
# ../../../kernel/test/mount.sh section 6 runs icheck, dcheck, ncheck and clri, and `icheck
# -s' as well, over /dev/rmd1 on a pack that has just been mounted, written and unmounted.
#
# NOTHING NEEDS MASKING HERE, unlike ../../fsck/test: no program of task C4e calls getpw(3)
# or getpwent(3), so none of them prints a user name and none of them reads the build
# machine's /etc/passwd.  That is why the read-only cases below are plain b6_progtest()s.

# The image must be byte-reproducible or its block counts are not expectations, so the
# timestamp is fixed exactly as ../../df/test fixes its own.  600 blocks and a 519-block
# file; icheckimg.manifest says why 519 and nothing else would do.
set(ICHECKIMG_TIME 1700000000)
set(ICHECKIMG_BLOCKS 600)

# One 64-byte line, repeated -- ../../df/test/CMakeLists.txt's arrangement, and for its
# reason: nothing binary is checked in.  64 divides 3072, so a size in lines is a size in
# blocks exactly.
set(ICHECKIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

# df's fixture builds its files by appending one line at a time, which is fine at 144 lines
# and is not at 24,912: this doubles a unit and picks the bits of the count out of it, so a
# 1.6 MB file costs fifteen appends rather than twenty-five thousand.  The order of the
# pieces does not matter, every line being identical.
function(icheckimg_file name nlines)
    set(_unit "${ICHECKIMG_LINE}")
    set(_c "")
    set(_n ${nlines})
    while(_n GREATER 0)
        math(EXPR _bit "${_n} % 2")
        if(_bit EQUAL 1)
            string(APPEND _c "${_unit}")
        endif()
        string(APPEND _unit "${_unit}")
        math(EXPR _n "${_n} / 2")
    endwhile()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.d/${name} "${_c}")
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.d)
icheckimg_file(small 1)      #        64 bytes -- one block
icheckimg_file(huge 24912)   # 1,594,368 bytes -- 519 blocks, so exactly one of them is
                             # reached through the DOUBLE indirect.  icheckimg.manifest.

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${ICHECKIMG_BLOCKS} -T ${ICHECKIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/icheckimg.manifest icheckimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/icheckimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/icheck/test/icheckimg.img" VERBATIM)
add_custom_target(icheck_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.img)

#
# One damaged copy of the fixture, made at BUILD time rather than inside a test script.  A
# case that only READS a broken filesystem needs no driver of its own, so this is what lets
# the read-only cases below stay plain b6_progtest()s.  The targets are symbolic -- the
# block and word offsets are b6fsutil's own tables' business, ../../TODO.md's rule -- but a
# block NUMBER in a spec is this fixture's own layout and is a literal, exactly as
# ../../fsck/test/dup.damage's is.
#
function(icheck_damaged name)
    set(_img ${CMAKE_CURRENT_BINARY_DIR}/${name}.img)
    set(_cmds COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.img ${_img})
    foreach(_spec ${ARGN})
        list(APPEND _cmds COMMAND $<TARGET_FILE:b6fsutil> -D ${_spec} ${_img})
    endforeach()
    add_custom_command(OUTPUT ${_img} ${_cmds}
        DEPENDS icheck_fsimg b6fsutil
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "b6fsutil -D cmd/icheck/test/${name}.img" VERBATIM)
    add_custom_target(icheck_${name}img DEPENDS ${_img})
    if(TARGET build_tests)
        add_dependencies(build_tests icheck_${name}img)
    endif()
endfunction()

# Inode 5 is /small and its one block is 536; inode 4 is /huge and its first is 14.  Both
# are properties of a fixture whose manifest and timestamp are fixed, so they are stable --
# and if the manifest ever changes, cmd_icheck_missing's `536 missing' fails and says so.
icheck_damaged(badblk  i5.addr0=9999) # a block number past the end of a 600-block volume
icheck_damaged(dup     i5.addr0=14)   # /small's block is now /huge's first block too
icheck_damaged(missing i5.addr0=0)    # block 536 is now in no file: lost space
icheck_damaged(notafs  sb.magic=0)    # the check v7's icheck had nothing to make

if(TARGET build_tests)
    add_dependencies(build_tests icheck_fsimg)
endif()

# DEST, because icheck is /etc/icheck and not /bin/icheck -- see ../CMakeLists.txt.
b6_progtest(icheck clean   DEST etc/icheck) # six direct, single AND double indirect, and
                                            # the KBPB multiply: used+free is (fsize-isize)*3
b6_progtest(icheck blist   DEST etc/icheck) # -b, on a block that really is in a file
b6_progtest(icheck missing DEST etc/icheck) # -m: a block NUMBER, unconverted, beside a
                                            # `missing' TOTAL that is converted
b6_progtest(icheck badblk  DEST etc/icheck) # an address outside fmin..fmax
b6_progtest(icheck dup     DEST etc/icheck) # two inodes claiming one block
b6_progtest(icheck notafs  DEST etc/icheck) # s_magic
b6_progtest(icheck nodev   DEST etc/icheck) # the diagnostic, and exit 4
b6_progtest(icheck badflag DEST etc/icheck) # v7 fell through this into check("-q")
b6_progtest(icheck salvagebare DEST etc/icheck) # -s with no filesystem named is refused,
                                            # where a plain icheck takes the default

#
# The two cases that need more than one run of one program.
#
if(B6SIM)
    # -s: the only thing in task C4e that writes.  Six oracles; run-icheck-test.sh lists
    # them, and the last two hold icheck against fsck -- the other free-list rebuilder on
    # this image -- first for agreement and then for byte-identity.
    add_test(NAME cmd_icheck_salvage
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-icheck-test.sh
                ${B6SIM} $<TARGET_FILE:b6fsutil>
                ${B6_ROOTFS}/etc/icheck ${B6_ROOTFS}/etc/fsck
                ${CMAKE_CURRENT_SOURCE_DIR} salvage
                ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.img
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_icheck_salvage PROPERTIES LABELS cmd)

    # clri throws an i-node away and the other two notice.  It is registered HERE rather
    # than in ../../clri/test because that directory is configured first and cannot hang
    # b6prog_icheck on build_tests; run-icheck-cleared.sh's header says so.
    add_test(NAME cmd_icheck_cleared
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-icheck-cleared.sh
                ${B6SIM} $<TARGET_FILE:b6fsutil>
                ${B6_ROOTFS}/etc/clri ${B6_ROOTFS}/etc/icheck ${B6_ROOTFS}/etc/dcheck
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_BINARY_DIR}/icheckimg.img 5
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_icheck_cleared PROPERTIES LABELS cmd)

    if(TARGET build_tests)
        add_dependencies(build_tests b6prog_clri b6prog_dcheck b6prog_fsck)
    endif()
endif()
