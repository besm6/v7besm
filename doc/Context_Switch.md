# Saving and restoring the CPU context on the BESM-6

This is the **worked example**: how a real, production BESM-6 operating system — Dubna — took an
interrupt, took an extracode, saved the CPU context, switched address spaces, and got back out
again. Every claim below is quoted from
[`https://github.com/besm6/besm6.github.io/blob/master/sources/dubna/dub_besm/dubna.dd`](https://github.com/besm6/besm6.github.io/blob/master/sources/dubna/dub_besm/dubna.dd),
62 000 lines of Madlen assembly containing Dubna's complete interrupt vectors, extracode switch,
context save/restore and MMU reload.

It is the companion to [Memory_Mapping.md](Memory_Mapping.md), and the division between them is
sharp: **Memory_Mapping.md says what the *hardware* does at a trap** — what goes into СПСВ, what
`выпр` restores, which mode bits are forced. **This says what a *kernel* has to do about it** —
which registers the hardware does *not* save, the order they must be restored in, and the idioms
that get it done. Where the hardware reference is derived from the
[besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) sources, this is derived from
software that ran on the real machine for two decades.

It is the reference for the unwritten half of [`kernel/besm6.S`](../kernel/besm6.S) — the trap gate
and `save()`/`resume()`, tasks 15 and 16 of [`kernel/TODO.md`](../kernel/TODO.md) — and it
complements [Kernel_Assembly_Routines.md](Kernel_Assembly_Routines.md), which specifies those
routines' contracts with their C callers. That document says *what* each routine must do; this one
shows *how* a kernel that worked actually did it.

> **A note on the octal radix and bit numbering.** Some numbers below are **octal**, and some decimal.
> Please be aware. BESM-6 numbers bits **right-to-left starting at 1**, so bit 1 is the least significant
> and bit 48 the most significant. Same convention as the rest of this project — see
> [Besm6_Data_Representation.md](Besm6_Data_Representation.md).
>
> **A note on the Dubna line references.** They point into `https://github.com/besm6/besm6.github.io/blob/master/sources/dubna/dub_besm/dubna.dd` and were
> verified against the file directly. The routine names (`FULSAV`, `RETURN`, `PUTTMP`, `SAVIND`)
> are the durable references; the line numbers will drift if that file is ever re-imported.

---

## Table of contents

1. [Reading Madlen](#1-reading-madlen)
2. [The two save areas](#2-the-two-save-areas)
3. [The vectors](#3-the-vectors)
4. [The interrupt prologue — the short save](#4-the-interrupt-prologue--the-short-save)
5. [Dispatch](#5-dispatch)
6. [`FULSAV` — the full save](#6-fulsav--the-full-save)
7. [`RETURN` — the restore and the exit](#7-return--the-restore-and-the-exit)
8. [One exit, three doors](#8-one-exit-three-doors)
9. [Extracodes save nothing](#9-extracodes-save-nothing)
10. [The MMU switch](#10-the-mmu-switch)
11. [The БРЗ drain](#11-the-брз-drain)
12. [The scheduler side](#12-the-scheduler-side)
13. [The C register across a trap](#13-the-c-register-across-a-trap)
14. [What this means for the Unix port](#14-what-this-means-for-the-unix-port)

---

## 1. Reading Madlen

A Madlen source line is `label:index_reg,opcode,address`, and a comment is whatever follows a `.`
on the line (or a whole line beginning with `C`). Two conventions matter for everything below.

**A leading `:` opens a new 48-bit word.** The BESM-6 word holds two 24-bit instructions, so the
`:` line is the *left* half and the next line without a `:` fills the *right* half. This is why the
vector blocks read as pairs. `,Z00,addr` emits a half-word whose address field is `addr` and whose
opcode is 000 — a data half-word, used for pointer tables.

**Numeric opcodes appear wherever a mnemonic is unavailable.** Three octal digits means Format 1
(15-bit address), two digits means Format 2 (12-bit address). So `,002,237B` is `рег 0237` and
`3,32,` is `выпр` with index field 3.

The mnemonics that carry the context machinery, cross-checked against this repo's own assembler
table in [`cmd/as/tables.c:114-172`](../cmd/as/tables.c#L114-L172):

| Madlen | Octal | Russian | What it does |
|--------|-------|------|---|
| `MOD`  | `002` | рег  | read/write a CPU-internal register (supervisor only) |
| `XTR`  | `027` | рж   | **set the mode register R from memory** |
| `RTE`  | `030` | счрж | **read R into the accumulator's exponent field** |
| `YTA`  | `031` | счмр | **read the younger-bits register Y (РМР)** |
| `NTR`  | `037` | ржа  | set R from the immediate address |
| `ATI`  | `040` | уи   | `M[i] := A` |
| `STI`  | `041` | уим  | `M[i] := A`, **and pop** |
| `ITA`  | `042` | счи  | `A := M[i]` |
| `ITS`  | `043` | счим | **push A, then `A := M[i]`** |
| `MTJ`  | `044` | уии  | `M[j] := M[i]` |
| `VTM`  | `24`  | уиа  | `M[i] := address` (immediate) |
| `UTM`  | `25`  | слиа | `M[i] += address` |
| `VLM`  | `27`  | цикл | loop: if `M[i] ≠ 0` then `M[i] += 1`, jump |
| `IJ`   | `32`  | выпр | **return from interrupt** |

`ITS` and `STI` are the whole trick, and they deserve emphasis up front. `ITS` (счим) **pushes the
accumulator and loads `M[i]` into it in one instruction**; `STI` (уим) is the exact inverse. Since
М15 is the stack pointer, `ITS` is a one-instruction "store register *i* and advance". Dubna's
entire context machinery is built on these two.

Two traps for the reader grepping this file:

- **The Cyrillic is written with Latin homoglyphs.** The listing came through a 6-bit BESM-6
  character set, so `ЭKCTPAKOД` ("extracode") is `Э` + Latin `K,C,T,P,A,K,O` + Cyrillic `Д`.
  `grep экстракод` finds *nothing*. The pattern that works is
  `grep "[ЭE][KК][CС][TТ][PР][AА][KК][OО]Д"`.
- **Octal is written with a trailing `B`**, so grepping for `0550` or `0577` finds nothing either;
  the text says `577B`. And a decimal `-11` really is decimal — `-10` and `-11` in the same routine
  can mean −8 and −11.

---

## 2. The two save areas

Dubna has two, they are different sizes, and conflating them is the easiest mistake to make.

### `SMASAV` — the global scratch for the current trap

Eight words, declared at `dubna.dd:16413`, holding the state of whatever was interrupted
*right now*:

```
16413    SMASAV: LC ,BLOCK, SAVAC, SAVSR, SAVAR, SAVI15, SAVI14, SAVI13, SAVMIR
16414               ,CONT,  SAVS16
```

| Cell                         | Holds |
|------------------------------|-------|
| `SAVAC`                      | the accumulator |
| `SAVSR`                      | the mode register **R** (ω-mode and the NTR suppress bits) |
| `SAVAR`                      | the **Y** younger-bits register — despite the name |
| `SAVI15`, `SAVI14`, `SAVI13` | М15, М14, М13 |
| `SAVMIR`                     | **ГРП**, the main interrupt register |
| `SAVS16`                     | М16 |

There is exactly **one** of these. It is safe because interrupts are blocked from the moment the
hardware takes one until the `выпр` that ends the handler, so the block cannot be re-entered. Every
module that needs it re-declares it with only the fields it uses — the vector block at
`dubna.dd:16048` declares just `SMASAV:LC,BLOCK,SAVAC`, which is all a `,ATX,SAVAC` needs.

### ИПЗ — the per-process block

**ИПЗ** («индивидуальное поле задачи», *individual task field*) is Dubna's u-area. Its register
save field is **24 words at offset 0** of the task block (`dubna.dd:488`):

```
488     *0:  B ,BLOCK, SUMMATOR(24) . PEГИCTPЫ        "REGISTERS"
489         30 ,CONT,  TMATH(40)    . MAT.ЛИCTЫ       "user pages"
490        100 ,CONT,  REGSCAL      . PAЗHЫE ФЛAГИ    "various flags"
```

`SUMMATOR(24)` is 24 decimal = 030 octal words. The source documents the layout itself at
`dubna.dd:12110-12123`, and this table is a transcription of that comment block:

| Offset    | Contents                               | Dubna's words            |
|-----------|----------------------------------------|--------------------------|
| `0`       | accumulator                            | `CYMMATOP`               |
| `1`       | mode register **R**                    | `PEЖИM  A Y` — "AU mode" |
| `2`       | **Y** younger bits                   | `PEГИCTP MЛAДШИX PAЗPЯДOB` |
| `3`       | **ИРЕТ** — interrupt return address    | `AДPEC ПPEPЫBAHИЯ (И33)` |
| `4`       | **ЭРЕТ** — extracode return address    | `AДPEC ЭKCTPAKOДA (И32)` |
| `5`       | **СПСВ** — saved mode word             | `PEЖИM  Y Y  (И27)` |
| `6`–`13`  | М16, М15, М14, М13, М12, М11, М10, М9  | |
| `14`–`21` | М8, М7, М6, М5, М4, М3, М2, М1         | |
| `22`–`23` | М34, М35 — the address-break registers | |

Note the register file is stored **in descending order**, М16 down to М1 — a consequence of the
`ITS` push sequence that fills it (§6). And М16 is not a sixteenth index register — there are only
fifteen. It is the **C register**, the address modifier, which happens to be addressable as register
16; saving it here is load-bearing, and §13 is devoted to why.

The layout is independently confirmed by the symbolic offsets declared 4 000 lines away at
`dubna.dd:16446-16456`, which land on exactly the right slots:

```
16446    S33: ,EQU, 3       . slot 3  = ИРЕТ
16447    S32: ,EQU, 4       . slot 4  = ЭРЕТ
16448    S27: ,EQU, 5       . slot 5  = СПСВ
16449    I14: ,EQU, 10B     . slot 8  = М14
16456     I5: ,EQU, 21B     . slot 17 = М5
```

Four independent equates, four exact hits. The layout above is not an inference.

### The MMU shadow lives inside the page table

Immediately above the save field, three `,EQU,`s overlay the *same words* three ways
(`dubna.dd:12128-12133`):

```
12128   C.....   TAБЛИЦA MATEMATИЧECKИX ЛИCTOB          . "table of user pages"
12129    TMATH:,EQU,30B.PAЗPЯДЫ 48-31   ДЛИHA : 50B     . "bits 48-31, length 50B"
12130   C.....   TAБЛИЦA ПPИПИCKИ                        . "MAPPING table"
12131    TПPИП:,EQU,30B.PAЗPЯДЫ 24-1    ДЛИHA : 8        . "bits 24-1,  length 8"
12132   C.....   TAБЛИЦA ЗAЩИTЫ                          . "PROTECTION table"
12133    TЗAЩ:,EQU,40B.PAЗPЯДЫ 28-21   ДЛИHA : 4         . "bits 28-21, length 4"
```

One 48-bit word per virtual page carries the page bookkeeping in bits 48–31, the **РП descriptor in
bits 24–1**, and — for the first four words — the **РЗ byte in bits 28–21**. The shadow copies of
the write-only MMU registers are packed *inside* the page table, not kept beside it. This is what
`PUTTMP` (§10) reads.

---

## 3. The vectors

`dubna.dd:16048-16052` — two words, four half-instructions:

```
16048    SMASAV:LC,BLOCK,SAVAC
16049    ,ATX,SAVAC.500B                                    . word 0500, left half
16050    ,UJ,INTINTER.BXOД ПPИ BHYTPEHHEM ПPEPЫBAHИИ        . 0500 right: "entry on internal interrupt"
16051    ,ATX,SAVAC.501B                                    . word 0501, left half
16052    ,UJ,EXTINTER.BXOД ПPИ BHEШHEM ПPEPЫBAHИИ           . 0501 right: "entry on external interrupt"
```

Word **0500** is the internal fault, word **0501** the external interrupt, and each is *store the
accumulator, then jump*. **A is spilled in the very first half-instruction**, before anything can
clobber it. That is the only thing the vector does.

The extracode switch is 24 words at **0550–0577**, one per extracode э50…э77
(`dubna.dd:16115-16164`):

```
16115   C         ПEPEKЛЮЧATEЛЬ ЭKCTPAKOДOB     . "EXTRACODE SWITCH"
16116    :,UJ,MACRO50                           . 0550 left
16117     ,Z00,TYPEMT                           . 0550 right — data, never executed
16118    :,UJ,MACRO51                           . 0551
16119    ,Z00,SCDICH.ГYPEBИЧ                    .      ("Gurevich")
...
16133    :,UJ,MACRO60                           . 0560
16134    ,Z00,*LINKPAR.560B                     .      <- the comment asserts the address
16135    :,UJ,MACRO61.                          . 0561
16136    ,Z00,*SCAL71.561B                      .      <- and again
...
16163    :14,VTM,577B.                          . 0577 left  — M14 := 577
16164    ,UJ,BADMACRO.                          . 0577 right — "bad extracode"
16165    :,BSS,100B.                            . 0600-0677 reserved
```

The block is **dual-purpose**. Because the left half always branches away, the right half is free,
and the kernel stores a per-extracode base address there as a `,Z00,` constant. The vector table
*is* the pointer table. Nothing is wasted.

### Dubna has no origin directive

Nothing in `dubna.dd` says "org 0500". `MAINSW` is an ordinary Madlen module and the loader
places it; the source only *documents* the fact, two ways — trailing comments (`.500B`, `.560B`,
`.561B`) and labels named after their own address (`*511:`, `*522:`, `*547:`). The word count from
0500 hits every one of them exactly, which is how the placement was confirmed for this document,
but the placement decision itself lives outside the file.

**We are on firmer ground.** [`kernel/besm6.S:47`](../kernel/besm6.S#L47) uses a real `.org 0500`,
and the assembler enforces it. No change needed here — this is just the contrast worth knowing when
reading Dubna, because those `*NNN:` labels are assertions, not addresses.

---

## 4. The interrupt prologue — the short save

`dubna.dd:16516-16531`, the body reached from word 0501. `INTINTER` at
`dubna.dd:16552-16584` is its near-twin for internal faults.

```
16514   C         OБPAБOTKA BHEШHИX ПPEPЫBAHИЙ    . "handling of external interrupts"
16516    ,  RTE  , 07777B        .                . A := mode register R      (счрж)
16517    ,  ATX  ,SAVSR          .                . SAVSR := R
16518    ,XTA,DISREG.                             . A := DISREG (= 02013)
16519    ,ATI,21B.OCTAHOB KK,KЧ                   . ПСВ := 02013  "halt on instr-check / number-check"
16520    ,  ITA  , 15            .                . A := М15  (the interrupted stack pointer)
16521    15,VTM,SAVI15                            . М15 := &SAVI15   <- retarget the stack at SMASAV+3
16522    ,ITS,14                                  . [SAVI15] := old М15 ; A := М14
16523    ,ITS,13                                  . [SAVI14] := М14    ; A := М13
16524    ,ITS,16                                  . [SAVI13] := М13    ; A := М16
16525    ,ATX,SAVS16                              . SAVS16 := М16
16526    ,YTA,                                    . A := РМР                  (счмр)
16527    ,ATX,SAVAR                               . SAVAR := РМР
16528    ,  MOD  , 00237B        .                . A := ГРП                  (рег, read)
16529    ,ATX,SAVMIR                              . SAVMIR := ГРП
16530    ,  AAX  , COMMIR        .                . A &= the enabled-interrupt mask
16531    ,  UZA  ,L15710         .                . nothing enabled -> error exit
```

Order of business: **R first** (`RTE 7777` reads it into A's exponent field), then lock the machine
down, then the index registers, then Y, then ГРП.

`DISREG` is a block holding `C2013` (`dubna.dd:15458`) = **02013** = БлПр + halt-on-check + БлЗ
+ БлП. The hardware has already forced БлП/БлЗ/БлПр on at the vector; this re-asserts them and adds
the check-halt bit, so a fault while handling a fault stops the machine instead of recursing.

**The stack pointer saves itself.** Lines 16520–16522 are worth reading twice: `ITA 15` copies М15
into A, `15,VTM,SAVI15` repoints М15 at the save block, and the first `ITS` pushes the old М15 into
its own slot. Three instructions, no scratch cell, and the stack is now inside `SMASAV`.

### What is *not* saved

**Only A, R, Y, ГРП and М13–М16.** М1–М12 are left live in the CPU, still holding the interrupted
program's values. The handler bodies are simply written not to touch them.

This is the design decision at the heart of Dubna's trap path: the prologue is deliberately
minimal, and the full 24-word context is materialised only when a task actually has to be parked.
That is `FULSAV` (§6), and most interrupts never call it.

---

## 5. Dispatch

`dubna.dd:16532-16547`:

```
16532    INTER:,ENTRY,.
16537    ,ATX,PERREG                . pending := ГРП & COMMIR
16538    ,AAX,PRT/MASK              . keep only the priority bits
16539    :,U1A,*+1                  . if any priority bit, SKIP the next half-word
16540    ,XTA,PERREG                . else reconsider all pending
16541    :,  ANX  ,               . . A := number of the highest set bit    (нед)
16542    ,  ATI  , 14            .  . М14 := bit number
16543    14,  XTA  ,BITS           . . A := the one-hot mask for that bit
16544    ,AEX,CLEMIR.ГAШ.BH.ПPEP.   . "clear the internal interrupt"
16545    ,  ATI  ,               .  . уи 0 — М0 is hardwired zero: a no-op
16546    ,  MOD  , 00037B        .  . ГРП &= A   — AND-mask clears the bit
16547    14,   UJ  , SWINT-1       . . dispatch
```

**The priority trick at 16539** exploits the half-word layout. `*+1` is the *next word*, and
`,XTA,PERREG` is the *right half of the current one* — so a taken branch skips it. If any priority
bit is pending, A keeps only the priority bits; otherwise A is reloaded with everything pending.
`ANX` (нед) then picks the highest set bit, numbering bit 48 as 1.

`PRT/MASK` is `,OCT,.6010 01` (`dubna.dd:16587`) = ГРП bits 18, 17, 10 and 1.

Because `нед` numbers bit 48 as 1, `14,UJ,SWINT-1` with М14 = 1 lands on `SWINT+0`. The table at
`dubna.dd:16596` is therefore **ordered ГРП bit 48 down to bit 10, one word per bit, highest
bit = highest priority** — and the source's own comments confirm the alignment (the ninth entry is
commented `40 P. ГPП`, and 48 − 9 + 1 = 40).

### The asymmetry worth knowing

**External interrupts clear one bit; internal faults clear them all.** The external path masks with
`CLEMIR` and clears only the bit being dispatched, preserving the rest for the next round. The
internal path does this instead (`dubna.dd:16566-16568`):

```
16566    ,XTA,CLEMIR.               . A := CLEMIR = 7777 7777 0140 3000
16568    ,MOD,37B.                  . ГРП &= CLEMIR : clear ALL internal bits at once
```

`CLEMIR` is an AND-mask whose *zero* bits are what gets cleared — bits 24–20, 17–12 and 9–1, exactly
the internal-fault bits. The reasoning is sound and worth stealing: **a fault is not queued.** If
the condition persists it will be re-raised by the very next instruction, so there is nothing to
preserve. A device interrupt, by contrast, is a one-shot notification that must not be lost.

---

## 6. `FULSAV` — the full save

`dubna.dd:15474-15501`. The header comment at 15472 reads `ПOЛHOE YПPЯTЫBAHИE` — **"full
stow-away"**.

```
15474    FULSAV:,ENTRY,.
15476    ,XTA,Г Y C.                . A := (ГУС) — pointer to the current task's ИПЗ
15477    ,ATI,15.AДPEC ИПЗ          . М15 := ИПЗ base   "address of ИПЗ"
15478    :,XTA,SAVAC                . <- SAVTOTAL enters HERE
15479    ,XTS,SAVSR                 . [ИПЗ+00] := SAVAC  ; A := SAVSR
15480    ,XTS,SAVAR                 . [ИПЗ+01] := SAVSR  ; A := SAVAR
15481    ,ITS,33B                   . [ИПЗ+02] := SAVAR  ; A := М033 (ИРЕТ)
15482    ,ITS,32B                   . [ИПЗ+03] := ИРЕТ   ; A := М032 (ЭРЕТ)
15483    ,ITS,27B                   . [ИПЗ+04] := ЭРЕТ   ; A := М027 (СПСВ)
15484    ,XTS,SAVS16                . [ИПЗ+05] := СПСВ   ; A := SAVS16
15485    ,XTS,SAVI15                . [ИПЗ+06] := SAVS16 ; A := SAVI15
15486    ,XTS,SAVI14                . [ИПЗ+07] := SAVI15 ; A := SAVI14
15487    ,XTS,SAVI13                . [ИПЗ+10] := SAVI14 ; A := SAVI13
15488    ,ITS,12                    . [ИПЗ+11] := SAVI13 ; A := М12
15489    ,ITS,11                    . [ИПЗ+12] := М12    ; A := М11
15490    ,ITS,10                    .   ...
15491    ,ITS,9
15492    ,ITS,8
15493    ,ITS,7
15494    ,ITS,6
15495    ,ITS,5
15496    ,ITS,4
15497    ,ITS,3
15498    ,ITS,2
15499    ,ITS,1                     . [ИПЗ+24] := М2     ; A := М1
15500    15,ATX,.                   . [ИПЗ+25] := М1
15501    13,UJ,.                    . return via М13
```

Every destination matches the §2 layout exactly: A, R, Y, ИРЕТ, ЭРЕТ, СПСВ, М16…М1.

**This is a software pipeline.** Each `ITS`/`XTS` simultaneously retires the previous value and
fetches the next, so 22 words are saved in 22 instructions with **no scratch cell and no loop
overhead**. The accumulator is the pipeline register. It is the single most elegant thing in the
file, and it is the idiom to copy.

Note what `FULSAV` is *not* doing: it never reads a register the prologue already spilled. A, R,
Y, М13–М16 come out of `SMASAV`; only М1–М12 and the three spec registers are read live. The two
save areas compose.

`FULSAV+1` is exported as **`SAVTOTAL`** (`dubna.dd:32672`):

```
32672    SAVTOTAL:,EQU,FULSAV+1
```

Entering one word in skips the `ГУС` lookup, so any caller can point М15 at an area of its own and
reuse the 22-instruction body — `13,VJM,SAVTOTAL` at `dubna.dd:32743`, and four other sites.
One save routine, two entry points, no duplication.

---

## 7. `RETURN` — the restore and the exit

`dubna.dd:15512-15583`. The tail is the part to study:

```
15572   C   BOCCTAHOBЛEHИE PEГИCTPOB ИЗ ИПД     . "restoring registers from ИПД"
15573    15,VTM,SAVI13+1                        . М15 := top of the save block
15574    ,XTA,SAVS16                            . A := SAVS16
15575    ,STI,16                                . М16 := A ; pop -> A := SAVI13
15576    ,STI,13                                . М13 := A ; pop -> A := SAVI14
15577    ,STI,14                                . М14 := A ; pop -> A := SAVI15
15578    ,ATI,15.                               . М15 := A   (ATI — no pop, the chain ends)
15579    ,XTA,SAVAR                             . A := saved РМР
15580    ,AEX,.BOCCTAHOBЛEHИE MЛ.PAЗPЯДOB       . "restoration of the younger bits"
15581    ,XTA,SAVAC.                            . A := saved accumulator
15582    ,XTR,SAVSR.                            . R := saved mode register
15583    3,32,.BOЗBPAT ИЗ  ПPEPЫBAHИЯ           . выпр — "RETURN FROM INTERRUPT"
```

`STI` is the exact inverse of `ITS`: it writes an index register **and pops**, so each instruction
retires one register and fetches the next — the pipeline run backwards. `,ATI,15` breaks the chain
at the end, because restoring М15 destroys the stack pointer it is popping from.

Four things here are not obvious, and all four matter.

### `,AEX,` at 15580 is not a XOR

It is `нтж` with a blank address — XOR the accumulator with word 0, which always reads 0, so **A is
unchanged**. The instruction exists *solely* for its side effect: a logical operation copies the old
A into Y. `XTA SAVAR; AEX 0` is therefore "**Y := the saved Y**", and it is the only way to
write that register — the architecture provides `счмр` to read it and nothing at all to write it.

A is garbage afterward, which is why 15581 reloads it.

### The restore order is forced

**Y → A → R.** Not a style choice:

- `XTA` (сч) is documented as **not** clearing Y, unlike `и`/`слц`/`сл`, which all set it to 0.
  So A can be reloaded *after* Y without destroying it. Any other order does destroy it.
- `XTR` must be last, because a subsequent `XTA` would perturb the mode bits it just restored.

### `,XTR,SAVSR` is how ω comes back

`рж` sets `R = X[47:42]` — the exact inverse of the prologue's `RTE 7777`. **The hardware does not
restore R.** Software does, and if software forgets, the interrupted program resumes with whatever
ω-mode and NTR suppress bits the handler happened to leave behind. Hold that thought for §14.

### `3,32,` is `выпр` through ИРЕТ

Index field 3, and the hardware computes `PC = M[(reg & 3) | 030]` → `M[033]` = **ИРЕТ**. It also
restores **БлП, БлЗ, БлПр and the supervisor bits from СПСВ**, all in one instruction — see
[Memory_Mapping.md](Memory_Mapping.md), "выпр". So the mode word is *not* restored by any
instruction in the listing; `ATI 21B` writes the *current* ПСВ, which `выпр` immediately overwrites
from СПСВ.

Which means: **the kernel steers `выпр` by editing СПСВ.** `dubna.dd:15551-15553` does exactly
that —

```
15551    ,ITA,27B                     . A := СПСВ
15552    ,AAX,=77677.YCT.0 ПOK        . clear one bit   "set ПоК to 0"
15553    ,ATI,27B                     . СПСВ := A
```

— and §12 shows the scheduler doing it wholesale.

### `RETURN` is a loop, not a straight line

```
15513    RETURN:,002,237B.CЧИTЫBAHИE ГPП     . A := ГРП   "reading ГРП"
15514    ,AAX,COMMIR.KOПИЯ MACKИ ГPП         . A &= the mask copy
15515    ,U1A,INTER.ECTЬ EЩE ПPEPЫBAHИЯ      . "there are more interrupts" -> back to dispatch
15516    ,XTA,MSELECT
15517    ,U1A,NEWTA                          . reschedule requested?
```

Before returning, it **re-reads ГРП and jumps back into `INTER`** if anything is still pending —
dispatching the next interrupt without ever leaving supervisor mode. Interrupts drain *fully* before
user code resumes. The comment on the other end of the back-edge (`dubna.dd:16534-16535`) says
so from the receiving side: *"entry from the block 'return from interrupt' — there is another
interrupt"*. Only when ГРП is quiet does 15516 go on to check the scheduler.

---

## 8. One exit, three doors

This is the best idea in the file.

An extracode returns via ЭРЕТ (`выпр` with reg ≡ 2), an interrupt via ИРЕТ (reg ≡ 3). They cannot
share an exit path — which is precisely the hazard [Memory_Mapping.md](Memory_Mapping.md) flags:
*"A single `выпр` in a shared trap-exit path must therefore know which door it came in by."*

Dubna's answer is not to branch. It **normalises the door** (`dubna.dd:15506-15508`):

```
15504   C            BЫXOД ИЗ ЭKCTPAKOДOB     . "EXIT FROM EXTRACODES"
15506    OUTMACRO:,ENTRY,
15507    ,ITA,32B                             . A := М032 = ЭРЕТ
15508    ,ATI,33B                             . М033 := A  -> ИРЕТ
15509   C
15510   C                 BOЗBPAT ИЗ ПPEPЫBAHИЯ
15512    RETURN:,ENTRY,.
```

**Two instructions.** `OUTMACRO` copies ЭРЕТ into ИРЕТ and falls straight through into `RETURN`, so
the single hardcoded `3,32,` serves both doors. And the payoff compounds: every system-call return
now automatically inherits the interrupt epilogue's ГРП polling, its reschedule check and its
debugger hooks, for free.

The third door is a task that was never interrupted at all. `SELECT` (`dubna.dd:15596-15600`
onward) **forges** ИРЕТ and СПСВ and executes the same instruction:

```
15596    SELECT:,ENTRY,.
15597    SELECT:,24,2003B                . уиа 2003(0) — lock down: БлП+БлЗ+БлПр
15598    15,VTM, Д H З
...
         12,MTJ,33B                      . М033 := М12   (forge ИРЕТ)
         12,VTM,13B                      . М12 := 013
         12,MTJ,27B                      . М027 := М12   (forge СПСВ = 013)
         3,IJ,                           . выпр — "return" into a task that never trapped
```

There is exactly one way into user mode on this machine, and Dubna uses it for all three cases:
resuming an interrupt, returning from a system call, and launching a brand-new task.

What makes it safe is a house rule, stated in capitals at the top of the file
(`dubna.dd:300-310`):

```
300     C******          B H И M A H И E                . "ATTENTION"
302     C        BCE ЭKCTPAKOДЫ,KOTOPЫE MOГYT           . "ALL EXTRACODES THAT CAN BE
303     C        ПPEPЫBATЬCЯ, B TOM ЧИCЛE ИЗ-ЗA         .  INTERRUPTED, INCLUDING BECAUSE
304     C        OБPAЩEHИЙ ПO MAT. AДPECY ИЛИ           .  OF REFERENCES BY USER ADDRESS
305     C        ИЗ-ЗA TPACCИPOBKИ, ДOЛЖHЫ BOЗ-         .  OR BECAUSE OF TRACING, MUST
306     C        BPAЩATЬ YПPABЛEHИE ЧEPEЗ               .  RETURN CONTROL THROUGH
307     C        RETURN  ИЛИ  OUTMACRO                  .  RETURN OR OUTMACRO
308     C        ДЛЯ ПPABИЛЬHOЙ PAБOTЫ OTЛAДЧИ-         .  FOR CORRECT OPERATION OF THE
309     C        KOB - ИHTEPAKTИBHOГO И ПAKETHOГO       .  DEBUGGERS — INTERACTIVE AND BATCH"
```

### An aside: `,24,2003B` is not a mystery

It appears ~40 times and looks like `уиа М0, N` — architecturally a no-op, since М0 reads as zero.
It is not. In **supervisor mode with register 0**, `уиа` writes ПСВ's БлП/БлЗ/БлПр **from the
address field** — see [Memory_Mapping.md](Memory_Mapping.md), which documents this as the cheapest
way to flip the mode bits. So:

- `,24,2003B` = БлПр + БлЗ + БлП all on — full lockdown.
- `,24,2002B` = the same but **БлП off** — data mapping back on, which is what makes
  `copyin`/`copyout` free.

`dubna.dd:15881-15887` uses exactly that bracket to load the address-break registers through the
user's mapping, and it is the same bracket [`kernel/TODO.md`](../kernel/TODO.md) specifies for our
own user-access family.

---

## 9. Extracodes save nothing

Look again at the vector block in §3. **Twenty-two of the twenty-four words save not one register.**
`:,UJ,MACRO50` is the *entire* prologue for extracode 050. Only 0563 (`,ATX,SAVAC`) and the 0577
catch-all touch any state at all — and 0577 *destroys* М14 (`14,VTM,577B`), because `BADMACRO` no
longer needs it.

Set against the 8-word interrupt prologue of §4, that is a stark contrast, and it is entirely
justified:

- **An extracode is synchronous.** It appears at a point the user program chose, so it is a *call*,
  and the BESM-6 calling convention already makes the caller responsible for its own live
  registers. An interrupt lands between two arbitrary instructions and must preserve the whole
  visible machine.
- **The hardware makes it cheaper still.** The extracode gate writes `M[016] = EA`
  unconditionally, so **М14 is caller-saved by architectural fiat** — the caller could not rely on
  it even if the kernel wanted it to.

Handlers that *do* need registers save them in the handler body, not the gate, and save only what
they personally use. The mathematical package's shared prologue (`dubna.dd:29864-29874`) saves
four: A, R, М13, М15.

### How a handler knows which extracode, and with what argument

**Which one: by the address it landed on.** There is no code number anywhere — the hardware picks
the word, and each of the 24 is hard-wired to its own handler. Only the catch-all needs its own
identity, so it manufactures one (`14,VTM,577B`, `dubna.dd:16163`).

**The argument: EA in М14, the operand in the accumulator.** The switch exploits this to overload
one extracode two ways, testing М14 in the vector word itself:

```
16129    :14,VZM,MACRO56        . if М14 == 0 -> MACRO56   (the mathematical meaning)
16130    ,UJ,MACRO7CH           . else -> the 7-channel handler
16131    :14,VZM,MACRO57        . if М14 == 0 -> MACRO57
16132    ,UJ,MACRO NEW          . else -> "new extracodes"
```

`EA == 0` means the classical *mathematical* extracode — operand in A, address unused, so М14 is
free. `EA != 0` means the *operating-system* meaning, where EA is a selector and the handler
sub-dispatches on its value. This is why `MACRO50`…`MACRO57` are defined twice in the listing (the
OS gates at `:16116+`, the math library at `:29855+`); the `VZM`/`V1M` tests are what choose between
them at run time.

Note also the **vector aliases**: `э20`/`э60` share word 0560 and `э21`/`э61` share 0561, because
the hardware maps `э20`/`э21` to `0540 + (opcode >> 3)`. Pick one of each pair and leave the other
alone.

Finally, **ЭРЕТ already points past the extracode** — the gate stores `nextpc`, not `pc`. An
extracode needs no "skip the faulting instruction" fixup, unlike the fault path, where
`SPSW_NEXT_RK` and `SPSW_RIGHT_INSTR` must be unwound by hand.

---

## 10. The MMU switch

`PUTTMP` — `ПOДПPOГPAMMA OTKPЫTИЯ ПPИПИCKИ И ЗAЩИTЫ`, "subroutine to open mapping and protection"
(`dubna.dd:19173-19188`). The **entire address space of a process in twelve instructions**:

```
19173    PUTTMP:,NAME,.
19174   C---------  ПOДПPOГPAMMA OTKPЫTИЯ ПPИПИCKИ И ЗAЩИTЫ
19175    Z28/21:,LC,1
19176    15,UTM,44B                 . М15 := ИПЗ + 44B  (one past TЗAЩ, which is 40B..43B)
19177    13,VTM,-3                  . М13 := -3
19178    L1:15,XTA,                 . pop: М15 -= 1 ; A := [М15]   (TЗAЩ+3 .. TЗAЩ+0)
19179    13,MOD,33B                 . РЗ[33B+М13] := A  =>  030B..033B  = РЗ0..РЗ3
19180    13,VLM,L1                  . 4 iterations
19181    13,VTM,-7                  . М13 := -7
19182    L2:15,XTA,                 . pop: A := [М15]              (TПPИП+7 .. TПPИП+0)
19183    ,ASN,64-8.                 . shift left 8
19184    ,AUX,Z28/21                . unpack under mask — extract bits 24-1
19185    13,MOD,27B                 . РП[27B+М13] := A  =>  020B..027B  = РП0..РП7
19186    13,VLM,L2                  . 8 iterations
19187    14,UJ,                     . return
```

Two tiny `VLM` loops: **4 × `рег` for РЗ, 8 × `рег` for РП** — 32 pages of mapping and 32 protection
bits — reading the ИПЗ page table backwards through the stack-mode `15,XTA,`. The `ASN 64-8` /
`AUX` pair extracts the descriptor from bits 24–1, since the same word also carries the `TMATH`
bookkeeping in bits 48–31 (§2).

`PUTTMP` is called from every context-switch site: `dubna.dd:12545`, `12728`, `15878`, `53933`.
The numbered comments around the caller at `dubna.dd:12724-12731` are Dubna's own outline of a
context switch:

```
12723   C    7. YCTAHOBKA PП И PЗ           . "7. setting up РП and РЗ"
12725     1,MTJ,11
12726     ,WTC,ГYC
12727     15,VTM,
12728     14,CALL,PUT TMP
12730   C    8. BOCCTAHOBЛEHИE PEГИCTPOB    . "8. restoring the registers"
```

**This independently confirms our design.** [`sureg()`](../kernel/utab.c) in `kernel/utab.c` does
the same job in twelve `рег`s from `u.u_upt[8]`, for the same reason: РП and РЗ are **write-only**,
so a shadow copy in memory is not an optimisation, it is the only way to know the current mapping.
Dubna reached the identical conclusion — every live MMU write in the file is paired with a write to
the in-memory image (`OPENPAGE` at `dubna.dd:28389-28413`, the swapper at `:32184` and `:32193`).

---

## 11. The БРЗ drain

The nine-store rule is real, and Dubna does it. `dubna.dd:11123-11138`, in the memory-sizing
code:

```
11121   C---  BЫЯCHИM PAЗMEP ПAMЯTИ :        . "let's determine the size of memory"
11123    ,XTA,
11124    ,ATX,-1
11125    ,ATX,-2
11126    4,VTM,-10                            . М4 := -8   (octal 10)
11127    :,ATX,1                              . store to physical 1 — the БРЗ flush port
11128    4,VLM,*                              . 9 executions of ATX -> evict all 8 lines
11129    ,XTA,=440 0000 1777.0==>137
11130    ,MOD,20B.1==>77                      . NOW rewrite РП0
11131    :,24,2002B                           . уиа 2002(0) — БлП off
11132    ,ATX,1777B
11133    ,ATX,3776B
11134    3,VTM,HAЧBEPX
11135    :,24,2003B                           . уиа 2003(0) — БлП back on
11136    4,VTM,-10
11137    :,ATX,1                              . drain again
11138    4,VLM,*
```

`4,VTM,-10` is octal, so М4 = −8; `VLM` ("if `M[i] ≠ 0` then `M[i] += 1`, jump") therefore yields
**exactly nine `ATX` executions**. Drain, rewrite РП, drain again.

Nine — not eight. That matches this project's own analysis in
[Memory_Mapping.md](Memory_Mapping.md) verbatim: *"The first such store only arms the counter —
eviction begins with the second — so nine consecutive stores are needed to drain all eight lines"*,
and *"A context switch must drain БРЗ before reloading РП."*

**So [`kernel/brz.s`](../kernel/brz.s) is confirmed by an authentic kernel.** The nine consecutive
stores, and the insistence that they be consecutive, are not an artefact of the simulator or an
over-reading of the hardware manual: Dubna wrote the same loop, for the same reason, and shipped it.
That is about as strong an independent confirmation as this port is ever going to get.

> **One open question, flagged honestly.** On the full-restore path the order looks wrong.
> `dubna.dd:15878` calls `PUTTMP` — which rewrites РП — and the only store to physical 1 on that
> path is a lone `,ATX,1` at `:15889`, *after* the fact, and a single store only arms the counter
> without evicting anything. Per the coherence hazard, the drain should precede the РП reload. It
> may be that the callers (`:12728`, `:53933`) drain earlier, or that it does not matter because the
> kernel runs with БлП set so its own stores are physical and untagged. **We have not traced every
> caller, so this is an open question, not a confirmed bug in Dubna** — and it does not affect the
> conclusion above, which rests on the `:11123` sequence.

---

## 12. The scheduler side

### `SAVIND` — a BESM-6 `setjmp`

`dubna.dd:15924-15945`. Called as `15,VJM,SAVIND`, and `VJM` deposits the return address into
М15. That is the whole trick:

```
15924    SAVIND:,ENTRY,.
15926    :   ,24,2003B.                . lock down
15927    ,ATX,SAVACC.                  . stash A in a local scratch
15928    ,ITA,15.                      . A := М15 = the VJM return address
15929    ,WTC,Г Y C.                   . C := (ГУС)
15930    15,VTM,.                      . М15 := ИПЗ base
15931    15,ATX,3.                     . [ИПЗ+03] := return address   <- И33 = ИРЕТ = the resume PC
15932    ,XTA,C7.
15933    15,ATX,5.                     . [ИПЗ+05] := 7                <- И27 = СПСВ = БлП|БлЗ|БлПр
15934    ,RTE,177B.
15935    15,ATX,1.                     . [ИПЗ+01] := mode register R
15936    15,UTM,7.                     . М15 := ИПЗ+7
15937    ,XTA,SAVACC.
15938    ,ITS,14.                      . dump the dead A ; A := М14
15939    14,VTM,13.                    . М14 := 13
15940    :14,ITS,                      . [М15++] := A ; A := М[М14]   <- LOOP over М13..М1
15941    14,UTM,-1.
15942    14,V1M,*-1.
15943    15,ATX,.                      . [ИПЗ+25] := М1
15944    13,VTM,SELECT
15945    ,UJ,CLTASK                    . close the task, then jump to SELECT
```

**Line 15931 is the point.** `SAVIND` *forges the resume PC*: it plants the `VJM` return address
into slot 03 (ИРЕТ) and mode 7 into slot 05 (СПСВ). When the scheduler later restores this ИПЗ and
executes `выпр`, the CPU pops ИРЕТ and the task **resumes at the instruction after its own
`15,VJM,SAVIND`**. A cooperative coroutine yield built entirely out of the interrupt-return
hardware — no separate mechanism at all.

Lines 15940–15942 are the **loop** counterpart of `FULSAV`'s unrolled run: `14,ITS,` takes its
register number *from М14*, so decrementing М14 from 13 to 1 walks М13→М1.

Note what `SAVIND` does *not* save: slot 00 (A) and slot 06 (И16) are never written, and slot 07
(И15) gets the stale accumulator from `,ITS,14`. Deliberate — A, М15 and М16 are dead across a
voluntary yield (М15 was already destroyed by the caller's `VJM`), and `,ITS,14` is used purely as
"discard A somewhere harmless and fetch М14".

### `BLSAVE` — hijacking the resume vector

`dubna.dd:15946-15959`. For the *involuntary* case, the kernel rewrites where a task will wake
up:

```
15946    BLSAVE:,BSS,
15947    ,ATX,ROLOUTSC
15948    15,XTA,3
15949    ,ATX,M33                      . stash the task's real ИРЕТ
15950    15,XTA,4
15951    ,ATX,M32                      . ... and ЭРЕТ
15952    15,XTA,5
15953    ,ATX,M27                      . ... and СПСВ
15954    14,VTM,TASKSAVR
15955    ,ITA,14
15956    15,ATX,3                      . [ИПЗ+03] := TASKSAVR   <- HIJACK the resume PC
15957    ,XTA,DISREG
15958    15,ATX,5                      . [ИПЗ+05] := DISREG     <- and the resume mode
15959    ,UJ,NOMATH1
```

The genuine И33/И32/И27 go aside into `M33`/`M32`/`M27`, and `TASKSAVR` is substituted as the resume
address. The task restarts inside a kernel routine of the scheduler's choosing instead of where it
was interrupted, and `TASKSAVR` puts the real values back afterwards. This is
[Memory_Mapping.md](Memory_Mapping.md)'s *"the kernel can edit СПСВ to control where it lands"*,
used in production.

### `BOCИПД` — the full restore

`dubna.dd:15876-15904`, `ПOЛHOE BOCCTAHOBЛEHИE` — "full restore", the mirror of `FULSAV`:

```
15876   C       ПOЛHOE BOCCTAHOBЛEHИE
15878    14,CALL,PUTTMP                . reload РП + РЗ from the task's tables (§10)
15879    14,VTM,-11.                   . М14 := -11 (DECIMAL)
15880    15,XTA,.                      . pop -> A := [ИПЗ+27] = И35
15881    :   ,24,2002B.ДЛЯ ПPEPЫBAH. B MAT. PEЖИME    . БлП off — "for interrupts in user mode"
15882    ,ATI,35B.PEГ.ПPEP. ПO AДP.ЧИCЛA             . М035 := break-on-operand-address
15883    :   ,24,2003B.                              . БлП back on
15884    15,XTA,.                      . pop -> A := [ИПЗ+26] = И34
15885    :   ,24,2002B.
15886    ,ATI,34B.PEГ.ПPEP. ПO AДP.KOMAHДЫ           . М034 := break-on-instruction-address
15887    :   ,24,2003B.
15888    15,XTA,.                      . pop -> A := [ИПЗ+25] = И1
15889    ,ATX,1.
15890    :14,STI,12.BOCCTAH. PEГИCTPOB.              . М[12+М14] := A ; pop   "restore registers"
15891    14,VLM,*.                                   . loop М14 = -11..0  =>  М1..М12
15892    14,VTM,RETURN                 . М14 := RETURN (the fall-through target)
15893    BOCИПД:,ENTRY,
15894    ,STX,SAVI13.BOCCTAHOBЛEHИE ИПД
15895    ,STX,SAVI14.
15896    ,STX,SAVI15.
15897    ,STX,SAVS16.
15898    ,STI,27B.                     . М027 := СПСВ
15899    ,STI,32B.                     . М032 := ЭРЕТ
15900    ,STI,33B.                     . М033 := ИРЕТ
15901    ,STX,SAVAR.
15902    ,STX,SAVSR.
15903    ,STX,SAVAC.
15904    14,UJ,                        . -> RETURN, which reloads the CPU from SMASAV
```

Lines 15890–15891 are the loop form of the register restore: `14,STI,12` computes its target as
`М[12 + М14]`, and `VLM` walks М14 from −11 to 0, restoring М1…М12 while popping the ИПЗ slots. The
stack unwinds **downward** through the ИПЗ, and every `STX`/`STI` lands on precisely the slot
`FULSAV` filled. `BOCИПД` restores the ИПЗ into `SMASAV`; `RETURN` then reloads the CPU from
`SMASAV`. The two-area design composes on the way out exactly as it did on the way in.

Note `,24,2002B`/`,24,2003B` bracketing the М034/М035 loads at 15881–15887: БлП must be **off**
while loading the address-break registers, because they match the *mapped* address.

### The whole chain

```
  hardware trap  ->  vector 0500/0501: ATX SAVAC ; UJ
                       |
                     short save into SMASAV  (A, R, Y, ГРП, М13-М16)   [§4]
                       |
                     dispatch via ГРП bit -> SWINT table                 [§5]
                       |
          +------------+------------------------------------------+
          |                                                        |
   handler finishes                                        must reschedule
          |                                                        |
   RETURN [§7]                                        NEWTA -> FULSAV [§6]
     re-poll ГРП --(pending)--> INTER                    SMASAV + live М1-М12
     Y -> A -> R                                       + М27/М32/М33 -> ИПЗ
     3,32, = выпр                                                |
          |                                                   SELECT
     [same task]                                          (pick a new task)
                                                                 |
                                                        PUTTMP [§10] — РП + РЗ
                                                                 |
                                                        М1-М12 restore loop
                                                                 |
                                                        BOCИПД — ИПЗ -> SMASAV
                                                                 |
                                                        RETURN -> выпр
                                                                 |
                                                          [the NEW task runs]
```

And the extracode path joins at `OUTMACRO`, two instructions above `RETURN` (§8).

---

## 13. The C register across a trap

The §2 layout calls slot 06 "М16, one more index register." It is not one more index register.
There are only fifteen — М1–М15. **М16 is the C register**, the address modifier, and it is the one
piece of saved context whose *value* and whose *pending state* live in two different places. Getting
it wrong corrupts an address, silently, one instruction after the return.

### What C is

`utc` (022, мода) and `wtc` (023, мод) load the modifier register C; its value is **added to the
effective address of the next instruction and then reset to 0**. Every other instruction resets it.
See [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §3 and ops 022/023 — and note §3's point
that the compiler's own idiom for a global in the middle of memory is `utc name` followed by a bare
`xta`/`atx`, so C is armed constantly in ordinary compiled code, for exactly one instruction at a
time.

In the [besm6/simh](https://github.com/besm6/simh/tree/master/BESM6/) sources C is a register-file
slot:

```c
#define MOD     020     /* модификатор адреса */          // besm6_defs.h — 020 octal = 16 decimal
```

So it is reachable by the ordinary index-register moves as register 16, which is exactly how Dubna
names it — `ITS 16`, `STI 16`, the `SAVS16` cell, the `И16` slot. That the address modifier is also
addressable as a general register is the whole reason the save/restore machinery of §6 and §7 needs
no special case for it.

### Two pieces of state, two homes

C is not just the value in `M[16]` (`M[020]`). There is also an **armed flag** — whether the *next*
instruction actually applies the modifier — and it lives in a different register:

```c
#define RUU_MOD_RK   000020  /* ПрИК - модификация регистром М[16] */
#define SPSW_MOD_RK  000020  /* ПрИК(РК) - на регистр РК принята команда,
                                которая должна быть модифицирована регистром М[16] */
```

`RUU_MOD_RK` is the live flag in РУУ while running; `SPSW_MOD_RK` is where it is parked in **СПСВ**
(`M[027]`, slot 05) across a trap. The value is in the register file; the armed-bit is in the mode
word. Two homes.

### The hardware dance

This is what SIMH does, and it is the part our own [Memory_Mapping.md](Memory_Mapping.md) `выпр`
pseudocode leaves out. At a trap (`op_int_1`):

```c
if (RUU & RUU_MOD_RK) {          // was a utc/wtc armed when the trap landed?
    M[SPSW] |= SPSW_MOD_RK;      //   remember that in СПСВ
    RUU &= ~RUU_MOD_RK;          //   and DISARM the live modifier
}
```

The armed flag migrates into СПСВ and the live modifier is **cleared** — so the handler runs
disarmed. That is what makes the vector safe: the very first half-instruction is `atx SAVAC` (§3),
and if C were still armed it would be silently modified. The **value in `M[16]` is left
untouched**, sitting inert in the register file.

On the way out, `выпр` puts it back (case 0320):

```c
if (M[SPSW] & SPSW_MOD_RK)
    next_mod = M[MOD];           // re-arm from M[16] for the resumed instruction
```

And the arm/disarm itself, at the end of every instruction:

```c
if (next_mod) { M[MOD] = next_mod; RUU |= RUU_MOD_RK; }
else            RUU &= ~RUU_MOD_RK;
```

Put together: **a `utc` interrupted before its target executes is preserved across the whole trap.**
Its value rides in `M[16]`, its armed-bit rides in СПСВ, and `выпр` reconstructs the pending
modification from the two. The earlier corner of this document that said the hardware does not try
to reconstruct a mid-`utc` interrupt was wrong; it reconstructs it precisely, and for free.

### How Dubna saves the value

Exactly as it saves any other register — because it *is* addressable as one. The five load-bearing
sites, all verified:

```
16524    ,ITS,16                 . EXTINTER prologue: push A ; A := М16 (the interrupted C)
16525    ,ATX,SAVS16             .   SAVS16 := М16
16560    ,ITS,16                 . INTINTER prologue: the identical pair
16561    ,ATX,SAVS16
15484    ,XTS,SAVS16             . FULSAV: push SAVS16 -> ИПЗ slot 06 = И16
15574    ,XTA,SAVS16             . RETURN: A := SAVS16
15575    ,STI,16                 .   М16 := A ; pop
15897    ,STX,SAVS16             . BOCИПД: pop -> SAVS16, on the way back into RETURN
```

`SAVS16` is the last cell of the `SMASAV` block (`dubna.dd:16413-16414`) and carries the alias
`SAVI16:,EQU,SAVS16` (`dubna.dd:51583`), which is where the `И16` naming comes from. It is slot 06 of
the ИПЗ (§2), and it is one of the М13–М16 group the short prologue already saves (§4).

**The armed flag needs no separate handling.** It is a bit of СПСВ, which `FULSAV` saves to slot 05
and `BOCИПД` restores, right alongside the mode word documented in §6, §7 and §12. Dubna never
mentions C-preservation because there is nothing to mention: save the full register file, save СПСВ,
and a pending `utc` comes back on its own. This is the single cleanest illustration of why the
two-tier save has to be *complete* — the correctness of an unrelated user instruction depends on a
bit of СПСВ and a word of the register file that the kernel had no obvious reason to care about.

### Two things to keep straight

**Saving C-as-context is not the same as using C-as-scratch.** Having spilled the interrupted C into
`SAVS16`, the handler is free to reuse the *live* modifier — which is now disarmed and inert — as an
ordinary address-modification register, and Dubna does so constantly:

```
16574    ,WTC,SAVI15             . arm C from the saved М15 cell...
16575    15,VTM,                 .   ...consume it: М15 := 0 + C  (reload М15)
16576    ,WTC,RETTRA             . arm C from RETTRA...
16577    ,UJ,                    .   ...consume it: computed jump to 0 + C
15528    ,WTC,ГYC                . arm C from the current-task ИПЗ pointer...
15529    15,XTA,TMATH            .   ...index the page table: TMATH + М15 + C
15929    ,WTC,Г Y C              . SAVIND: same idiom to reach the ИПЗ base
```

and the `13,MTJ,16` + `CALL SAVEMOD` return-link idiom (`dubna.dd:20037-20040` and a dozen more)
parks a subroutine link in the C slot across a nested call. None of these touch the *saved* value in
`SAVS16`; they borrow the physical register because the trap left it inert.

**The restore order proves C is a plain slot.** `RETURN` restores C **first** (`STI 16` at 15575)
and *then* М13, М14, М15 through the following `STI`s (§7). If `STI 16` armed the modifier, the next
`STI 13` would target register `13 + C` — corruption. It does not: only `utc`/`wtc` arm, only `выпр`
re-establishes the resumed program's armed state, and every `ITS`/`STI`/`ATI` in between is a plain
register move. That is the same fact from the other side — C is saved and restored as a value, and
the *pending* semantics are carried entirely by СПСВ's `SPSW_MOD_RK`.

---

## 14. What this means for the Unix port

### The finding: we are not saving R or Y

[`kernel/besm6.S:205-238`](../kernel/besm6.S#L205-L238) — the `extint` stub — saves the accumulator
and r8–r14. It does **not** save the mode register R, and it does **not** save Y. Dubna saves both
on every single interrupt (`RTE`/`XTR` for R, `YTA` + the `AEX` side-effect for Y), and it is not
being fussy:

- **The C ABI clobbers R.** [Besm6_Runtime_Library.md:86](Besm6_Runtime_Library.md#L86) states the
  contract outright: *"enter with `NTR 3` / ω unknown, exit with `NTR 3` / ω = logical."* So the
  `13 vjm extintr` at `besm6.S:221` returns with R changed, and the interrupted user code resumes
  with the wrong ω-mode and the wrong normalize/round suppression. Its next floating-point
  instruction does something quietly different.
- **Any logical op clobbers Y.** `и`, `слц`, `сл` and friends all write it. The interrupted code
  may hold a live Y — mid-multiply, or between a `счмр` and its use — and `extintr()` will
  destroy it.

The fix mirrors §4 and §7, and the **order in the epilogue is not negotiable** (Y → A → R, for the
reasons in §7):

```
extint: atx     sa                  // A first, as the vector does
        rte     07777               // R -> A
        atx     sr
        yta                         // Y -> A
        atx     srmr
        ...                         // r8-r14 as today
        13 vjm  extintr
        ...                         // r8-r14 as today
        xta     srmr                // Y back, via the AEX side effect
        aex                         //   (A is garbage after this — intentional)
        xta     sa                  // A back (xta does not disturb Y)
        xtr     sr                  // R back — must be last
        3 ij
```

Per the decision on this task, **this document only records the gap** — `besm6.S` is unchanged. It
belongs with task 15 in [`kernel/TODO.md`](../kernel/TODO.md).

### The C register is a third gap

The same stub does not save `M[16]` either, and by §13 that is the C register. The race is narrow
but real. A device interrupt can land in the one-instruction window between a user `utc` and the
instruction it modifies. The trap parks `SPSW_MOD_RK` in СПСВ and leaves the value in `M[16]`, both
of which `extint` preserves *by accident* — it never touches СПСВ, and it does not read `M[16]`. But
`extintr()` is C, and the compiler's idiom for a global is `utc name` + bare load
([Besm6_Instruction_Set.md:144-149](Besm6_Instruction_Set.md#L144-L149)), so the handler **overwrites
`M[16]`**. The closing `3 ij` then re-arms from the clobbered value (§13), and the resumed user
instruction is modified by the wrong address. Nothing faults; a load just reads the wrong word.

The fix is the §13 idiom — read register 020 into a save cell on entry, alongside A and r8–r14, and
put it back with `ati 020` before `3 ij` (a plain move, which does not arm — §13):

```
extint: atx     sa
        ...                         // R, Y as above; r8-r14 as today
        ita     020                 // C register -> A
        atx     sc
        13 vjm  extintr
        xta     sc                  // C register back (ati does not arm the modifier)
        ati     020
        ...                         // Y, A, R restore as above
        3 ij
```

Recorded, not applied. The positive corollary is the §13 point restated for our side: once the trap
frame and `save()`/`resume()` (tasks 15/16) save the **full** register file *and* СПСВ, a pending
`utc` is preserved with no code that mentions it — exactly as Dubna's `FULSAV`/`BOCИПД` get it. The
gap exists only in `extint`, which saves a *subset* of the registers.

### The stack is a fourth gap

The three above are registers the stub forgets to *save*. This one is a stack the stub forgets to
*switch*, and it is the one Dubna does not let us copy verbatim — because the two kernels place the
supervisor stack differently.

The PDP-11 v7 that Unix came from switched to the kernel stack for free: SP is banked by processor
mode, so a trap from user mode lands with SP already pointing at the per-process kernel stack. **The
BESM-6 has one stack register, М15, shared across modes.** Dubna solves this in the §4 prologue — `ITA
15` / `15,VTM,SAVI15` saves the interrupted М15 and repoints it at `SMASAV`, so the handler runs on a
supervisor scratch, not on whatever the interrupted code was using. `extint` does neither: it never
touches r15 and relies on the note at [`besm6.S:183-186`](../kernel/besm6.S#L183) that "extintr()
preserves r1–r7 and r15 for us (that is the ABI)."

That reliance holds only when r15 *already* names the kernel stack — i.e. when the interrupt nested
inside the kernel (case 3 of the four-case analysis). When the interrupt lands in **user** code (case
2), r15 is the user stack pointer (`exec` seeds it at `070000`, growing up), and the trap has just
forced БлП **on**, so supervisor data is unmapped: that r15 is now a **physical** word index at
≈ `070000`, *inside the kernel image below the u-area at `076000`*. `extintr()` is C; its prologue
decrements r15 and writes its frame there — silent corruption of the kernel's own text/data, invisible
until the timer or a device ISR is actually armed.

Unlike Dubna, we do not repoint at a global scratch — we have a real per-process kernel stack, the one
`_start` seeds — so the fix is to reload *that*, and only from user mode. The signal is СПСВ: the trap
stores `(old ПСВ БлП/БлЗ/БлПр) | IS_SUPERVISOR(RUU)` there (Memory_Mapping.md, "Entering and leaving
supervisor mode"), so `СПСВ & 014` (РежЭ | РежПр) is zero **iff** the interrupted context was user.
Test the supervisor bits, **not** БлП: `copyin`/`copyout` clear БлП while staying in supervisor mode,
so a БлП test would misclassify a fault taken mid-`copyin` and reset r15 out from under the syscall.

The corrected `extint`, folding in all four gaps (R, Y, M[16], and the stack) — the register save/
restore order is the §7 rule, the r15 and C-register restores go before the final `xta sa` because they
clobber A, and the two `выпр`-doesn't-reload registers (r8–r14, r15) are put back by hand:

```
extint: atx     sa                  // A first, as the vector does
        rte     07777               // R   -> A
        atx     sr
        yta                         // Y -> A
        atx     srmr
        ita     020                 // C register (M[16]) -> A
        atx     sc
        ita     017                 // interrupted r15 -> A
        atx     s15                 //   one static cell: interrupts are blocked, no re-entry
        ita     027                 // СПСВ -> A
        aax   #(014)                // isolate РежЭ | РежПр (RUU_EXTRACODE|RUU_INTERRUPT)
        u1a     extk                // nonzero -> nested in the kernel: keep r15
     15 vtm     [ustkbase]          // zero -> from user: r15 := kernel stack base (~076214)
extk:   ita     010                 // r8-r14 as today
        atx     s8
        ...                         // r9..r13 unchanged
        ita     016
        atx     s14

     13 vjm     extintr             // intr.c: read ГРП, dispatch, dismiss

        xta     s14                 // r8-r14 back, as today
        ati     016
        ...                         // r13..r9 unchanged
        xta     s8
        ati     010
        xta     s15                 // interrupted r15 back (before the A restore)
        ati     017
        xta     sc                  // C register back (ati does not arm the modifier)
        ati     020
        xta     srmr                // Y back, via the aex side effect
        aex                         //   (A is garbage after this -- intentional)
        xta     sa                  // A back (xta does not disturb Y)
        xtr     sr                  // R back -- must be last
      3 ij                          // выпр: restore the mode word, jump via M[033]
```

Recorded, not applied, like the three above; it belongs with task 15.

### What Dubna confirms we already have right

- **`sureg()`.** `PUTTMP` (§10) reloads the whole address space from an in-memory shadow in twelve
  `рег`s, for the same reason ours does: РП/РЗ are write-only, so the shadow is the only copy. Same
  design, arrived at independently.
- **`brz.s` and the nine stores.** §11. An authentic kernel drains with exactly nine consecutive
  stores to physical 1, before rewriting РП. The rule holds, and the insistence on *consecutive* is
  not over-caution.
- **`.org 0500`.** Dubna had no origin directive and counted words by hand, checking itself with
  `*NNN:` labels. Our assembler enforces it. Nothing to change.

### What to steal

- **`ITS`/`STI` are the register-save idiom** (§1, §6). One instruction per register, no scratch
  cell, the accumulator as the pipeline register. This is what `save()`/`resume()` should be built
  from — not a loop, and certainly not `vtm`+`atx` pairs.
- **`OUTMACRO`'s two-instruction door-merge** (§8) applies directly to task 15's shared exit. Copy
  ЭРЕТ into ИРЕТ and let one `выпр` serve both the syscall gate and the interrupt gate, rather than
  branching on the door. The bonus — the syscall return inheriting the interrupt epilogue's pending
  check — is worth having on its own.
- **The two-tier save** (§4, §6). Most interrupts never park a task, so the prologue saves the
  minimum and the full 24-word context is materialised only when the scheduler actually needs it.
  Our `extint` already has this shape; it just needs R, Y and the C register (§13) added to the
  short tier.
- **The internal/external clear asymmetry** (§5). Faults are not queued — clear them all; device
  interrupts are — clear one.

### The correspondences

| Dubna | Ours |
|---|---|
| ИПЗ, the per-task block | the u-area, [`kernel/TODO.md`](../kernel/TODO.md) "The u-area invariant" |
| `SAVIND` (§12) | `save()` — [`besm6.S:268`](../kernel/besm6.S#L268), task 16 |
| `BOCИПД` + `PUTTMP` (§10, §12) | `resume()` — [`besm6.S:276`](../kernel/besm6.S#L276), task 16 |
| `PUTTMP` | `sureg()`, [`kernel/utab.c`](../kernel/utab.c) |
| the nine-store drain (§11) | `drainbrz()`, [`kernel/brz.s`](../kernel/brz.s) |
| `RETURN`/`OUTMACRO` (§7, §8) | the trap gate, task 15 |
| `SAVS16` / `И16`, the C register (§13) | an `M[16]` slot in the trap frame and the new `reg.h` |
| `SMASAV` | the trap frame on the kernel stack at `076000` |

**One difference that matters.** Each Dubna task has its **own** ИПЗ page, separately allocated
(`dubna.dd:10500`, `OCBOБOЖДEHИE ЛИCTA ИПЗ` — "freeing the ИПЗ page"), and a context switch just
repoints `ГYC` at the incoming task's block — nothing is copied. The supervisor reaches it through
the physical pointer in `ГYC`, unmapped (at a trap the hardware forces БлП on, so supervisor data is
physical — [Memory_Mapping.md](Memory_Mapping.md)). Ours is *copied*: the u-area is a **single**
fixed physical page at `076000`, shared across tasks, so `resume()` must `uflush()` the old and
`uload()` the new. That is the price of a one-page u-area and it is the one we chose to pay
([`kernel/TODO.md`](../kernel/TODO.md), "Known consequences, accepted"). So `SAVIND` is a closer
model for `save()` than `BOCИПД` is for `resume()` — the register half transfers directly, the
u-area half does not.
