# novi's b6sim cases -- task C12.
#
# THE EDITOR ITSELF HAS ALMOST NO b6sim HALF, and saying so is most of what this file is.
# b6sim's ioctl is an unconditional no-op and its gtty answers ENOTTY when descriptor 0 is
# not a terminal (cmd/sim/syscall.cpp), so a program whose subject is the screen can be run
# here but not driven -- lib/test/curstty.c is IMAGEONLY for exactly this reason.  What CAN
# be asserted is the two halves that have nothing to do with a terminal, and they are three
# quarters of the risk in the port:
#
#   novibuft   the disk-backed gap buffer: the block-wise migrate(), the reversal it turns
#              on, the removed ftruncate(2), and a save OVER AN EXISTING FILE, which is what
#              creat(2)-in-place is here for.
#   novikeyt   the escape decoder, driven through pipe(2)+dup2(2) -- both real under b6sim.
#              Its `Cyrillic Л' row is the assertion that 0233 is no longer read as an
#              eight-bit CSI introducer.
#
# and two cases on novi itself, which reach term_open()'s failure and stop there.  They are
# worth having anyway: buf_open() runs FIRST, so `notty' really does create the two scratch
# files, load a file into the gap block-reversed, and close them again before the diagnostic.
# Its fixture is Cyrillic so that the load path carries eight-bit bytes.
#
# NO CASE MAY NAME OR PRINT AN ABSOLUTE PATH (../../README.md SS9: argv[0] is the staged
# path and differs per machine), which is why `notty' uses @srcdir@ in its .args and novi
# never echoes the name back.
#
# THE TWO TEST PROGRAMS ARE STAGED UNDER build/rootfs/test/ AND DO NOT REACH THE DISK.
# root.manifest names every file on the image explicitly -- there is no directory walk -- so
# a binary staged outside the paths it names is built, size-checked and run, and nothing
# more.  This is the first time this tree stages something under B6_ROOTFS that is not
# destined for the image, hence the note.
#
# WHAT IS NOT HERE, AND DELIBERATELY.  There is no typed SIMH dialogue driving the editor.
# kernel/test/edit used to fail because `send after=20000 "ed\r"' came back with a stray byte
# between the two characters (kernel/TODO.md task 35, fixed), and console failed for the same
# reason; a novi dialogue would be strictly worse, its output being escape sequences with
# embedded cursor coordinates on an alternate screen rather than readable text.  A third test
# born disabled asserts nothing and costs a volume number and a simh_boot lock slot.  novi's
# interactive half goes in WITH the re-enabling of console and edit, not before.

set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/../buffer.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/../terminal.h)

b6_prog(novibuft DEST test/novibuft
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_buffer.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../buffer.c)

b6_prog(novikeyt DEST test/novikeyt
        CFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/..
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_terminal.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../terminal.c)

# The case names have to differ even though the programs do: b6_progtest() takes both the
# expectation and the .out it writes from the case name, and all four cases share this one
# directory and one binary directory.
b6_progtest(novibuft buffer DEST test/novibuft)  # the whole gap buffer, every byte verified
b6_progtest(novikeyt keys DEST test/novikeyt)    # the escape decoder, including Л

b6_progtest(novi notty)     # no terminal: buf_open() succeeds, term_open() does not
b6_progtest(novi longname)  # a name over MAXNAME, diagnosed before anything is opened
