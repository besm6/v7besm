//
// drainbrz -- drain the БРЗ write cache.
//
// void drainbrz(void);
//
// БРЗ is eight write-back lines.  A store to physical 1-7 with mapping off (which
// is the mode the kernel always runs in) stores nothing: it advances the flush
// counter and evicts the oldest line.  The first store only *arms* the counter --
// eviction begins with the second -- so nine are needed to drain all eight lines.
//
// The value stored is irrelevant; the accumulator is left alone.
//
// A dirty line carries a *virtual* tag and is written back through whatever mapping
// is loaded when it is finally evicted.  So a kernel that reloads РП with dirty
// lines outstanding writes the old process's stores into the NEW process's memory.
// sureg() therefore drains before it writes РП, and so must every context switch.
// The hazard is invisible under default SIMH and fatal under `set mmu cache'.
// See doc/Memory_Mapping.md, "БРЗ -- the write cache".
//
// This has to be assembly, and it is the one routine in the kernel that does.  The
// nine stores must be CONSECUTIVE -- any ordinary store between them resets the
// counter to zero -- and C cannot promise that: b6cc materializes the destination
// pointer through a frame slot, so a C version emits two ordinary stores of its own
// before each switch-register write and the counter never advances past one.
//
// It lives in its own file rather than in besm6.S so that kernel/test/mmutest can
// link the real routine: besm6.o cannot go into a standalone test (its 0500 vector
// reaches into the C kernel, and _start seeds no stack).

        .text
        .globl  drainbrz
drainbrz:
        atx     1                // nine consecutive stores to physical 1-7 --
        atx     2                //   nothing may come between them
        atx     3
        atx     4
        atx     5
        atx     6
        atx     7
        atx     1
        atx     2
     13 uj
