# libcurses — 4.3BSD screen handling on the BESM-6

Windows, a screen image, and the cursor motion that reconciles the two. Thirty-nine sources,
**5,311 words** of object code all told, built on [`../libtermcap`](../libtermcap/) and
reading the `/etc/termcap` that [`../../etc/`](../../etc/) stages onto the root filesystem.
It is the library `../../include/curses.h` and `../../include/unctrl.h` had been waiting for
since before there was a termcap to stand on.

The four largest objects are where the work is: `refresh.o` (857 words) is the screen-diff
engine, `cr_put.o` (667) the motion optimiser, `cr_tty.o` (639) the terminal description and
`newwin.o` (454) the allocator. One function group per file, so `b6ranlib`'s index lets a
program pay only for what it calls.

## Building and linking

`make` from the top of the tree builds it; `make install` puts `libcurses.a` into
`share/besm6/lib` beside `libc.a`, `libm.a`, `libtermcap.a` and the external `libruntime.a`.

**The link order is a contract, not a style:**

```
crt0.o  objects…  -lcurses  -ltermcap  -lc  -lruntime
```

`b6ld` scans each archive exactly once, in order, and the calls only ever run one way —
curses calls `tgetent`/`tgetnum`/`tgetflag`/`tgetstr`/`tgoto`/`tputs`, termcap calls
`getenv`/`open`/`read`/`close`/`write` and the string routines, libc calls the `b$*` helpers,
and nothing calls back. Naming `-ltermcap` first, or leaving it out because "curses pulls it
in", gives undefined references from an archive that has already been scanned.

`b6cc` needs no help: `cmd/cc/cc.c` places user `-L`/`-l` flags after the objects and before
its implicit `-lc -lruntime`, so `b6cc prog.c -lcurses -ltermcap -o prog` is already right.
`b6_prog()` in `scripts/BesmCross.cmake` was a different matter — hard-wired to `-lc -lruntime`
with no hook for a third archive — and **the `LIBS` keyword predicted here now exists**, with
`B6_LIBM_DIR`/`B6_LIBCURSES_DIR`/`B6_LIBTERMCAP_DIR` promoted beside `B6_LIBC_DIR`. It emits
its own order rather than the caller's, for the reason above: a caller has no business knowing
that `-lcurses` must precede `-ltermcap`. `lib/test/CMakeLists.txt`'s `b6_libtest()` grew the
same capability first, for `-lm`, and is what it was modelled on.

**`cmd/more` is its first user, and wanted termcap and not curses.** It had arrived with the
*whole* termcap call structure already written — `tgetent`/`tgetnum`/`tgetflag`/`tgetstr`/`tputs`
and all — under RetroBSD's ANSI stubs, so adding the keyword was most of the work and replacing
the stubs was the rest. `/etc/termcap` is therefore read by something at last. `cmd/novi` is
the program still writing hard-coded ANSI, and no longer has a blocker to point at.

## This is 4.3BSD curses, and `include/curses.h` was replaced to say so

The header that stood in `include/` until now was v7's `1.7 (4/17/81)`, and it was
**ABI-incompatible** with the sources that had been sitting in `lib/tmp/libcurses/` — which
are Berkeley's later ones. Five differences, every one of which corrupts memory rather than
merely failing to compile:

| | v7 `curses.h` | these sources |
| --- | --- | --- |
| `struct _win_st` | no `_ch_off`, `_nextp`, `_orig` | all three, and `subwin()` writes them |
| flag bits | `_SUBWIN 01`, everything else shifted up | `_ENDLINE 001` … `_IDLINE 040` |
| `bool` | `char` — one byte | a whole object |
| `_puts(s)` | `tputs(s, 0, _putchar);` — **trailing semicolon** | no semicolon |
| tty modes | `stty()`/`gtty()` | `ioctl(TIOCSETP)` |

The sources won. That stray semicolon alone turns every `if (x) _puts(A); else …` in
`cr_put.c` and `refresh.c` into a syntax error, and the struct difference means a `subwin()`
compiled against the old header writes past the end of the caller's window.

