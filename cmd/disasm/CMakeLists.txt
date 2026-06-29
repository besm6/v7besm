#
# Disassembler for BESM-6.
#
# Built from the host C compiler: it runs on the host and reads BESM-6 a.out
# objects, decoding the 48-bit instructions and printing either the ASCII
# (MADLEN) or the Cyrillic (BEMSH) mnemonics.  The fget* helpers it needs are
# shared with the rest of the toolchain and come from cmd/libaout (aout).
#
# The engine is split into a reusable library so it can be linked into both the
# command-line tool and the unit tests.
#
add_library(disassembler STATIC dis.c)
target_include_directories(disassembler PUBLIC ${BESM6_CROSS_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(disassembler PUBLIC aout)

add_executable(b6disasm main.c)
target_link_libraries(b6disasm PRIVATE disassembler)
install(TARGETS b6disasm RUNTIME DESTINATION bin)

add_subdirectory(test)
