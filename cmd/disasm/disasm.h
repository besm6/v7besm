/*
 * Public interface of the BESM-6 disassembler engine (cmd/disasm/dis.c).
 *
 * Split into a reusable library so it can be linked into both the command-line
 * tool (main.c) and the unit tests.  All output goes to stdout.
 */
#ifndef BESM6_DISASM_H
#define BESM6_DISASM_H

/*
 * Mnemonic tables: two dialects (MADLEN, the ASCII default, and BEMSH, the
 * Cyrillic set).  lcmd/scmd point at the dialect currently selected.
 */
extern const char *lcmd_madlen[16], *lcmd_bemsh[16];
extern const char *scmd_madlen[64], *scmd_bemsh[64];
extern const char **lcmd, **scmd;

/* Output-control flags consumed by the file driver. */
extern int rflag, Rflag, cflag, Cflag;

/* Decode one 24-bit instruction into re-assemblable text written to buf. */
void disasm_insn(unsigned insn, char *buf);

/* Disassemble a whole a.out object file to stdout.  Returns 0 on success. */
int disassemble(const char *fname);

#endif /* BESM6_DISASM_H */
