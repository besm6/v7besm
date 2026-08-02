// The terminal: raw mode through <sgtty.h>, output through one buffer, input
// through an ANSI escape decoder.  See terminal.c.
//
// The synthetic key codes start at 256 so that `key < 256' separates a byte the
// user typed -- ANY byte, this line is eight bits wide -- from a key that arrived
// as an escape sequence.
//
// THIS FILE IS NAMED IN cmd/novi/CMakeLists.txt's KHDRS; see buffer.h for why.

#ifndef TERMINAL_H
#define TERMINAL_H

#define KEY_UP     256
#define KEY_DOWN   257
#define KEY_LEFT   258
#define KEY_RIGHT  259
#define KEY_HOME   260
#define KEY_END    261
#define KEY_PGUP   262
#define KEY_PGDN   263
#define KEY_DELETE 264
#define KEY_INSERT 265

int term_open(void);
void term_close(void);
void term_size(int *rows, int *cols);
int term_key(void);
void term_put(char *s);
void term_write(char *s, int n);
void term_flush(void);
void term_move(int row, int col);
void term_clear(void);

#endif
