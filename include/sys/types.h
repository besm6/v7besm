/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * The kernel's scalar typedefs.  These used to be duplicated at the end of
 * sys/param.h; they live here alone now, because param.h has to be #define-only
 * so that the assembly sources can #include it (a typedef is fatal to b6as).
 * Every kernel source includes this header just before sys/param.h.
 *
 * The device-code macros major()/minor()/makedev() stay in sys/param.h.
 */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

typedef struct {
    int r[1];
} *physadr;                   /* physical address */
typedef long daddr_t;         /* disk address */
typedef char *caddr_t;        /* core address */
typedef unsigned short ino_t; /* i-node number */
typedef long time_t;          /* a time */
typedef int label_t[10];      /* program status: r1-r7, r13, r15 */
typedef short dev_t;          /* device code */
typedef long off_t;           /* offset in file */

#endif /* _SYS_TYPES_H */
