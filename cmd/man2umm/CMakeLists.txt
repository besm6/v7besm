#
# b6man2umm -- roff -man to Unix Manual Markdown.
#
# The format is doc/Manual_Page_Format.md; this tool converts a page into it and
# then holds the result to it.  It is a HOST tool and only a host tool: it is not
# cross-compiled, there is no rootfs/ subdirectory, and nothing runs it on the
# BESM-6.  (Nor could it be: b6cc drives b6parse, which is strict C11, and there is
# no C++ cross-compiler for this machine.  cmd/sim and cmd/fsutil are C++ for the
# same reason this is -- they run on the build machine and nowhere else.)
#
# IT IS NOT A RUN-ONCE TOOL.  The 200 pages this tree carried were the first job,
# not the only one: every v7 program still to be ported (../TODO.md C10-C24, and the
# unported pages already sitting in ../awk/, ../make/ and the rest) arrives with a
# roff page that has to become a .md.  ./README.md is the standing procedure and
# ../../scripts/mancheck.py is its verification step.
#
# The engine is split into a static library, as cmd/nm's is and for cmd/nm's reason:
# the GoogleTest suite in test/ links the same code the binary does.  The library is
# called `umm' rather than `man2umm' because ummread.cpp is a reader of the DIALECT
# and not of roff: it is the half that outlives the conversion, and cmd/manview
# (task C25a, the renderer on the image) was written from it as a SKELETON rather than
# by linking it -- that program is C11 and this library is C++.  Neither is cmd/man,
# which is C25b's program and shares no code with either.  cmd/manview/parse.c is the
# transliteration and its header says where the two must go on agreeing.
#
add_library(umm STATIC
    model.cpp       # diagnostics
    escape.cpp      # roff escapes, and the name(N) cross-reference rule
    roff.cpp        # the -man reader
    emit.cpp        # the dialect writer, the font stream and the model dump
    ummread.cpp      # the dialect READER -- the half that outlives the conversion
    lint.cpp        # canonical shape, doc/Manual_Page_Format.md section 9
    run.cpp)        # the driver, so that test/ can call it
target_include_directories(umm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(b6man2umm main.cpp)
target_link_libraries(b6man2umm PRIVATE umm)
install(TARGETS b6man2umm RUNTIME DESTINATION bin)

add_subdirectory(test)
