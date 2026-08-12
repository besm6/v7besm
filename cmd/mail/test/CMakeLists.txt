# mail(1) cases, task C28.  ../README.md's "What this harness cannot say" is the other half
# of this file: most of what mail does is about /usr/spool/mail, and under b6sim that is the
# BUILD MACHINE's (../../README.md SS9).
#
# THE FIXTURES ARE COPIED HERE AND READ BY A RELATIVE NAME, which is unlike every other test
# directory in this tree and is not a preference.  Two reasons, and the first is the sharp one:
#
#   1. EVERY MUTATING COMMAND REWRITES THE MAILBOX.  `d', a successful `s' or `w', and `m' all
#      set `changed', and copyback() then rewrites whatever -f named.  Under `-f @srcdir@/mbox'
#      that file is the checked-in fixture, so a case that deletes a letter would edit the
#      source tree and every later run would see a different mailbox.  Every interactive case
#      below therefore ends in `x' or `q', and none of them may exercise copyback().
#   2. `@srcdir@' is an ABSOLUTE path, and mail bounds -f at FILENAME_MAX (128).  A deep enough
#      checkout would push `@srcdir@/mbox' past the bound and the case would fail with `file
#      name too long' on somebody else's machine and nowhere else.
#
# A relative name in the working directory answers both.  mail's own -f also skips the lock
# (mboxlock()'s privfile), so nothing here chmods a fixture either.
foreach(f mbox many)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${f}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/${f} ${CMAKE_CURRENT_BINARY_DIR}/${f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${f}
        COMMENT "stage mail test fixture ${f}" VERBATIM)
    list(APPEND MAIL_FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/${f})
endforeach()
add_custom_target(mail_fixtures ALL DEPENDS ${MAIL_FIXTURES})
if(TARGET build_tests)
    add_dependencies(build_tests mail_fixtures)
endif()

# The diagnostics, which need no mailbox at all.
b6_progtest(mail nomail)   # no mailbox on the build machine, so the No mail. path -- and the
                           # one case whose answer does not depend on who is building
b6_progtest(mail badopt)   # v7 exited 0 after this diagnostic; it exits 1 now
b6_progtest(mail nofile)
b6_progtest(mail longpath) # the -f bound (../../README.md SS6): v7 strcpy'd argv[2] into 40 bytes

# Reading a mailbox.  printall and forward are the same fixture read two ways, which is SS9's
# designed-fixture oracle: LIFO is the default and -r is first-in-first-out, so each is the
# other's control.  The fixture carries a Cyrillic letter and a `>From ' quoted line -- SS11
# needs a positive case and this is where it costs nothing.
b6_progtest(mail printall)
b6_progtest(mail forward)
b6_progtest(mail toomany)  # 301 letters: v7 stored past let[MAXLET] into bss

# The command loop, all of it non-mutating -- see the note at the top of this file.
b6_progtest(mail quit)
b6_progtest(mail help)     # an unknown command falls through `default' into the summary
b6_progtest(mail navigate)
b6_progtest(mail savefail)
b6_progtest(mail saveillegal)
b6_progtest(mail mailillegal) # stops before the fork, which is why it can be a case here

# Sending.  Nothing is ever delivered: /usr/spool/mail is the build machine's and is not there.
# unknownuser and knownuser are a PAIR -- the first says getpwnam() failed, the second says it
# SUCCEEDED and the mailbox is what was missing, and neither means much without the other.
#
# knownuser is the one case in this directory that asserts something about the HOST (SS9's
# "ask whose it is"): it expects /usr/spool/mail/root to be absent, which it is on macOS and on
# any Linux, both of which spool mail elsewhere.  On a machine where that path existed and were
# writable, this case would append to a real mailbox.
b6_progtest(mail emptyletter) # flgf: an empty letter is not sent, and nothing is touched
b6_progtest(mail unknownuser)
b6_progtest(mail knownuser)
b6_progtest(mail bangname)    # a `!' in a name is the whole uucp refusal, task C28

# dead.letter is written into this directory by three of the cases above, concurrently under a
# parallel ctest.  Nothing reads it, so the race is benign -- but no fixture may be named that,
# any more than it may be named <case>.out.
