# ncheck's b6sim cases -- task C4e.
#
# ncheck reads a FILESYSTEM, and a flat b6fsutil image is an ordinary host file, so these
# walk a real directory tree rather than only exercising argument handling.
#
# THE FIXTURE IS ../../dcheck/test's, copied rather than rebuilt -- the arrangement
# ../../quot/test has with ../../df/test's, and for its reason: there should be exactly one
# description of what is in it (../../dcheck/test/dcheckimg.manifest), and that file says
# what each entry is for.  Three of them are ncheck's alone: the hard link, so one i-number
# comes back under two names; /d/e/g, so pname() recurses; and the setuid file and the
# device node, which are the two things `-s' exists to find.
#
# THE OUTPUT IS IN NO USEFUL ORDER and the manual page says so -- it is i-list order, not
# path order -- so these .expected files are the order the program produces and not a sorted
# one.  A test that sorted would be hiding the property the page states.
#
# WHAT THESE CASES CANNOT SAY, and for task C4e nothing else says it either: b6sim's read(2)
# is the host's, so none of the alignment conditions of the raw path (../../df/README.md)
# exists here.  A green cmd_ncheck_* says the arithmetic is right and nothing whatever about
# the device.  C4e deliberately had no SIMH test at all, and task C4f closed that gap:
# ../../../kernel/test/mount.sh section 6 runs this program over /dev/rmd1, on a pack that
# has just been mounted, written and unmounted.
#
# NOTHING NEEDS MASKING, unlike ../../fsck/test: ncheck prints i-numbers and path names, and
# never maps a uid to a name, so it never reads the build machine's /etc/passwd.

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/cmd/dcheck/test/dcheckimg.img
            ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img
    DEPENDS dcheck_fsimg
    COMMENT "copy cmd/dcheck/test/dcheckimg.img" VERBATIM)
add_custom_target(ncheck_fsimg DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dcheckimg.img)

# ctest does not build, so the fixture hangs on the target `make test' does build.
if(TARGET build_tests)
    add_dependencies(build_tests ncheck_fsimg)
endif()

# DEST, because ncheck is /etc/ncheck and not /bin/ncheck -- see ../CMakeLists.txt.
b6_progtest(ncheck all     DEST etc/ncheck) # the whole listing: three levels deep, and one
                                            # i-number under two names
b6_progtest(ncheck aflag   DEST etc/ncheck) # -a: `.' and `..' as well, so every directory
                                            # appears from inside itself and from its parent
b6_progtest(ncheck ilist   DEST etc/ncheck) # -i: both names of i-number 6 survive the filter
b6_progtest(ncheck sflag   DEST etc/ncheck) # -s: the setuid file and the device node, and
                                            # NOTHING else -- v7's missing braces make this
                                            # arm read as though it reported the first only
b6_progtest(ncheck nodev   DEST etc/ncheck) # the diagnostic, and exit 1
b6_progtest(ncheck badflag DEST etc/ncheck) # v7 fell through this into check("-q")
