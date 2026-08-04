#ifndef BESM6_LIBAOUT_FASTIO_H
#define BESM6_LIBAOUT_FASTIO_H

//
// The BESM-6-only word path for fgetw()/fputw(), and the guard that says when it
// is legal.  On the host this header defines nothing and the callers compile to
// what they always were.
//
// A word on disk is six big-endian bytes; six chars pack big-endian into a BESM-6
// word (doc/Besm6_Data_Representation.md).  They are the same bit pattern, so a
// stdio cursor that happens to sit on a word boundary is already pointing at the
// word the object file wants: ONE load, in place of six fat-pointer byte
// extractions and the six getc expansions around them.
//
// ALIGNMENT IS TESTABLE IN C.  Casting a char* to a word pointer discards the
// byte offset and casting back rebuilds it as byte #0, so the round trip is the
// identity exactly for a pointer that was already at byte #0.
// lib/libc/gen/qsort.c makes the same test for the same reason, and it is
// preferred to picking the fat pointer's offset field apart by hand:
// <sys/param.h>'s ptrbyte() shifts by 44, not 45 -- bits number right-to-left
// from 1 -- and that header cannot be included here anyway, since these sources
// are compiled for the host too.
//
// THE GUARD IS ABOUT MEMORY ALIGNMENT, NOT FILE OFFSETS.  The fast path returns
// the next six bytes of the stream in stream order, which is exactly what six
// getc calls return; where the buffer happens to start in the file does not
// enter into it.  What must hold is that the six bytes are contiguous AND
// word-aligned in memory.
//
// _cnt is read before _ptr is touched.  Every stream that must not take this path
// is held at _cnt == 0 by design -- line-buffered, unbuffered and memory streams
// (see the note in <stdio.h>) -- and so is one whose buffer has not been chosen
// yet, whose _ptr is not a pointer at all.
//
#if besm6
#define WORD_IN_BUF(f) \
    ((f)->_cnt >= (int)sizeof(uword_t) && (char *)(uword_t *)(f)->_ptr == (f)->_ptr)
#endif

#endif // BESM6_LIBAOUT_FASTIO_H
