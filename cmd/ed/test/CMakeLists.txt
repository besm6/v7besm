# ed's b6sim cases -- task C3.  Each is one b6_progtest(): the program AS STAGED FOR THE IMAGE
# (build/rootfs/bin/ed) run under b6sim with its system calls serviced on the host, its
# commands fed from <case>.in, and its output diffed against <case>.expected.
#
# <case>.in IS NEW WITH THIS TASK.  ed is the first program here whose whole command language
# arrives on standard input, so scripts/run-prog-test.sh grew the file; ../../README.md SS9 had
# named it as the obvious extension and task C5's filters all want it.  The redirection is
# from a FILE and not a pipe, which matters: error() calls lseek(0, 0, SEEK_END) to throw away
# the rest of a script after a diagnostic, and only a seekable descriptor can answer that.
#
# WHICH IS ALSO THE RULE FOR WRITING A CASE: ONE ERROR PER CASE.  After any `?' the rest of the
# input is discarded and ed reads EOF, so a second probe in the same file is dead text.  That
# is why badaddr, revaddr, badcmd, nofile, qmodified and markundo are six small files rather
# than six lines of one.  badcmd is the case that ASSERTS the discard: its `1,$p' never runs.
#
# EVERY CASE EXITS 0, so there is not one .status file here.  ed has no failing exit: `?' is a
# diagnostic and not a status, and the only non-zero exit in the program is the 0100 of a child
# whose exec failed.  A trailing `?' in an .expected is usually quit() refusing to leave a
# modified buffer, which is v7's behaviour and is asserted, not worked around.
#
# NO CASE MAY PRINT A PATH.  The source directory differs per machine, so `f' and the `?name'
# diagnostic are only ever exercised on a relative name -- see nofile.  readfile.args does use
# @srcdir@, because a SUCCESSFUL read prints its byte count and never its name.
#
# WHAT CANNOT BE TESTED HERE, and lives in kernel/test/edit instead: `!' (b6sim cannot exec
# /bin/sh), a delivered SIGINT, ed.hup, a real terminal, and a /tmp that belongs to this
# machine -- under b6sim the temp file is the build host's.

b6_progtest(ed append)     # a i c d p = $ . and the empty buffer
b6_progtest(ed addr)       # every address form, both search directions, marks
b6_progtest(ed regex)      # the engine: anchors, closure, classes, back-references
b6_progtest(ed subst)      # s///, &, \1, the g suffix, \& and \\ -- QESC's first producer
b6_progtest(ed globsub)    # a multi-line replacement, in a g list and typed -- QESC's second
b6_progtest(ed global)     # g, v, and the g/re/d shortcut gdelete() exists for
b6_progtest(ed move)       # m t j u, and a mark following a line through a substitute
b6_progtest(ed utf8)       # TASK C3'S ASSERTION: eight bits everywhere, and the byte 0377
b6_progtest(ed list)       # l: the escapes, the three-digit octal, DEL, the 72-column fold
b6_progtest(ed grow)       # 200 lines: crosses nlall 128 -> 640, so realloc() runs
b6_progtest(ed blocks)     # lines that straddle a 512-byte temp block, so getblock() splices
b6_progtest(ed write)      # w, W appending, f, e, and the byte counts putd() prints
b6_progtest(ed readfile)   # the `ed file' startup read, and r at an address

# One error each, for the reason the header gives.
b6_progtest(ed badcmd)     # an unknown command, and that the rest of the script was discarded
b6_progtest(ed badaddr)    # nonzero() refusing line 0
b6_progtest(ed revaddr)    # setdot() refusing a backwards range
b6_progtest(ed nofile)     # ?name on a file that is not there
b6_progtest(ed qmodified)  # q refuses a modified buffer once, then obeys
b6_progtest(ed markundo)   # a mark does NOT survive the undo of the substitute that moved it
