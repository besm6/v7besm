#
# lorder: topological ordering helper for the BESM-6 archiver/linker.
# A shell script — configured, not compiled.
#
# ONE SOURCE, TWO COPIES, and the only difference between them is which nm the
# script reaches for when the caller sets none: the host tool is b6nm, and on the
# disk image (task C9c) it is /usr/bin/nm.  @NM@ is the whole of the difference,
# which is why this is a configure_file() and not a second script.
#
# lorder.sh.in is otherwise written to parse under v7's sh as well as the host's --
# backquotes rather than $( ), ${name-word} rather than ${name:-word}; its own
# comments say so.  Everything it shells out to (nm, sed, sort, join, basename, and
# tsort at the far end of the pipe) is on the image already.
#
set(NM b6nm)
configure_file(lorder.sh.in ${CMAKE_CURRENT_BINARY_DIR}/lorder.sh @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/lorder.sh DESTINATION bin RENAME b6lorder)

add_subdirectory(test)
