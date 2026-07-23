//
// vsprintf -- format into an unbounded buffer (C11 §7.21.6.11).
//
// "Unbounded" is a nominal size larger than any buffer can be: a program sees 15
// bits of address, so the whole user space is 32768 words == 196608 bytes and no
// object in it can be longer than that (doc/Memory_Mapping.md).  v7 used 32767
// here, which was the PDP-11's answer to the same question.
//
#include <stdio.h>

#define UNBOUNDED 196608 // 32768 words x 6 bytes: the entire address space

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, UNBOUNDED, fmt, ap);
}
