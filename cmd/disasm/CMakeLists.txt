#
# Disassembler for BESM-6.
#
# Built from the host C compiler: it runs on the host and reads BESM-6 a.out
# objects, printing the Cyrillic BESM-6 mnemonics directly.
#
# NOTE: not yet wired into the top-level build — dis.c is an older "micro-BESM"
# disassembler (includes "b.out.h", word size W == 8) and needs source fixes to
# compile against the current cross headers (cross/besm6/b.out.h, W == 6).
# Building it is a separate task.
#
add_executable(b6disasm dis.c)
target_include_directories(b6disasm PRIVATE ${BESM6_CROSS_INCLUDE} ${BESM6_CROSS_INCLUDE}/besm6)
install(TARGETS b6disasm RUNTIME DESTINATION bin)
