/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// The shell's expression stack: the scratch area where words, arguments and parse-tree
// nodes are built before they are committed.
//
// It lives immediately above the heap arena (blok.c), and addblok() moves it whenever
// the arena grows underneath it.  That is what relstak()/absstak() are for: an OFFSET
// survives a move where an address would not.
//
// To use the stack as temporary workspace across a possible storage allocation (a name
// lookup, say):
//   a) get an offset from `relstak'
//   b) push with `pushstak'
//   c) reset with `setstak'
//   d) `absstak' turns the offset back into an address when one is needed
//
#ifndef SH_STAK_H
#define SH_STAK_H

//
// relstak() IS AN INTEGER, and its callers must hold it in one.  v7 stored it in a
// STRING (macro.c, name.c) because on a byte-addressed machine an offset and a pointer
// are the same 16 bits.  Here a char * is a FAT pointer -- a bit-48 marker, a byte
// offset, a word address -- and an integer cast into one is not a pointer at all; it is
// the exact thing lib/README.md's ground rules forbid fabricating.  So absstak() takes
// an INT, the callers declare an INT, and v7's Rcheat() is not needed by either.
//
#define relstak()   (staktop - stakbot)
#define absstak(x)  (stakbot + (x))
#define setstak(x)  (staktop = absstak(x))
#define pushstak(c) (*staktop++ = (c))
#define zerostak()  (*staktop = 0)

//
// Make sure the char at `p' is inside the break, growing it if it is not.
//
// EVERY UNBOUNDED PUSH NEEDS THIS, and v7 had nothing like it.  locstak() guarantees one
// increment of headroom when an item is STARTED, and the loops that then fill the item
// -- parameter substitution, word(), split(), the here-document copier -- push as many
// characters as the user's data has, through a cursor of their own, with no further
// check.  v7 got away with that by arrangement: running off the end of the break raises
// a memory fault, and fault() (fault.c) extends the arena and lets the faulting store
// re-execute.
//
// The arrangement is kept, but it cannot be the only one.  It needs the kernel to
// restart the faulting instruction, which is a property of this port's trap path that
// nothing has demonstrated yet -- the shell runs under the real kernel now, but no test in
// kernel/test/ drives it into a growth fault; and under b6sim there is no
// signal delivery at all -- its signal() implements only SIG_DFL and SIG_IGN -- so a
// value larger than the current slack walks straight out of the address space instead.
// The test costs a compare on the common path.
//
#define chkstak(p)         \
    do {                   \
        if ((p) >= brkend) \
            growstak();    \
    } while (0)

void growstak(void);

// Used to address an item left on the top of the stack (very temporary)
#define curstak() (staktop)

// `usestak' before `pushstak' then `fixstak'.  These are safe against the heap being
// allocated.
#define usestak()  \
    {              \
        locstak(); \
    }

// Will allocate the item being used and return its address (safe now).
#define fixstak() endstak(staktop)

// A chain of ptrs of stack blocks that have become covered by heap allocation.
// `tdystak' will return them to the heap.
extern BLKPTR stakbsy;

// Base of the entire stack
extern STKPTR stakbas;

// Top of entire stack
extern STKPTR brkend;

// Base of current item
extern STKPTR stakbot;

// Top of current item
extern STKPTR staktop;

#endif // SH_STAK_H
