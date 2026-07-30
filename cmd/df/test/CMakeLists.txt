# df's b6sim cases -- task C4a, and the harness the rest of C4 inherits.
#
# EVERY EARLIER cmd/ TEST DIRECTORY ASSERTS ARGUMENT HANDLING AND LITTLE ELSE, because
# b6sim services each system call on the host and there is nothing of this machine left to
# look at.  df is the first program here for which that is not the whole story: what it
# reads is a FILESYSTEM, and a flat b6fsutil image is an ordinary host file.  So the fixture
# below is a real v7 filesystem, and `cmd_df_image' walks its real free list and counts real
# blocks -- in a tenth of a second, with no boot.  mkfs(1) (C4c) and fsck(1) (C4d) want
# exactly this and should copy it rather than invent a second one.
#
# WHAT IT STILL CANNOT SAY.  None of the four alignment rules df.c's header lists exists
# here: b6sim's read(2) is the host's, so an unaligned buffer, a partial-block count and an
# off-block seek all succeed under this harness and all fail with EFAULT -- or, for the
# seek, silently -- on the real kernel.  A green `cmd_df_image' therefore says the free-list
# arithmetic is right and says NOTHING about the device path.  kernel/test/fsinfo is where
# that is asserted, and ../README.md is the account.
#
# NOTHING BINARY IS CHECKED IN.  The fixture's files are runs of one 64-byte line, written
# at configure time, and the image is built from them at build time; fsimg.manifest says
# why each size is the size it is.

# The image must be byte-reproducible or its free-block count is not an expectation, so the
# timestamp is fixed exactly as ROOTTIME fixes root.img's (kernel/test/CMakeLists.txt).  It
# is a different number on purpose: nothing correlates the two, and reusing ROOTTIME would
# suggest something does.
set(FSIMG_TIME 1700000000)
set(FSIMG_BLOCKS 200)

# One 64-byte line, repeated.  64 divides 3072, so a size in lines is a size in bytes and
# `mid' lands on a block boundary exactly rather than nearly.
set(FSIMG_LINE "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\n")

# string(REPEAT) is CMake 3.15 and this project requires 3.10, so the loop is the portable
# spelling.  The files are small; this costs nothing at configure time.
function(fsimg_file name nlines)
    set(_c "")
    foreach(_i RANGE 1 ${nlines})
        string(APPEND _c "${FSIMG_LINE}")
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/fsimg.d/${name} "${_c}")
endfunction()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fsimg.d)
fsimg_file(small 1)     #    64 bytes -- 1 block, reported as 3
fsimg_file(mid 48)      #  3072 bytes -- exactly 1 block, reported as 3: the off-by-one
fsimg_file(big 144)     #  9216 bytes -- 3 blocks, all direct, reported as 9
fsimg_file(other 24)    #  1536 bytes -- half a block, so 1, reported as 3
fsimg_file(nested 2)    #   128 bytes -- 1 block under a directory, reported as 3

# The manifest's `source' paths resolve against the WORKING_DIRECTORY below, which is why
# fsimg.d/ is generated into the build tree and named relatively -- the same arrangement
# ../../../root.manifest has with kernel/test's build directory.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img
    COMMAND $<TARGET_FILE:b6fsutil> -n -s ${FSIMG_BLOCKS} -T ${FSIMG_TIME}
            -M ${CMAKE_CURRENT_SOURCE_DIR}/fsimg.manifest fsimg.img
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fsimg.manifest b6fsutil
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6fsutil cmd/df/test/fsimg.img" VERBATIM)
add_custom_target(df_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fsimg.img)

# ctest does not build, so the fixture has to hang on the target `make test' does build --
# the same reason b6_progtest() hangs the program itself there.
if(TARGET build_tests)
    add_dependencies(build_tests df_fsimg)
endif()

b6_progtest(df image)   # the free list of a real filesystem, walked and counted -- in the
                        # 1024-byte block df REPORTS in, so 555 where the image has 185
                        # free filesystem blocks.  b6fsutil -v says the latter.
b6_progtest(df nodev)   # the diagnostic, and that v7's df still exits 0 after it
