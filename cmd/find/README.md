# `find`, a union by reinterpretation, and a frame that was not the size it looked

Task C5f, the largest of its seven at 725 lines, and the second program in this directory —
after `mount`/`umount` — that **`b6sim` can say nothing at all about**. §1's C11 pass is
mechanical and is not repeated here.

## The parse tree was a union built out of pointer casts

```c
struct anode { int (*F)(); struct anode *L, *R; } Node[100];

mk(glob,  (struct anode *)b,       (struct anode *)0)     /* b is a char * */
mk(mtime, (struct anode *)atoi(b), (struct anode *)s)     /* an int and a char */

glob(p)  register struct { int f; char *pat; } *p;      { return gmatch(Fname, p->pat); }
mtime(p) register struct { int f, t, s; }     *p;       { return scomp(..., p->t, p->s); }
```

Eighteen primaries, each reading the same three words back through a private struct shape of
its own. On a PDP-11 that is merely unpleasant. Here **`-name` would have matched the wrong
bytes**: a `char *` is a *fat* pointer — bit 48 set, a byte offset in bits 47–45 — and casting
one to a `struct anode *` **floors it to the word**
([../README.md](../README.md) §2's third hazard, and the one the compiler's 2026-06-17 fix
explicitly does *not* cover). The pattern would come back pointing at the start of whatever
word it happened to begin in.

The node carries a `pat`, a `num` and a `sign` of the right types now, every primary takes
`struct anode *`, and `mknum()`/`mkpat()` are the two constructors beside `mk()`. It costs
three words a node — 600 words for the whole table, against 300 — which is nothing.

**This is §2's third hazard finding a victim at last.** [../README.md](../README.md) §2 has
listed it since task C11 and named `sort`, `find` and `make` as the places to expect it;
`sort` turned out to have none of the three (C5d) and `dd` none either (C4b). This is the
first program in the tree where the flooring cast was actually load-bearing.

## The measurement finding: read the prologue, do not estimate it

`descend()` recurses once per directory and v7 held its directory block **in that frame**:

```c
struct direct dentry[32];      /* 32 * 24 bytes = 128 WORDS, per level */
```

A PDP-11 entry was 16 bytes; here `struct direct` is four words / 24 bytes, so the array
nearly doubled without a line changing. Nothing anywhere checked the depth, so about thirty
levels filled the four-page stack §6 names — and C5c's `grep` finding says what the region
past it does: **wrong answers for a dozen levels before it faults.**

The first draft of this port shrank the array to 16 entries, estimated the frame at "about
eighty words a level" from the local variables, and set `MAXDEPTH 40`. Then `b6disasm`:

```
2540:  15 utm 0277        /* descend()'s prologue: 191 words */
```

**The estimate was out by more than a factor of two, in the unsafe direction** — 40 levels
would have been 7,640 words of a 4,096-word stack. The block is on the heap now, one
`malloc` per level freed on the way out, which is the largest single thing in the frame and
what buys the depth back:

```
2536:  15 utm 0223        /* 147 words */
```

and `MAXDEPTH` is 20 by arithmetic rather than by feel: 20 × 147 = 2,940, plus 127 for the
deepest thing the program can call while stopping (`fputs` 19 + `_flsbuf` 108), against 4,096.

**C5e's warning about the diagnostic's own frame does not bite as hard here, and the reason
is worth naming: `find` links no `_doprnt` at all.** `pr()` is `fputs` and `-print` is
`puts`; there is not one numeric conversion in the program. `sed` had to leave 281 words for
its message; this one needs 127. That is §6's "what a program prints with dominates what it
does", seen from the *stack* rather than from the image.

## Three PDP-11 layout constants that had to change together

```c
for (offset = 0; offset < dirsize; offset += 512)          /* a PDP-11 disk block */
    for (dp = dentry, entries = dsize >> 4; entries; ...)  /* a 16-byte entry     */
        for (i = 0; i < 14; ++i)                           /* DIRSIZ              */
```

`struct direct` is **24 bytes** here and `DIRSIZ` is **18**, and 24 is not a power of two, so
the shift becomes a divide. All three are one fact stated three times, so the port states it
once — `DIRBLK` is `NENTRY * sizeof(struct direct)` — and `_Static_assert`s the rest against
`<sys/dir.h>`. §4's rule is the search that finds this class of bug: **grep for a `read` whose
length is arithmetic rather than a `sizeof`.**

A name out of a directory is not NUL-terminated (§5), which v7 knew; what it did not do is
bound the concatenation into `Pathname[200]`, and 18-byte components reach that bound sooner
than 14-byte ones did.

