# fsck's b6sim cases -- task C4d.
#
# THIS IS WHERE THE PROGRAM IS DEBUGGED, and each case costs about a tenth of a second.
# ../../df/test established that a program which reads a device can be handed a whole
# filesystem here, because a flat b6fsutil image is an ordinary host file; ../../mkfs/test
# established that one which WRITES a device can be handed a blank one.  This one is the
# third step: an image is damaged with `b6fsutil -D', fsck repairs it, and `b6fsutil -c'
# -- a separate implementation of the same checks, in C++ on the host -- has to agree that
# the result is sound.  run-fsck-test.sh lists the five assertions.
#
# WHAT IT STILL CANNOT SAY, and it is ../../df/README.md's warning unchanged: b6sim's
# read(2) and write(2) are the host's, so none of the five alignment conditions of the raw
# path exists here.  Everything green in this directory says the ALGORITHM is right and
# nothing whatever about the device.  kernel/test/fsck is the other half.

set(FSCKIMG_TIME 1700000000)
set(FSCKIMG_BLOCKS 200)

# One 64-byte line, repeated -- ../../df/test/CMakeLists.txt's arrangement, and for its
# reason: nothing binary is checked in.  64 divides 3072, so a size in lines is a size in
# blocks exactly.
set(FSCKIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

function(fsckimg_file name nlines)
    set(_c "")
    foreach(_i RANGE 1 ${nlines})
        string(APPEND _c "${FSCKIMG_LINE}")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fsckimg.d/${name} "${_c}")
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fsckimg.d)
fsckimg_file(small 1)   #     64 bytes -- one block
fsckimg_file(big 336)   # 21,504 bytes -- SEVEN blocks, one more than the six direct
                        # addresses, so the file has a single indirect block and iblock()
                        # is on the path.  fsckimg.manifest says why that matters.

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fsckimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${FSCKIMG_BLOCKS} -T ${FSCKIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/fsckimg.manifest fsckimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fsckimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/fsck/test/fsckimg.img" VERBATIM)
add_custom_target(fsck_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fsckimg.img)

# The `clean' case runs against DF's fixture rather than this one, which makes the pair a
# cross-check instead of a duplication: two independently built filesystems that must both
# come out clean, and a free count fsck reaches by walking the list that must equal the
# 555 cmd_df_image asserts df prints.  ../../README.md asked C4d to reuse df's image; this is
# the half of it that is reusable, and fsckimg.manifest says why the rest is not.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dfimg.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/cmd/df/test/fsimg.img ${CMAKE_CURRENT_BINARY_DIR}/dfimg.img
    DEPENDS df_fsimg
    COMMENT "copy cmd/df/test/fsimg.img" VERBATIM)
add_custom_target(fsck_dfimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dfimg.img)

if(TARGET build_tests)
    add_dependencies(build_tests fsck_fsimg fsck_dfimg)
endif()

# ctest does not build, so the program has to hang on the target `make test' does.
if(TARGET build_tests AND TARGET b6prog_fsck)
    add_dependencies(build_tests b6prog_fsck)
endif()

#
# One damage case.  b6_progtest() cannot be used -- it diffs raw stdout and one field of
# fsck's output is the build machine's /etc/passwd; run-fsck-test.sh's header says why.
#
function(fsck_case name fixture)
    if(B6SIM)
        add_test(NAME cmd_fsck_${name}
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-fsck-test.sh
                    ${B6SIM} $<TARGET_FILE:b6fsutil> ${B6_ROOTFS}/etc/fsck
                    ${CMAKE_CURRENT_SOURCE_DIR} ${name} ${fixture}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(cmd_fsck_${name} PROPERTIES LABELS cmd)
    endif()
endfunction()

fsck_case(clean     dfimg.img)   # df's image, read-only: five banners and a free count
                                 # that must equal what cmd_df_image asserts df prints
fsck_case(linkcnt   fsckimg.img) # phase 4 ADJUST
fsck_case(unref     fsckimg.img) # phase 4 RECONNECT of a file into /lost+found
fsck_case(unrefdir  fsckimg.img) # phase 3's orphan walk, and chgdd() rewriting `..'
fsck_case(dup       fsckimg.img) # phase 1 DUP, phase 1b, phase 2 DUP/BAD, phase 4, phase 6
fsck_case(badblk    fsckimg.img) # a block number past the end of the volume
fsck_case(unknown   fsckimg.img) # a file type ftypeok() does not know
fsck_case(badfree   fsckimg.img) # a bad block IN the free list, then salvage
fsck_case(freecnt   fsckimg.img) # a free-list count outside 1..NICFREE
fsck_case(tcounts   fsckimg.img) # the two dead counters: a NOTE, no question, no write
fsck_case(notafs    fsckimg.img) # s_magic: the check v7's fsck did not have

# The three that need no filesystem at all, and so no masking.
b6_progtest(fsck usage DEST etc/fsck)
b6_progtest(fsck nodev DEST etc/fsck)
b6_progtest(fsck bothflags DEST etc/fsck)
