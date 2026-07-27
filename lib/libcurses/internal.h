// Private to lib/libcurses: what the library's own files share and no caller may name.
//
// 4.3BSD called this file `curses.ext' and every source included it instead of <curses.h>.
// Renamed to a .h because nothing -- not clang-format, not the editor, not the CMake header
// dependency -- recognises the old extension, and it is a header like any other.
//
// TWO THINGS ARE HERE THAT COULD NOT BE IN <curses.h>.  ttytype must be declared with its
// SIZE, because cr_tty.c writes it through sizeof(ttytype) and the public header can only
// promise an array; and the routines below are the library's seams -- _sprintw and _sscans
// are the varargs cores the eight printw/scanw entry points share, _swflags_ and
// _set_subwin_ are what newwin() and mvwin() have in common, and _id_subwins re-aims a
// parent's subwindows after its rows move.  A program that named any of them would be
// reaching inside.
//
// The DEBUG scaffolding 4.3BSD carried here -- `FILE *outf' and a `# define outf _outf' --
// is gone rather than compiled out, on the precedent of lib/libc/gen/malloc.c.  Its absence
// is load-bearing in one place: the fprintf in clrtoeol.c was the only reader of a `char *'
// bound that had to be rewritten anyway, so deleting the print deleted the hazard.

#ifndef _CURSES_INTERNAL_H
#define _CURSES_INTERNAL_H

#include <curses.h>
#include <stdarg.h>

// The long terminal name, sized here so cr_tty.c's sizeof() has something to measure.
extern char ttytype[50];

// The window wrefresh() is currently painting, or NULL.  cr_put.c's plod() consults it to
// decide whether it may move right by re-printing the characters it passes over.
extern WINDOW *_win;

void gettmode(void);
void _id_subwins(WINDOW *orig);
void _set_subwin_(WINDOW *orig, WINDOW *win);
void _swflags_(WINDOW *win);
int _sprintw(WINDOW *win, char *fmt, va_list ap);
int _sscans(WINDOW *win, char *fmt, va_list ap);

#endif // _CURSES_INTERNAL_H
