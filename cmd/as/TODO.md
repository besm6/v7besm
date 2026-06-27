Tasks to do in assembler:

 * The operator-bearing mnemonics (a+x, a-x, x-a, a/x, a*x, e+x, e-x, e+n, e-n, j+m) cannot be matched by the current name lexer (+ - * / are not name characters in cmd/as/lex.c). They are stored verbatim per the chosen mnemonics; making the lexer accept them (e.g. the comma-delimited ,a+x, Madlen syntax shown in doc/Besm6_Calling_Conventions.md) is a separate task.

 * pass1.c/pass2.c long-address field width is still 20-bit (0xfffff); BESM-6 long is 15-bit. With val<<15, addresses ≥ 2^15 or negative collide with the opcode field (bits 16-20). The ubiquitous `vtm -1` currently mis-encodes as vlm. Tighten the long-address masks to 15 bits (077777) in pass1.c makecmd(), pass2.c adjust() (RLONG/RSHIFT/RTRUNC cases), and cmd/ld/ld.c relhalf().

 * The raw-opcode escapes in pass1.c still use micro-BESM bit positions. LSCMD ($NN, short) emits `cval<<12 | 0x3f00000` — the 0x3f00000 prefix must be dropped so a short opcode lands at bits 13-18. LLCMD (@NN, long) emits `cval<<20` — must become `cval<<15` so the long opcode lands at bits 16-20.

 * Removing the dead TLIT/TINT/TCOMP/MAKECOMP machinery (the literal-pool `#` path and the 0x4000000 literal bit in pass1.c makecmd(), plus the MAKECOMP macro and component-instruction handling). No BESM-6 instruction uses these flags.

 * Command-line option parsing is broken in as.c main(): the inner loop `for (cp = argv[i]; ...)` starts at the leading '-', so every flag (including -o) falls into the "Unknown option" default. Start the scan at argv[i]+1.

 * makeheader() in pass2.c does not initialize hdr.a_abss, so the abss size field is written as garbage. Set it (currently always 0) along with the other a_* fields.
