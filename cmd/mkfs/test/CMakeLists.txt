# mkfs's b6sim cases -- task C4c, and the strongest oracle in the whole of C4.
#
# THE POINT OF THIS DIRECTORY IS cmd_mkfs_layout, and it is worth understanding why it can
# be a BYTE-FOR-BYTE compare when nothing else here manages one.  b6fsutil's `-n' is the
# host's mkfs (cmd/fsutil/create.cpp) and ../mkfs.c is a transcription of it, so the two
# write the same image -- provided the fixture starts as zeros (Image::create() zeroes the
# whole volume, and mkfs writes only the i-list, the chain blocks, the root's directory
# block and the superblock) and provided the four timestamps agree.  The timestamps are the
# only obstacle and they are six bytes at a known offset: s_time is word 6 of block 1, so
# run-mkfs-test.sh reads it back out of what the guest wrote and hands it to `b6fsutil -T'.
# Everything else -- 614,400 bytes of it -- then has to match exactly.
#
# That makes this the place where the LAYOUT is debugged, in a tenth of a second, with a
# diffable failure.  kernel/test/mkfs afterwards is about the DEVICE PATH and nothing else.
#
# WHAT THIS HARNESS STILL CANNOT SAY, as df's directory says of its own: none of the four
# alignment rules exists here, b6sim's read(2) and write(2) being the host's, so an
# unaligned buffer or a partial-block count would pass here and fail with EFAULT on the
# real kernel.  It also cannot say anything about a raw WRITE, which is the thing this task
# added -- kernel/test/mkfs is where that is asserted.  And time(2) here is the build
# machine's clock, which is precisely why the timestamp is recovered from the output rather
# than fixed in advance.
#
# THE FIXTURE IS A BLANK, NOT A FILESYSTEM.  If it were already an image, a mkfs that wrote
# nothing at all would pass every check below.  It is a file of exactly N zero blocks, which
# is also what makes the byte compare meaningful: every byte mkfs did not write is a byte
# b6fsutil did not write either.

set(FSIMG_BLOCKS 200)
set(SHORT_BLOCKS 40)

# `cmake -E' cannot make a file of a given size, so this is dd.  Two fixtures: the blank the
# layout case builds a filesystem in, and a deliberately SHORT one, which is how the probe
# in mkfs's main() is tested -- it reads the last block before writing the first, so asking
# for 200 blocks of a 40-block file fails with nothing written.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/blank.img
                          ${CMAKE_CURRENT_BINARY_DIR}/short.img
    COMMAND sh -c "dd if=/dev/zero of=blank.img bs=3072 count=${FSIMG_BLOCKS} 2>/dev/null"
    COMMAND sh -c "dd if=/dev/zero of=short.img bs=3072 count=${SHORT_BLOCKS} 2>/dev/null"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "cmd/mkfs/test: blank fixtures" VERBATIM)
add_custom_target(mkfs_blank DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/blank.img)

# ctest does not build, so the fixture has to hang on the target `make test' does build --
# the same reason b6_progtest() hangs the program itself there.
if(TARGET build_tests)
    add_dependencies(build_tests mkfs_blank)
endif()

# The layout oracle.  b6_progtest() diffs stdout and checks a status and can do no more, so
# this one is a custom add_test; cmd/sh/test/CMakeLists.txt is the precedent for that shape.
if(B6SIM)
    add_test(NAME cmd_mkfs_layout
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run-mkfs-test.sh
                ${B6SIM} $<TARGET_FILE:b6fsutil> ${B6_ROOTFS}/etc/mkfs ${FSIMG_BLOCKS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cmd_mkfs_layout PROPERTIES LABELS cmd)
    if(TARGET build_tests AND TARGET b6prog_mkfs)
        add_dependencies(build_tests b6prog_mkfs)
    endif()
endif()

# The diagnostics.  Each of these is a way of getting the arguments wrong, and each must
# leave the volume untouched -- which is what the ordering in main() is arranged for: every
# check but the i-list one happens before a single write.
b6_progtest(mkfs usage DEST etc/mkfs) # no arguments at all
b6_progtest(mkfs nodev DEST etc/mkfs) # a special that is not there
b6_progtest(mkfs junk  DEST etc/mkfs) # a size that is not a number
b6_progtest(mkfs tiny  DEST etc/mkfs) # 4 blocks: below the floor, and refused before open()
b6_progtest(mkfs short DEST etc/mkfs) # 200 blocks asked of a 40-block file: the probe fires
