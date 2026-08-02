// The disk-backed gap buffer: the document is never in memory.
//
// Two unlinked scratch files hold it.  `left' has the text before the cursor in
// forward order; `right' has the text after it REVERSED, so that both halves grow
// and shrink at their own end and the cursor position is simply nleft.  Moving the
// cursor moves bytes between the two files; nothing else about the document is kept.
//
// A logical position maps to a physical one this way, and buf_get() is the only
// place that needs it:
//
//	pos <  nleft	left[pos]
//	pos >= nleft	right[nright - 1 - (pos - nleft)]
//
// THIS FILE IS NAMED IN cmd/novi/CMakeLists.txt's KHDRS.  b6cc has no -M and
// b6_obj's dependency is the SYSTEM header tree, so without that line editing this
// file would rebuild nothing.

#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

// The read cache inside struct buffer.  256 is right for the backward line scans
// line_start() does; it is NOT the gap transfer size -- see BUF_MOVE in buffer.c.
#define BUF_CACHE 256

struct buffer {
    int left;
    int right;
    off_t nleft;
    off_t nright;
    int changed;
    // Bumped by every mutator and by nothing else, so that novi.c's refresh() can
    // tell a cursor move (repaint nothing) from an edit (repaint the body).
    long version;
    int cachefd;
    off_t cachebase;
    int cachelen;
    unsigned char cache[BUF_CACHE];
};

int buf_open(struct buffer *b, char *name);
void buf_close(struct buffer *b);
off_t buf_size(struct buffer *b);
off_t buf_pos(struct buffer *b);
int buf_get(struct buffer *b, off_t pos);
int buf_insert(struct buffer *b, int ch);
int buf_insert_block(struct buffer *b, char *s, int n);
int buf_backspace(struct buffer *b);
int buf_delete(struct buffer *b);
int buf_left(struct buffer *b);
int buf_right(struct buffer *b);
int buf_seek(struct buffer *b, off_t pos);
int buf_save(struct buffer *b, char *name);

#endif