The new header also **completes the prototype set** — 4.3BSD's own declared about two thirds
of the API, and this front end has no implicit declarations, so `overlay`, `winsertln`,
`wdelch`, `winsch`, `idlok`, `mvwin`, `touchoverlap`, `fullname` and the four `scanw` entry
points were each a hard error at their first caller — and drops `UPPERCASE`, which 4.3BSD
declared and never defined.

## `bool` is C11's, and this library is the first `_Bool` in the tree

4.3BSD wrote `#define bool int` and used the name for every window flag and every boolean
capability. `<curses.h>` includes `<stdbool.h>` and declares them `bool` — the real type, not
a macro of our own — so `curses.c`, `cr_tty.c`, `idlok.c`, `getch.c` and `refresh.c` read the
way Berkeley wrote them.

**That was not true when the port landed**, and the route here is worth recording because
nothing else in the tree had ever named `_Bool`. Two compiler defects and a layout objection
stood in the way, and the port shipped with `int` in every one of those places:

- **`b6lower` could not lower it.** `Fatal error: ast_type_to_tac_type: unsupported type kind
  1` for a `_Bool` object of any storage class — file-scope, `static`, array, pointer or
  `typedef`. The front end parsed the type and `get_size()` had a case for it; the lowering
  pass had none. A three-line program with one `bool` global reproduced it.
- **Conversion to `_Bool` did not normalise.** `bool b = 5;` stored 5, against C11 §6.3.1.2.
  Boolean contexts still worked, because `!b` and `if (b)` re-test against zero; what broke
  was `b == 1`, `b + b` and printing — and `cr_put.c` compares `AM` and `_pfast` against `0`
  in three places.
- **And `sizeof(_Bool)` was one char-unit**, not one word like every other scalar, which
  would have made a `bool` member a sub-word object and `bool *` a **fat pointer** —
  and `cr_tty.c`'s table of pointers-to-flags is written through exactly that way.

All three are fixed upstream, in c-compiler commit `2fcd322`. `_Bool` now has **int's
representation**: `bool_size`/`bool_align` of 6 and 6 on this target, so `sizeof(bool)` is one
word ([`../../doc/Besm6_Data_Representation.md`](../../doc/Besm6_Data_Representation.md)) and
`bool *` is an ordinary word pointer. Note that the carrier is `int` and **not** the
`unsigned char` the bug report proposed: the char TAC kinds mean byte-packed storage and fat
byte pointers, which is the very thing that ruled `_Bool` out here. And `emit_bool_normalize()`
now runs on every conversion path — assignment, initialisation, argument passing, return,
explicit cast, `++`/`--` — so a `bool` object never holds anything but 0 or 1.

**Keeping BSD's own `#define bool int` was never an option and still is not.** A different
replacement list for a macro `<stdbool.h>` also defines is a constraint violation (C11
§6.10.3p2) in *either* inclusion order, and `<stdbool.h>` is one of the ten freestanding
headers the external compiler puts on the default include path; a program including both would
take a diagnostic through no fault of its own. Using the type instead of redefining the name
sidesteps the question. `TRUE` and `FALSE` are `true` and `false` now for the same reason —
they are the boolean constants — while `ERR` and `OK` stay `int`, being return codes.

The cost is four words: `cr_tty.o` 636 → 639 and `refresh.o` 856 → 857, which is
`emit_bool_normalize()` — a `b$ne` zero test — on the two `int`-valued stores,
`*(*fp++) = tgetflag(namp)` and `curwin = (win == curscr)`. The store itself is still a plain
`atx`. `getch.o` came back 7 words *smaller*, so the library is 5,311 words rather than 5,314.
`LINES`, `COLS`, `_tty_ch` and `_res_flg` stay `int` and must: `_res_flg` holds the sgtty flag
word, not a flag.

## Eleven `char *` comparisons had to go

When this port was written, a relational operator between two `char *` gave the wrong answer on
this machine: a fat pointer carries its byte offset in bits 47–45 above its word address in
bits 15–1, the offset **decrements** as the pointer advances, and `<` compiled to an integer
comparison of the whole word, so the ordering came out scrambled and inverted within every
word. That is the hazard [`../libtermcap/README.md`](../libtermcap/README.md) met four times,
and curses is a program made almost entirely of buffer cursors.

