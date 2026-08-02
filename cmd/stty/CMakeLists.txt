# /bin/stty -- Unix v7 stty(1), built natively for the BESM-6.
#
# Task C6 (../TODO.md), and the one program in it whose port IS a cut: v7's two tables
# describe a dial-up terminal, and most of what they name is inert on a Consul-254.  Eleven
# groups of entries came out, measured against kernel/dev/tty.c rather than assumed --
# stty.c's header is the list and README.md is the account.  ../getty is the precedent: the
# same driver took the same cut to its speed table one task earlier.
#
# NOT SETUID: gtty(2) and stty(2) are gated on nothing but holding the descriptor open, and
# what they change belongs to the terminal the caller is already sitting at.
#
# NO b6sim TESTS, and there is no test/ subdirectory.  ../README.md §9 says that world has
# no terminal -- its ioctl is an unconditional success that changes nothing -- so every case
# would be asserting the harness rather than the program.  It is tested in
# ../../kernel/test/multi, on a real Consul whose modes scopen() set.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/sh/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(stty DEST bin/stty SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stty.c)
