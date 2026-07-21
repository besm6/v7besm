/*
 * <string.h> — string handling (C11 §7.24), BESM-6 target.
 *
 * All functions below are implemented in libc.bin (the str* / mem* family plus
 * strerror).  On BESM-6 char* / void* are FAT pointers: a byte cursor carries
 * its in-word offset, so byte-by-byte traversal works across word boundaries
 * (see doc/Besm6_Data_Representation.md and the b/p* runtime helpers).
 */
#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen(const char *s);

char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
char  *strcat(char *dest, const char *src);
char  *strncat(char *dest, const char *src, size_t n);

int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);

char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
char  *strtok(char *str, const char *delim);

void  *memcpy(void *dest, const void *src, size_t n);
void  *memmove(void *dest, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
void  *memchr(const void *s, int c, size_t n);

char  *strerror(int errnum);

#endif /* _STRING_H */
