//
// Assembler for BESM-6.
// Engine: global state and pass sequencing.  The command-line front end lives
// in main.c; fatal() is supplied by whoever links this library (main.c for the
// CLI, the test harness for unit tests).
//
// HOW IT WORKS
// ------------
// The assembler turns one source file into one relocatable object file (the
// BESM-6 a.out format described in cross/besm6/b.out.h).  It runs in two
// passes, which is the classic way to handle forward references (a "jump
// ahead" to a label that has not been seen yet):
//
//   Pass 1 (generate_code): read the source line by line and translate it into
//   machine words, but with addresses still expressed symbolically.  Output is
//   accumulated into several SEGMENTS, each stored in a pair of temporary
//   files:
//       sfile[seg]  - the segment's actual code/data image
//       rfile[seg]  - a parallel stream of relocation half-words, one per
//                     image half-word, recording "what does this address
//                     refer to" so pass 2 can fix it up.
//   The segments are: const (a de-duplicated pool of literal values), text
//   (code), data, strng (string constants), and bss (reserved space).  As
//   names are seen they are entered into the symbol table.
//
//   A BESM-6 word is 48 bits, handled here as two 24-bit HALF-WORDS, because a
//   host `long` cannot be relied on to hold 48 bits.  Two instructions pack
//   into one word, so the engine works in half-words throughout.
//
//   Between the passes (finalize_symtab) the segment sizes are final, so the
//   symbol table can be sized; pass 2 then assigns each segment its base
//   address.
//
//   Pass 2 (emit_segments + friends): now every address is known, so re-read
//   the temp files, add the segment bases to every reference (relocation), and
//   write the final object file: header, segment images, relocation records,
//   and the symbol table.
//
// The whole pipeline, driven by assemble() below, is:
//   open_temp_files -> init_hash_tables -> generate_code -> finalize_symtab ->
//   write_header -> emit_segments -> write_reloc -> write_symtab
//
#include "as.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// The single instance of the assembler's state.  Only the two fields with
// non-zero defaults are set here; assemble() zeroes the rest at the start of
// each run.
struct assembler as = {
    .outfile   = "a.out",
    .tfilename = "/tmp/asXXXXXX",
};

//
// Format the current source-location prefix into buf, e.g. "hello.c:42: ".  If
// a cc-style line marker has been seen its file:line is used; otherwise we fall
// back to the input file name and physical line number.  Shared by the CLI's
// fatal() (main.c) and the unit-test harness so both render locations the same
// way.  Returns buf.
//
char *format_location(char *buf, int size)
{
    if (as.srcfile[0])
        snprintf(buf, size, "%s:%d: ", as.srcfile, as.srcline);
    else if (as.infile && as.line)
        snprintf(buf, size, "%s:%d: ", as.infile, as.line);
    else if (as.infile)
        snprintf(buf, size, "%s: ", as.infile);
    else if (as.line)
        snprintf(buf, size, "%d: ", as.line);
    else
        buf[0] = 0;
    return buf;
}

//
// Open the temporary files that hold the segment images and their relocation
// streams during pass 1.  Each segment needs two files (code image + matching
// relocations).  The files are created with mkstemp() and then immediately
// unlink()ed: they have no name on disk, so they disappear automatically when
// the process exits, yet stay usable through the open FILE* handles.
//
static void open_temp_files(void)
{
    int i;

    int fd = mkstemp(as.tfilename);
    if (fd == -1) {
        fatal("cannot create temporary file %s", as.tfilename);
    } else {
        close(fd);
    }
    for (i = STEXT; i < SBSS; i++) {
        if (!(as.sfile[i] = fopen(as.tfilename, "w+")))
            fatal("cannot open %s", as.tfilename);
        unlink(as.tfilename);
        if (!(as.rfile[i] = fopen(as.tfilename, "w+")))
            fatal("cannot open %s", as.tfilename);
        unlink(as.tfilename);
    }
    as.line = 1;
}

//
// Public entry point: assemble one file with the given options and return 0.
// The passes communicate through stdin/stdout (the input source and the output
// object), so this function redirects those streams and carefully restores
// them on exit - that matters because the unit-test process calls assemble()
// many times and must get its own stdin/stdout back each time.  For the same
// reason the whole global state is zeroed at the top, making the engine safely
// re-runnable in a single process.
//
int assemble(const struct assembler_args *args)
{
    int i;
    int saved_in, saved_out;

    // Reset the global state so the engine can be invoked repeatedly in one
    // process (the unit tests assemble many sources in a row; the CLI front end
    // calls this just once).  Everything else is rebuilt by open_temp_files()/init_hash_tables()/
    // the passes; only the two static defaults need restoring afterwards.
    memset(&as, 0, sizeof as);
    strcpy(as.tfilename, "/tmp/asXXXXXX");

    // Copy options into the global state.
    as.infile  = args->infile;
    as.outfile = args->outfile ? args->outfile : (char *)"a.out";
    as.debug   = args->debug;
    as.xflags  = args->xflags;
    as.Xflag   = args->Xflag;
    as.uflag   = args->uflag;
    as.aflag   = args->aflag;

    // Save the original stdin/stdout so they can be restored when done; the
    // passes read from stdin and write to stdout, so we redirect them here but
    // must not leave them redirected for a caller (e.g. a test process).
    saved_in  = dup(fileno(stdin));
    saved_out = dup(fileno(stdout));

    // set up input/output
    if (as.infile && !freopen(as.infile, "r", stdin))
        fatal("cannot open %s", as.infile);
    if (!freopen(as.outfile, "w", stdout))
        fatal("cannot open %s", as.outfile);

    // A '#' on the very first line is a whole-line comment / cc-style line
    // marker.  There is no preceding newline to trigger the lexer's line-start
    // handling, so parse it here (recording the source file/line if it is a
    // "# N \"file\"" marker) and skip the rest of the line, leaving the '\n'
    // for the lexer (which counts lines and handles any following line-start
    // '#').  Otherwise push the character back unchanged.
    i = getchar();
    if (i == '#') {
        parse_line_marker();
        while ((i = getchar()) != '\n' && i != EOF)
            ;
    }
    ungetc(i, stdin);

    open_temp_files();  // open the per-segment temp files
    init_hash_tables(); // build the instruction/symbol/constant hash tables
    generate_code();    // pass 1: parse source into the segment temp files
    finalize_symtab();  // align segments and size the symbol table
    write_header();     // write the a.out header
    emit_segments();    // pass 2: relocate and write const + code segments
    write_reloc();      // write the relocation records
    write_symtab();     // write the symbol table

    // Restore the original stdin/stdout.
    fflush(stdout);
    if (saved_in >= 0) {
        dup2(saved_in, fileno(stdin));
        close(saved_in);
        clearerr(stdin);
    }
    if (saved_out >= 0) {
        dup2(saved_out, fileno(stdout));
        close(saved_out);
        clearerr(stdout);
    }
    return 0;
}
