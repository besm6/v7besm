# libcurses for programs running under this kernel.  See README.md and ../README.md.
#
# The 4.3BSD screen library -- windows, a screen image, and the cursor motion that reconciles
# them -- built on ../libtermcap and reading the /etc/termcap that etc/ stages onto the root
# filesystem.  One product, libcurses.a.
#
# LINK ORDER IS A CONTRACT: -lcurses AHEAD of -ltermcap ahead of -lc ahead of -lruntime.
# b6ld scans each archive exactly once, in order, and the calls only ever go one way --
# curses calls tgetent/tgetnum/tgetflag/tgetstr/tgoto/tputs, termcap calls getenv/open/read/
# close/write and the string routines, libc calls the b$* helpers, and nothing calls back.
# Same one-scan rule libm and libtermcap obey (../README.md).
#
# tstp.c of the upstream directory is not here: it handles SIGTSTP, and this system has no
# job control -- no SIGTSTP, SIGWINCH, sigset_t or sigprocmask anywhere in ../../include.
# Nor is test.c, which becomes ../test/cursest.c.
set(LIBCURSES_OBJ
    addch addstr box clear clrtobot clrtoeol cr_put cr_tty curses delch deleteln delwin
    endwin erase fullname getch getstr id_subwins idlok initscr insch insertln longname
    move mvprintw mvscanw mvwin newwin overlay overwrite printw putchar refresh scanw
    scroll standout toucholap touchwin unctrl)

# internal.h is this directory's, not ../../include's, so the KHDRS glob b6_obj() takes its
# coarse header dependency from does not cover it.  KHDRS is read from the caller's scope
# (../../scripts/BesmCross.cmake), so appending here is the intended seam.
list(APPEND KHDRS ${CMAKE_CURRENT_SOURCE_DIR}/internal.h)

set(OBJS "")
set(OBJ_NAMES "")
foreach(_n ${LIBCURSES_OBJ})
    b6_obj(_o ${CMAKE_CURRENT_SOURCE_DIR}/${_n}.c)
    list(APPEND OBJS ${_o})
    list(APPEND OBJ_NAMES ${_n}.o)
endforeach()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libcurses.a
    COMMAND ${B6AR} cr libcurses.a ${OBJ_NAMES}
    COMMAND ${B6RANLIB} libcurses.a
    DEPENDS ${OBJS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "b6ar libcurses.a" VERBATIM)

add_custom_target(libcurses ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libcurses.a)

# Install beside libc.a, libm.a, libtermcap.a and libruntime.a; install() preserves the
# timestamp for the same __.SYMDEF reason libc/CMakeLists.txt gives.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libcurses.a DESTINATION share/besm6/lib)
