# `ed`, and the eighth bit it could not spare

Task C3, the pivot of [../README.md](../README.md): before `/bin/ed` was on the image, every byte
of every file on it had been written on the build host and staged in by `b6fsutil`. The C11
pass over [ed.c](ed.c) — 50 functions, 46 with an implicit `int`, all with K&R parameter lists,
27 untyped `register` declarations — is described in that file's own header and is not repeated
here. What the port *taught* is below.

## It was 4,805 words and linked no stdio at all; `-x` cost both

| | const | text | data | bss | total |
|---|---|---|---|---|---|
| `ed`, task C3 | 95 | 4,027 | 50 | 633 | **4,805** |
| `ed`, task C19 | 116 | 6,101 | 344 | 2,183 | **8,744** |

Against the 28,672-word ceiling ([../README.md](../README.md) §6) both are nothing, and the
interesting part was *how* the first was nothing: **there is not one format string in `ed.c`**.
Numeric output is `putd()` recursing over `putchr()` into a 70-byte line buffer and a
`write(2)`, so the program linked neither `doprnt` nor a `FILE` — and that is what put it within
twenty-four words of `cat`, at 4,805 against 4,781, despite being five times the source. `ed`
was the one large program on this image that paid nothing for stdio.

**Task C19 ended that, and the shape of the bill is the lesson.** Restoring `-x` (below) added
about 344 words of tables and buffers, which is what a reading of the diff would predict. It
added 3,939. The rest is two libc routines and what stands behind them: `crypt(3)` brings the
DES key schedule, and `getpass(3)` opens `/dev/tty` with `fopen()` — so one call for one line of
input dragged in the whole of stdio, `FILE` buffers and all. **Measure the program, not the
patch**: a shared routine's cost is what it links, not what it is.

It is still the port for which §3's `%D` pass is a *no-op*, which is worth knowing before
grepping for one.

## Two corrections to the record

Both are fixed in [../README.md](../README.md) and [../README.md](../README.md) in the same change,
because a README that records only its own findings leaves the wrong warning standing.

**`ed` has twenty `char *` comparisons, not the ten §2's table claimed** — the densest
concentration in the tree, `sort.c`'s fifteen included. Dropping `-x` took two of them, so
nineteen were rewritten. Every one is a *bound* on a buffer the regex engine or the substitute
path is writing into, and none of them faults when it answers wrongly. Task C19 brought `-x`
back and did **not** bring the two comparisons with it: `crblock()` counts down a `nchar` and
`getkey()` is a `strncpy`, so nineteen is still the number.

**`ed` has no `CCL` bitmap.** The task brief warned that `expbuf` packs character classes "with
`CCL` bitmap arithmetic (`1 << (c & 07)`, `c >> 3`)" and that this was the third thing that
would bite. There is no such arithmetic anywhere in the file. `compile()` writes `CCL`, a count
byte, and then the class's members **laid out literally**, expanding a range one byte at a
time; `cclass()` scans that list; `advance()` steps over it with `ep += *ep`. The bitmap is
*`grep`'s* and *`sed`'s* — `cmd/grep/grep.c` and `cmd/sed/sed0.c` really do pack 128 bits into
16 bytes and really will need 32 — so the warning belongs to task C5 and has been moved there.
The consequence for this task was the pleasant one: **the class encoding was byte-capable
already**, and the only thing that had ever kept a byte above `0177` out of a class was
`getchr()` masking the pattern on its way in.

## The eighth bit, and what the mask was really doing

[../README.md](../README.md) §11 makes eight-bit transparency a rule of the recipe, and task
C11 had just done it to the shell. But the reason it could not be skipped here is sharper than
either: **v7's `getfile()` calls `error()` on any byte with `0200` set**, so this editor could
not so much as *open* `/etc/motd` — 365 bytes of UTF-8 that `cat` has always printed, and that
says on its face that `ed(1)` is on the system.

Four masks went (`getchr()`, `gettty()`, `getfile()`, `dosub()`) and one bit had to be
re-encoded. And what the first of those masks was doing is worth stating exactly, because it is
worse than the `echo привет` bug that prompted task C11. Cyrillic capitals are the UTF-8 pairs
`320 220` through `320 257`, and `& 0177` folds that second byte onto `020`–`057` — a range
containing `*` (052), `.` (056), `$` (044) and `+` (053):

| typed | v7 stored |
|---|---|
| `Ъ` = `320 252` | `P` `*` |
| `Ю` = `320 256` | `P` `.` |
| `Э` = `320 255` | `P` `-` |

So a Cyrillic letter typed into a **regular expression** became a metacharacter. The editor did
not mangle the text so much as silently rewrite the pattern.

### `rhsbuf`: the mark is a prefix byte now

