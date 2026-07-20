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

/*
 * Every one of these is exactly one 48-bit word, because on this machine
 * `short', `int', `long' and `long long' are all the same type: a 41-bit
 * signed value (1 sign bit + 40 value bits) in one word.  The PDP-11 spelled
 * them apart to pick a width; here the width is fixed and the only thing the
 * spelling still chooses is SIGNEDNESS -- and that choice is expensive.
 *
 * Signed add, subtract and compare are single inline instructions.  Unsigned
 * `+', `-', `*', `/', `<', `<=', `>', `>=' are CALLS -- b$uadd, b$usub,
 * b$umul, b$udiv, b$ult, ... -- because the additive unit reads bits 48-42 as
 * an exponent and a full 48-bit value carries data there.  See
 * doc/Besm6_Runtime_Library.md, "Unsigned Integer Arithmetic".
 *
 * So: `int' unless the value is genuinely a 48-bit hardware bit pattern (an
 * MMU page-register word, a GRP/PRP mask).  None of these are.
 */
typedef int daddr_t; /* disk address; bmap() returns -1, so SIGNED */
typedef int ino_t;   /* i-node number */
typedef int time_t;  /* a time; 41 bits of seconds, so no 2038 problem */
typedef int dev_t;   /* device code; NODEV is -1, so SIGNED */
typedef int off_t;   /* byte offset in file */

typedef char *caddr_t;   /* core address -- a FAT pointer; see sys/param.h */
typedef int label_t[10]; /* program status: r1-r7, r13, r15 */

/*
 * A sleep/wakeup channel.  `struct chan' is deliberately never defined: the
 * value is a token, compared only for equality and against zero, and an
 * incomplete type makes dereferencing it a compile error rather than a
 * mystery.
 *
 * It is a plain struct pointer, not a `caddr_t' and not a `void *', because on
 * this machine those two are FAT pointers -- so `sleep((caddr_t)ip, ...)' used
 * to emit a real three-instruction conversion (xta/aox/atx through a frame
 * slot) at each of ~56 call sites, to build a byte-offset field that wakeup()
 * then only ever compared.  A struct pointer is THIN, so the cast is free.
 *
 * Channels are distinguished by VALUE, so a channel built by offsetting an
 * object must offset it by whole WORDS -- see CHANOF() in sys/param.h.
 */
struct chan;
typedef struct chan *chan_t;

/*
 * A callout argument -- the token timeout() carries through to its callback.
 * Thin, for the same reason chan_t is, and a distinct incomplete type so that
 * a wakeup channel and a callout argument cannot be swapped by accident.
 */
struct carg;
typedef struct carg *carg_t;

/*
 * A PHYSICAL word address.  Not a pointer: a caddr_t's word field is only 15
 * bits, so it cannot name memory above 32767, and this machine has 512 Kwords
 * -- 19 bits, which is why struct buf had to grow b_paddr in the first place.
 * Signed, because 19 bits fit the 41-bit signed range with room to spare and
 * signed arithmetic is inline.
 */
typedef int paddr_t;

#endif /* _SYS_TYPES_H */