## `-cpio` is deleted

It wrote a PDP-11 `cpio` archive out of 16-bit `short`s — through a **run-time byte-order
probe**, `union { long l; short s[2]; char c[4]; }` with `if (U.c[0] /* VAX */)` — and
`chgreel()` prompted on `/dev/tty` for the next **magnetic tape reel**.
[../TODO.md](../TODO.md)'s exclusion table drops all of v7's tape software, this kernel has no
tape driver and no `bdevsw` row for one, and an archive nothing on the machine can read is not
a service. `getty`'s speed table and `col`'s half-shift are the precedent, and the question
they all answer is the same one: *what still feeds this mechanism?*

It took about 140 lines with it, and one of them is worth recording: **`find` now calls `sbrk`
nowhere.** `Buf = (short *)sbrk(512)` was in the `-cpio` branch and was the file's only use of
it — so [../README.md](../README.md) §2's "`find` and `make` are the two left" for the three
arena hazards becomes just `make`. Neither of the two programs the table expected to have them
did.

## `-size` is in 1024-byte blocks

v7's were 512. §4's rule — *a constant is the user's business only while it still names
something on this machine* — is `dd`'s from C4b, and 512 names a PDP-11 disk block and nothing
here. `df`, `du`, `quot` and `ls -s` were all taught `KBYTE` in C4a, so a user types the number
they read out of one of those. The division is at the comparison and nowhere else, so
`st_size` stays in bytes right up to that line, and `_Static_assert(BSIZE % KBYTE == 0)` sits
beside it. `find.1.umm` gets the `BLOCKS ARE 1024 BYTES` section the other four carry.

## What else was fixed rather than carried

* **`execvp(nargv[0], nargv, np)` takes two arguments here.** A hard error, and a good one to
  meet early.
* **Eight file-scope names had to move**: `exp` is libm's, `ctime` is `<time.h>`'s, and
  `index`, `size`, `type`, `print`, `and`, `or`, `not` and `pr` were all global. §1's
  rename-on-sight; `tsort`'s `index` in the same task is the dangerous form of the same thing.
* **`getunum()` parsed `/etc/passwd` by hand** into `char str[20]` with `while ((*sp = getc(pin))
  != ':') sp++;` — no bound at all. `getpwnam(3)` and `getgrnam(3)` are in this libc and are
  what it uses now.
* **`Home[strlen(Home) - 1] = '\0'`** on a `popen` that returned nothing writes `Home[-1]`.
* **`doex()`'s `nargv[50]`** was filled from `argv` with no bound.
* `-type f` was the literal `0100000`; it is `S_IFREG`.

## Left alone, deliberately

`amatch()`'s character-range test is `k |= lc <= scc & scc <= (cc = p[1])` — a bitwise `&`
where `&&` was meant. It is correct, both operands being 0 or 1, and it is left as it stands
with a comment saying so, because the next reader will stop at it exactly as this one did.

`popen("pwd", "r")` stays: this system has no `getcwd(3)`, v7 had none either, and `/bin/sh`
and `/bin/pwd` are both on the image. It is a fork and an exec at every startup and there is
no cheaper way to name the working directory.

## No `b6sim` half at all, and saying so is the point

`cmd/find/` has **no `test/` directory**. Three separate things put it out of reach of that
harness, and any one of them would be enough:

* it **reads a directory descriptor**, which `b6sim` refuses — the same reason `ls` and `du`
  have no cases (§9);
* it **`popen`s `pwd`**, which under `b6sim` forks the *build machine's* shell;
* **`-exec` forks and execs**, which under `b6sim` would run the build machine's programs on
  the build machine's files.

So every assertion about this program is a section of
[../../kernel/test/filters.sh](../../kernel/test/filters.sh), which builds a tree under the
image's own `/tmp` and walks it. `mount`'s C4f rule applies word for word: **a whole harness
can have nothing to say about a program, and that has to be said out loud** — `ctest -L cmd`
is silent about `find`, and the booted test is not the second opinion but the only one.

## Sizes

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `find` | 77 | 4,165 | 402 | 2,023 | **6,667** |

Out of the 28,672 words §6 allows. `bss` is 2,023, of which the parse tree is 600 and stdio's
buffer 1,024; `data` is 402 mostly because `Home` and `Pathname` are there. What the number
does not include is one 768-byte directory block per level of recursion, which is now on the
heap — §6's fourth ceiling, and the reason the depth is counted in the program.