`compsub()` marked a backslash-escaped byte of a substitution's replacement text by setting bit
`0200` of it, and `dosub()` decoded that: it is how `\&` is a literal ampersand where a bare
`&` is the matched text, and `\1` a back-reference where a bare `1` is a digit. A byte-
transparent editor cannot spare that bit, so the mark is a **prefix byte, `QESC` = `0377`**,
exactly as the shell's is ([../sh/README.md](../sh/README.md)):

| in the replacement text | stored in `rhsbuf` |
|---|---|
| an ordinary byte `c`, `c != QESC` | `c` |
| an escaped byte `\c` | `QESC c` |
| the byte `0377`, escaped or not | `QESC QESC` |
| the newline that continues a replacement | `QESC \n` |

A bare `QESC` never appears — `compsub()` only ever writes the pair — so `dosub()`'s decode
cannot be ambiguous. Escaped and unescaped `0377` collapse for the reason they do in the shell:
`0377` is not a metacharacter in a replacement, so its escapedness cannot be observed.

Two things came with it. **`rhsbuf` grew from `LBSIZE/2` to `LBSIZE`**, because a prefix costs a
byte where a set bit cost none and a replacement v7 accepted must not newly overflow; 43 words,
and the one place the encoding costs anything. And **the `esc` flag carries v7's control flow
across the change**: in v7 an escaped byte could not compare equal to `'\n'` or to the
delimiter, the `0200` bit having already made it unequal, which is how `s/x/\//` inserts the
delimiter and how a `\`-continued replacement inserts a newline. Testing `!esc` asks the same
question. Get that wrong and the multi-line substitute — both its forms, the typed one and the
bare newline a `g` command list hands `compsub()` — stops working; `cmd_ed_globsub` is the case
that would say so.

### What follows, and is stated rather than worked around

**The pattern language matches bytes**, which is `sh`'s rule for its globber. `.` matches one
byte, `*` after a multi-byte character repeats that character's last byte, and a range written
between two multi-byte characters is a range of bytes. `ed.1.umm` says so, and `cmd_ed_utf8`
asserts it — including the `3s/^../@/p` that replaces `[` and the first byte of `К` and leaves a
stray continuation byte behind, because that is what the semantics are.

`ESIZE` went from 128 to **512**, because a compiled expression holds a class's members
literally and a UTF-8 letter costs two bytes of one — and a *pattern* costs four per letter,
`CCHR` plus the byte, twice. 512 is 86 words. **That raise required a new check**: the class's
length is a single byte, which `advance()` adds to its cursor, so a class may hold no more than
255 bytes. v7 never needed the test — `ESIZE` 128 bounded the count below 255 by itself — and
without it the raise would have turned a diagnostic into a silently truncated pattern.

`putchr()`'s `l` command got the right answer for free, once: `if (c < ' ')` does not catch a
byte above `0177` because a plain `char` is **unsigned** here, so `l` passes UTF-8 through
rather than escaping every byte of it. On the PDP-11 the same line printed `\3`, the octal of a
negative number.

## What was found and fixed rather than carried

[../README.md](../README.md)'s rule: the fix says which it is, and claiming more than the fix
does is worse than carrying the bug.

* **`mktemp("/tmp/eXXXXX")` writes into a string literal.** `mktemp()` fills the trailing `X`s
  in the buffer it is *handed*, and a literal lives in the read-only const segment here. A
  writable static now. `mktemp` was also declared by no header at all, so
  [../../include/stdio.h](../../include/stdio.h) declares it — beside the comment that already
  mentioned it.
* **`(int)oldintr & 01` as a test for `SIG_IGN`.** `SIG_IGN` is 1, so bit 0 answered the
  question — but `SIG_ERR` is −1 and has bit 0 set too, so a `signal()` that **failed** read as
  "the caller was ignoring this" and the handler was silently never installed. Asks the question
  that was meant now.
* **The line table's growth bumped `nlall` before the attempt that may fail**, and the failure
  path restored `zero = ozero` after the block had already been freed. So after a `?MEM?` the
  table was one block long and `ed` believed it was 512 entries longer — 512 appends past the
  end of a heap block, in the one path that runs when the machine is already out of memory. It
  is a plain `realloc` now, and the `free()` that preceded it is gone: v7's free-then-realloc
  idiom is one this libc still honours (`lib/libc/gen/malloc.c`) and C11 does not, and deleting
  it changes nothing because `realloc` frees a busy block itself.
* **`filename()` copied an unbounded typed file name into `file[FNSIZE]`**, and `main()` did the
  same with `argv[1]` into `savedfile[FNSIZE]`. 64 bytes, no test at all. §6 says every port so
  far has had to bound one of these; this one had two.
* **`blkio()` called `read` and `write` through one `int (*)()`.** Their real prototypes differ
  in the constness of the buffer, so no single pointer type fits both. The caller already had a
  `READ`/`WRITE` flag to hand, so the flag is what comes down now.
* **`l` mishandled DEL**, which is the one bug `ed.1.umm` itself owned up to. The cause is in the
  page rather than the code: it promised "two-digit octal", and two digits cannot spell `0177` —
  v7's `(c >> 3) + '0'` on 127 gives `?`, so DEL listed as `\?7`. Three digits, and the BUGS
  entry is gone.
* `execl("/bin/sh", "sh", "-t", 0)` — a bare `int` 0 as a variadic pointer terminator, the
  hazard `lib/libc/README.md` records for `execle()`.

Two things were looked at and **left alone**, with the reason in the source, because both look
like bugs:

* **`count = (addr2 - zero) & 077777`** on the `=` command is a PDP-11 16-bit artefact, and it
  cannot bite: `getblock()`'s own `bno >= 255` caps the buffer at 130,560 bytes of temp file —
  about 32,640 four-byte line slots — before a line number can reach 32,767. Removing it would
  change nothing and claim something.
* **`init()`'s unchecked `creat`/`open` of the temp file.** The obvious fix is wrong, and why is
  the rule the whole file now obeys: **nothing in `ed` may call `error()` before `main()` has
  reached its `setjmp`**. Dropping `-x` removed the one place that did — `main()` called
  `getkey()`, which calls `error("Input not tty")`, from an arm that ran *before* the `setjmp`,
  so the jump went through an uninitialised `jmp_buf`. `init()` runs there too on the first
  call, and from the `e` command on every later one, where `error()` *is* right. v7 reports the
  failure at the first `blkio()` as `?TMP`, which is a real diagnostic in a reachable place.

## The two worlds, and the one harness change

**Nineteen `b6sim` cases** in [test/](test/) cover the command language and the regex engine at
a few hundredths of a second each. They needed `scripts/run-prog-test.sh` to grow a
**`<case>.in`** for standard input — §9 had named that as the obvious extension and task C5's
filters all want it — and two rules fall out of `error()`:

* **The redirection must be from a file, not a pipe.** `error()` calls
  `lseek(0, 0, SEEK_END)` to throw away the rest of a script after a diagnostic, and only a
  seekable descriptor can answer that.
* **One error per case.** Everything after the first `?` is that discarded remainder, so a
  second probe in the same file is dead text. `cmd_ed_badcmd` is the case that *asserts* the
  discard: its `1,$p` never runs.

**`kernel/test/edit`** (volume 3086) is the other half, and it covers only what `b6sim` cannot:
a `/tmp` belonging to this machine, a `!` with a real `/bin/sh -t` to exec, files long enough
to splice a line across two of `ed`'s 512-byte temp blocks against a real disk, and a
filesystem to `fsck` afterwards. Its strongest assertion needs no checked-in fixture at all:
the guest reads `/etc/motd` and writes it straight back out, and `run-edit.sh` `cmp`s the two
**out of the same extracted image**. That is the file v7's `ed` would have refused outright.

Its **second stage is typed at the Consul**, and it is there rather than in a test of its own
because it needs no second boot and covers what the script cannot: everything `/etc/edit` does
reaches `ed` through a here-document, which is a *file*, so nothing else drives this editor from
a **terminal**. Two things are only visible that way — `getchr()` taking a byte at a time off
the Consul through the clists, Cyrillic included, and `error()`'s `lseek(0, 0, SEEK_END)` on a
descriptor no seek can move. On a file that seek succeeds and discards the rest of the script;
on a tty it fails harmlessly and the editor must go on reading. The `1,$p` typed after the `?`
is what says it did. The dialogue ends with `Q`, so it writes nothing and the three host-side
oracles see exactly what the script left them.

That typed stage is also why the test is **`DISABLED`** as it stands. It fails two runs in six on
an idle tree, always on the *first* send of the dialogue and never inside the scripted half:
`send "ed\r"` is echoed back as `e d`, so the `expect` that follows cannot match. It is the same
wobble that took `console` out — kernel task 35 owns both, and
`../../kernel/test/CMakeLists.txt` carries the transcript and the command to run this one by hand.
Nothing about `ed` is implicated: the nineteen cases above still run on every build, and the
by-hand run passes about two times in three.

Two findings from writing it:

* **`ed` must not be given a file that does not exist** in a here-document. The startup read
  fails, and `error()`'s `lseek` then takes the rest of the *document* with it — a here-document
  being a file. So a file is authored by an `ed` with no argument and a `w name`, which is the
  more honest spelling anyway.
* **It runs the first here-document this image has ever executed** under the booted kernel.
  `cmd/sh/test/heredoc` covers the mechanism under `b6sim`, but the shell writes one to
  `/tmp/sh-<pid><serial>` and unlinks it at once (`cmd/sh/io.c`), which nothing had done here.
  It worked first time — worth writing down precisely because it might not have.

## `-x` was deleted and task C19 put it back

The C3 port dropped v7's encrypting mode whole — `getkey()`, `crinit()`, `crblock()`,
`makekey()`, the `xflag`/`xtflag`/`kflag` state and the `x` command — on two grounds, both
written into [ed.c](ed.c) and [ed.1.umm](ed.1.umm) at the time: `/usr/lib/makekey`, which v7's
`ed` execs to derive a key, was not on the image and was in no task; and `crinit()`'s seed
arithmetic wanted 32-bit wraparound this machine has not got, so the keys would not have matched
a PDP-11's even had it run.

**C19 answered both**, and the answer is [../crypt/rotor.c](../crypt/rotor.c): the key comes from
`crypt(3)` instead of a forked program, and the arithmetic is bounded to 32 bits explicitly.
[../crypt/README.md](../crypt/README.md) is that argument. What matters here is that `ed` and
`crypt(1)` link **the same object file** — `../df` and `../umount` share `../mount/mtab.c` the
same way — so `ed.1`'s and `crypt.1`'s promise that the two interoperate is a property of the
build and not a thing two files have to be kept agreeing about.

Three changes to what came back:

* **`getpass(3)` replaced `getkey()`**, which is what retires the *wild `longjmp`* the C3 note
  recorded. v7's `getkey()` calls `error("Input not tty")`, and the `-x` arm runs before
  `main()` reaches its `setjmp(savej)`, so the jump went through an uninitialised `jmp_buf`.
  `getpass(3)` cannot fail: it takes eight characters — every one `crinit()` reads of v7's nine
  — and answers an empty line with an empty key, which is `ed`'s own "encrypt nothing". **The
  rule the deletion left behind is therefore unbroken**: nothing in this program may call
  `error()` before `main()` has reached its `setjmp`.
* **v7's local `makekey(a, b)` is `tmpkeyinit()`.** It is the temp file's throwaway key, from
  the clock and the pid, and it is not `/usr/lib/makekey` — which is a real program on this
  image now, so the name had become a trap.
* **The 0200 guess is gone**, and this is the one behavioural divergence. v7's `getfile()`
  deciphers a block only if some byte in it has the top bit set, guessing at whether the file is
  encrypted at all. That guess cannot survive an eight-bit-transparent editor: a Cyrillic
  plaintext trips it and a short ciphertext may not. So `-x` means the file *is* encrypted and
  every block is deciphered. Reading a clear file under `-x` gives nonsense rather than the file,
  and [ed.1.umm](ed.1.umm) says so.

### It has no automated test, and it cannot have one

`getpass(3)` wants a terminal. Under `b6sim` there is none, and the fallback — reading standard
input — is worse than useless as a fixture: run from a terminal, `ctest` *would* find a
`/dev/tty` and the case would sit there waiting for someone to type. So `-x` has no
`b6_progtest` case and belongs to no test that survives ([../README.md](../README.md) §9). It
was checked by hand, in both directions and across a temp-file spill, against the host reference
[../crypt/test/mkfix.c](../crypt/test/mkfix.c):

```sh
crypt hobbit <big.txt >big.c            # 18,790 bytes, 300 lines
printf 'hobbit\nw big.p\nq\n' | ed -x big.c
crypt hobbit <big.p | cmp - big.txt     # identical
```

The middle line is the whole of it: `ed` deciphered 300 lines with the user's rotor, spilled
them through 37 enciphered temp-file blocks under a *different* rotor, and enciphered them back
out — and `crypt(1)`, and the PDP-11 reference, both read the result.

## Known limits

* **`ed.hup` is undriven.** No test can send `ed` a `SIGHUP`: `!kill -1 0` would signal the
  login shell too, and there is no other route to it. The code is live — `kernel/sendsig.c`
  delivers handlers and `kernel/test/usig` proves it — but nothing here asserts the file.
* **A mark does not survive the undo of the substitute that moved it.** `substitute()` re-points
  `names[]` at the new line token and `u` puts the old one back, so `'x` then names a token no
  line holds and gives `?`. v7's behaviour; `cmd_ed_markundo` asserts it rather than filing it
  as a surprise.
* **After a `?MEM?` the buffer points at a block the allocator has taken back.** It is safe *by
  arrangement*: the line table is the only allocation `ed` ever makes, so nothing can hand the
  block out again. Anyone who adds a second `malloc` to this program has broken that.
* `l` folds at 72 **bytes**, so a fold can land inside a UTF-8 character.
