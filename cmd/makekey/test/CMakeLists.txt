# b6sim cases for makekey(8) -- task C19.  DEST must repeat the string ../CMakeLists.txt's
# b6_prog() was given; the program is not in bin/.
#
# key.in is eight bytes of key and two of salt, chosen printable so the fixture is readable:
# `besm6.v7' and `aQ'.  The expected thirteen bytes are the host crypt(3)'s, which
# ../../../lib/test/pwent.c already pins against this libc's -- crypt("besm6","aQ") is
# "aQR1E1yZh1bPE" there -- so the oracle is a second implementation and not a transcript.
b6_progtest(makekey key DEST usr/lib/makekey)

# v7 ignored both read(2) results and wrote thirteen bytes derived from whatever the stack
# held.  Five bytes in, a diagnostic and status 1 out.
b6_progtest(makekey short DEST usr/lib/makekey)
