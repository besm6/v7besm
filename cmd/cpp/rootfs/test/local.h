/*
 * The local include agree.c pulls in with a QUOTED name, so it is found relative to the
 * including file rather than through /usr/include -- which under b6sim would be the build
 * machine's.  It also carries the include-guard shape, so the #ifndef/#define/#endif path
 * is walked as well.
 */
#ifndef LOCAL_H
#define LOCAL_H

#define LOCAL_ANSWER 17

void report(const char *fmt, ...);

#endif /* LOCAL_H */