**The compiler fixed it on 2026-06-17** — the relational lowers through `b$pdiff` now and
tests the sign ([`../../cmd/README.md`](../../cmd/README.md) §2). The eleven rewrites below
stand: they cost nothing and an index test is a register compare where the relational is two
calls.

**Subtraction is fine and is left exactly as v7 wrote it** — `b$pdiff` decodes both operands,
so `p - base` is an exact character count. It was also the conversion tool: where a loop is
entered with a pointer that has already advanced, recover its index once by subtracting.

| Where | v7 wrote | Now |
| --- | --- | --- |
| `refresh.c` `makech` | `for (ce = &_y[wy][_maxx-1]; *ce==' '; ce--) if (ce <= _y[wy]) break;` | index down from `_maxx-1` with an `i > 0` floor |
| `refresh.c` `makech` | `while (*ce==' ') if (ce-- <= csp) break;` | index down from `COLS-1`, floor recovered as `csp - crow` |
| `newwin.c` | `for (sp = _y[i]; sp < _y[i]+nc; ) *sp++ = ' ';` | `memset(_y[i], ' ', nc)` |
| `deleteln.c`, `insertln.c` | `for (end = &temp[_maxx]; temp < end; ) *temp++ = ' ';` | `memset(temp, ' ', _maxx)` |
| `clrtoeol.c` | `for (sp = maxx; sp < end; sp++)` | `memset(&row[x], ' ', _maxx - x)` |
| `clrtobot.c`, `erase.c` | `for (sp = &_y[y][startx]; sp < end; sp++)` | `int` index; `maxx - &_y[y][0]` was already a subtraction and is just `maxx` |
| `delch.c` | `while (temp1 < end) *temp1++ = *temp2++;` | `for (i = _curx; i < _maxx-1; i++) row[i] = row[i+1];` |
| `insch.c` | `while (temp1 > end) *temp1-- = *temp2--;` | `for (i = _maxx-1; i > _curx; i--) row[i] = row[i-1];` |
| `overlay.c` | `for (sp = &_y[y1][…]; sp < end; sp++)` | `int` index, with `x` advancing in the same header |

`refresh.c`'s two are the ones that took care. Both `nsp` and `csp` walked in lockstep with
`wx` — `nsp` is always `&win->_y[wy][wx]` and `csp` always
`&curscr->_y[y][wx + _begx]` — so indexing off a row base says the same thing and leaves
nothing to compare. The one place that is not mechanical is `csp = " "` when refreshing
`curscr` against itself: the old code read a one-character literal over and over, which just
means "the screen is blank here", so a `cur()` macro answers `' '` in that case and there is
no second row at all.

That also settles a comparison that *looks* cross-object and is not. `if (ce-- <= csp)` pits
a pointer into `curscr`'s row against `csp`, which is the literal `" "` when `curwin` — but
that arm is unreachable: `ce` is `NULL` exactly when `curwin`, and the whole block is guarded
by `ce != NULL`. Same allocation, always. It still had to go.

**One comparison that stays** is `refresh.c`'s `clsp - nlsp >= strlen(CE)`. `size_t` is
*signed* here — [`../../include/README.md`](../../include/README.md) explains the deliberate
departure from C11 §7.19 — so the mixed comparison behaves. On a machine with unsigned
`size_t` it would promote and misbehave for a negative difference. Worth a comment so nobody
"fixes" it.

## A row is `malloc(nc)`, not `malloc(nc * 6)`

`newwin.c` allocated a character row as `malloc(nc * sizeof win->_y[0])`, and `_y[0]` is a
`char *` — six char-units. Every row of every window was six times the size it needed.

For a 24×80 `stdscr` and `curscr` pair, with `malloc`'s one-word block header:

| Per window | Corrected | As 4.3BSD had it |
| --- | --- | --- |
| `WINDOW` (16w) + `_y` (24w) + `_firstch`/`_lastch` (24w each), header each | 92 | 92 |
| 24 rows | 24 × (14+1) = 360 | 24 × (80+1) = 1,944 |
| | **452** | **2,036** |

`stdscr` + `curscr`: **904 words** rather than **4,072** — 3,168 words saved, 11% of the
28,672-word user address space, and the difference between a one-page heap and a four-page
one. The other three allocations in that file are correct as written and must not be "fixed"
alongside it: `_y` itself *is* an array of `char *`, and `_firstch`/`_lastch` *are* arrays of
`short`.

## Six upstream bugs carried no further

Three of them are memory corruption, and this machine is what made them show.

**`_id_subwins()` wrote `win->_y[-1]`.** `realy - win->_begy` is the subwindow row holding
the parent's cursor line, and it is **negative** for a subwindow that begins *below* that
line — which the guard above it does not exclude, since that guard only drops subwindows
lying entirely *above*. v7 then wrote one word in front of the malloc'd row-pointer array,
which is the block header: after a `wdeleteln()` on a parent whose cursor is above its
subwindow, the next `free()` walks a corrupt arena and does not come back. The starting row
is clamped now, with the parent-side index moved down by as much as the subwindow-side one
moves up. This is what `../test/cursest.c` caught first, and it presented as a hang whose
existence depended on the object layout — adding three `printf`s to the test made it appear
and adding three more made it go away.

**`makenew()` never initialised `_orig`.** It set `_cury`…`_flags`, `_scroll` and `_leave`;
`newwin()` then set `_nextp` and `_ch_off`; nothing set `_orig`, and `malloc` does not zero.
`_orig` is the flag that decides whether a window owns its rows — `delwin()`, `wdeleteln()`
and `winsertln()` all branch on it — so a top-level window that came back with garbage there
took the *subwindow* path, copying characters where it should rotate row pointers and
freeing none of its rows. Latent on most runs only because a fresh `sbrk` page happens to be
zero. `makenew()` sets all three now.

**`setterm()` wrote through its own argument.** It ended
`strncpy(ttytype, longname(genbuf, type), …)`, and `longname()` fills its *second* argument —
so that wrote the terminal's long name into `type`, the caller's string. On the `My_term`
path `type` is `Def_term`, a **string literal**: on this machine a word of the const pool,
and under a pure link (`-n`, NMAGIC) shared text that other processes are reading. It goes
through a buffer of its own now, and `longname()`/`fullname()` are bounded at 49 characters —
their only in-tree caller's buffer is `ttytype[50]`, so that is the API's real contract, and
there is no way to pass a size through v7's interface.

**`overlay()` was not non-destructive.** This copy carried `overwrite.c`'s unconditional
row-at-a-time `bcopy` *and* the selective loop. The first copies the blanks too, so it made
`overlay()` destructive and left the second loop nothing to do: the two functions were the
same function, and the one that documents itself as non-destructive was not. The `bcopy` is
gone, because keeping it means `<curses.h>` has two spellings of `overwrite()` and no
`overlay()` at all.

**`isspace()` on a window cell ran off the end of `_ctype_[]`.** A cell carries `_STANDOUT`
in bit `0200`, `char` is unsigned here, and `_ctype_` is 129 entries indexed as
`(_ctype_ + 1)[c]` — [`../libc/gen/ctype_.c`](../libc/gen/ctype_.c) says so itself. Every
other blank test in the library compares against `' '`, and so does this one now, over the
character with the standout bit masked off.

**`wgetch()` could never return `ERR`.** 4.3BSD wrote `char inp = getchar();` and never
tested for `EOF`; `char` is unsigned here, so `EOF` became `0377` — an ordinary character —
and `getstr.c`'s loop, which reads until `ERR` or a newline, never stopped on a stream that
simply ended. It limps on a signed-`char` machine too (`-1` compares unequal to `ERR`, which
is `0`); unsigned `char` only takes away the last accident that hid it.

Three smaller ones, for the record: `win2->_maxy + win2->_begx` where `_begy` is meant, in
**three** files (`overlay.c`, `overwrite.c` and `toucholap.c` — invisible whenever
`_begx == _begy`, which is true of every window `initscr()` makes); `_unctrl[036]` spelled
`"^~"` where `"^^"` is meant; and the `#ifndef CURSES_H` guard that nothing ever `#define`d.

## What is gone, and why

- **`tstp.c`** — job control does not exist here. No `SIGTSTP`, `SIGWINCH`, `sig_t`,
  `sigset_t`, `sigemptyset`, `sigaddset` or `sigprocmask`; `../../include/signal.h` stops at
  fifteen signals and `NSIG 17`. The file is deleted and so is the `#ifdef SIGTSTP` block in
  `initscr.c`, rather than left as a conditional that can never be true.
- **The `TIOCGWINSZ` blocks in `cr_tty.c`** — no `struct winsize`, no `TIOCGWINSZ` in
  `<sgtty.h>`. `LINES` and `COLS` come from `li#`/`co#` or the 24×80 default, and nothing
  asks the driver how big the console is. Same rule `../libtermcap` dropped its termios shim
  under.
- **`getdtablesize()`** — no such call. `initscr()` searches the three standard descriptors,
  which is what BSD's loop finds in practice, and falls back to `curses.c`'s default of 1
  when none of them is a tty. v7 left `_tty_ch` at `nfd`, so every later `ioctl` failed on a
  descriptor that was never open.
- **`bcopy`** — `memmove`, argument order reversed. `memmove` and not `memcpy` because two
  windows can be subwindows of one parent and share their rows.
- **The stack-allocated `FILE`s.** `_sprintw` set `_flag` to `_IOWRT + _IOSTRG`, pointed
  `_ptr` at a buffer, put `32767` in `_cnt` and called `_doprnt` — leaving `_base` and
  `_file` uninitialised and relying on `_cnt` never reaching zero, because `<stdio.h>`'s
  `putc` falls into `_flsbuf()` when it does and `_flsbuf()` reads exactly those two fields.
  `_sscans` did the same with `_IOREAD|_IOSTRG` and `_doscan`. This tree has `vsnprintf` and
  `vsscanf` and declares neither `_doprnt` nor `_doscan`, so both are one call now. The four
  `scanw` entry points also lose v7's `(char *fmt, int args)` + `&args` hack, which is not
  merely ugly here: `<stdarg.h>` is the external compiler's, and an `int *` is not a
  `va_list` and never becomes one. `wprintw()` passed `&args` where its three siblings passed
  `args` — a real bug, and one only comparing the four could show.
- **Every `#ifdef DEBUG` block**, and `curses.c`'s `FILE *outf` with it — gone rather than
  carried compiled out, on the precedent [`../libc/gen/malloc.c`](../libc/gen/malloc.c) set.
  It does work for the port besides: those dead `fprintf`s were the only reader of a `char *`
  bound in `clrtoeol.c`, so deleting the print deleted one of the eleven hazards, and a dozen
  files lose an `<stdio.h>` dependency they had no other use for.

## `PC` and `ospeed` are written and never read

`curses.c` defines `char PC` and `cr_tty.c` `short ospeed`, and nothing in the system reads
either. That is deliberate on both sides: `../libtermcap/tputs.c` emits no padding at all —
nothing on this machine can be overrun — so
[`../../include/term.h`](../../include/term.h) declares neither name and leaves both to
curses, exactly as 4.xBSD had them. Two words, and they keep this library linkable against a
future termcap that does pad. `setterm()` writes `PC` from the `pc` capability, which is also
why **`sstrs[]` must keep its 63rd slot** even though the value is dead: the table and the
`namp` string run in lockstep, and compacting one without the other silently shifts sixty
capabilities.

`normtty` is 4.xBSD's and has no writer here either — `tset(1)` was what set it, and there is
no `tset` in this tree.

`_tspace`, the arena the decoded capability strings land in, is **1,024 characters where
4.xBSD allowed 2,048**, and that is provable rather than hopeful: `tgetent()` bounds a whole
entry at `TBUFSIZ` (1,024) characters, and `tgetstr()` only ever *decodes* — `\E` to one
byte, `^X` to one byte, `\101` to one byte — so the decoded strings of one entry cannot
together outrun the text they came from. 171 words rather than 342.

## Testing: one program run twice, and one that cannot be

[`../test/cursest.c`](../test/cursest.c) runs under `b6sim` (ctest label `lib`) **and** off
the disk image under the booted kernel (label `kernel`, `libtest_cursest`), and both are
diffed against the same `cursest.expected`. Under `b6sim` every system call is the host's, so
a kernel bug cannot show; the two harnesses disagreeing means one of them is wrong.

Three things make that possible:

1. **`My_term = TRUE`.** `initscr()`'s other path calls `gettmode()`, which derives `GT` from
   `XTABS` and `NONL`/`_pfast` from `CRMOD` — and `ioctl` is an unconditional no-op under
   `b6sim` (`cmd/sim/syscall.cpp`, and `gtty` zero-fills) while the booted kernel's console
   really is `ECHO|CRMOD|XTABS` (`kernel/dev/sc.c`). All three flags change which cursor
   motion `cr_put.c` emits, so the same program would produce two different streams. With
   `My_term` set, `initscr()` asks the tty nothing and all three keep their BSS zeros.
2. **The test owns its `environ`** and takes the database as `argv[1]`, the trick and the
   reason `termcapt` uses: the source tree's `etc/termcap` under `b6sim` (through
   `cursest.args` and `run-test.sh`'s `@srcdir@`), and `/etc/termcap` — *the same file* —
   on the image.
3. **The test defines its own `_putchar()` and `wgetch()`.** `b6ld` pulls an archive member
   only for a symbol still undefined, so `putchar.o` and `getch.o` are never pulled and the
   library calls the test's instead. `_putchar` renders every byte printably, so the whole
   cursor-motion stream lands in the expectation as plain ASCII; nothing in curses counts
   what it wrote, so the rendering cannot perturb the algorithm. `wgetch` feeds canned input,
   which is the only way to reach the `scanw` family at all.

It runs the whole scenario against **three terminals**, because one cannot cover the motion
optimiser: `vt100` has `cm` and `ce`, so it is the only one that reaches both `refresh.c`
comparisons and shows `\E[K` emitted where sixty spaces would otherwise go; `cons25` has a
different `cm`, 25 lines, and real `al`/`dl`; and `notaterminal` fails `tgetent` altogether,
which is the only way into `plod()`'s no-cursor-addressing branch and `fgoto()`'s
scroll-by-linefeed loop.

[`../test/curstty.c`](../test/curstty.c) is the half that follows from decision 1: it does
*not* set `My_term`, so it is **IMAGEONLY** — the same precedent `memt` and `shellt` set. It
asserts the console this kernel opens: `_res_flg` must be `06030` (`ECHO|CRMOD|XTABS`),
`gettmode()` must clear `XTABS` in the live flags, `GT` and `NONL` must both come out false,
and `erasechar`/`killchar` must be `0177`/`025` — `^?` and `^U`, what `ttychars()` installs
([`include/sys/tty.h`](../../include/sys/tty.h)). It runs **last** in
the deleted `kernel/test/libtest.sh` on purpose: `gettmode()` clears `XTABS` on the real console and only
`endwin()`'s `resetty()` puts it back, so if it ever dies the only thing still running
against a changed console is one `echo` and the `sync`.

**Deliberately uncovered**, and said so at the point where the case would have gone: the real
`wgetch()`'s `getchar()`, since `stdin` is ctest's under `b6sim` and the console under the
kernel; and `initscr()`'s `isatty()` search, for the same reason.

## The manual page

`curses.3.umm` is 4.3BSD's, corrected in place with `Note:` markers where it describes something
this port does not do. **Nothing installs it** — no `CMakeLists.txt` in this tree has a man
rule yet, which is also true of `../libtermcap/termcap.3.umm` — but it is staged onto the image
by the top-level `B6_STAGE_MAN` glob, where manview(1)
([`../../cmd/README.md`](../../cmd/README.md) C25a) formats it.
