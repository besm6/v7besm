//
// ar — create and maintain BESM-6 object-code libraries (archive files).
//
// An archive packs many files into one, most often the object files that make
// up a link library for the BESM-6 linker. This tool adds, replaces, deletes,
// lists, extracts, and reorders those members.
//
//     ar [mrxtdpq][uvnbail] archive_name files ...
//
// Command letter (choose exactly one): r=replace/add  d=delete  x=extract
//     t=list contents  p=print to stdout  m=move  q=quick-append.
// Option letters: u=only if newer  v=verbose  a/b/i=insert after/before a named
//     member  l=keep temp files in the current directory  (n is accepted, ignored).
//
// WHAT AN ARCHIVE IS
// ------------------
// An "ar" archive is just several files glued together into one, the way a
// library (.a) bundles many object files. The layout on disk is:
//
//     [ magic number ARMAG ]          one BESM-6 word, marks the file as an archive
//     [ member 1 header ][ data ]     header describes the file, data is its bytes
//     [ member 2 header ][ data ]
//     ...
//
// Each "member" is one stored file. Its header (struct ar_hdr) records the
// name, modification date, owner, permission bits, and byte size. The raw file
// bytes follow, zero-padded up to a whole BESM-6 word (W = 6 bytes) so the next
// header always starts on a word boundary.
//
// HOW THE COMMANDS WORK
// ---------------------
// The letter commands (r d x t p m q) never edit the archive in place. To add,
// delete, or move members we STREAM the old archive member-by-member into a
// fresh temporary file, making the change as we go, and finally copy the temp
// back over the original (commit_archive). The one exception is q ("quick"),
// which just appends to the archive directly.
//
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "intern.h"

// The command letters (exactly one required) and the option letters.
char *command_letters = "mrxtdpq"; // one of these selects what ar does
char *option_letters  = "uvnbail"; // modifiers; shown in the usage message

// Signals we trap so a half-written archive/temp file gets cleaned up on Ctrl-C.
int caught_signals[] = { SIGHUP, SIGINT, SIGQUIT, 0 }; // 0-terminated list

struct arstate ar; // the one and only instance of the engine's state

static void set_command(void (*fun)(void)); // file-local; defined below

// Wipe all engine state back to zero before a run.
//
// ar_run() may be called many times inside one process (the unit tests do
// this), so every field must be reset or one run would leak into the next.
// A single memset clears the whole struct; then we re-seed the three temp-file
// name templates. The trailing "XXXXXX" is where mkstemp() later writes a
// unique suffix.
static void reset_state(void)
{
    memset(&ar, 0, sizeof(ar));
    strcpy(ar.tmpl_main, "/tmp/ar0XXXXXX");
    strcpy(ar.tmpl_before, "/tmp/ar1XXXXXX");
    strcpy(ar.tmpl_move, "/tmp/ar2XXXXXX");
}

// Entry point of the archiver engine: parse argv and run the chosen command.
//
// argv[1] is the bunched flag string (e.g. "rv"), argv[2] is the archive name,
// and argv[3..] are the file names to operate on. Returns the process exit
// code. Instead of calling exit(), the code below jumps to finish(), which
// longjmp()s back to the setjmp() here — that is how a fatal error unwinds
// cleanly and how the function can be run repeatedly in tests.
int ar_run(int argc, char **argv)
{
    int i;
    char *cp;

    reset_state();
    ar.exit_code = 0;
    if (setjmp(ar.done_env)) // finish() lands here; ar.exit_code holds the result
        return ar.exit_code;

    // Trap interrupt signals so a partial temp file is removed on the way out,
    // but only if the signal wasn't already being ignored (e.g. by a shell).
    for (i = 0; caught_signals[i]; i++)
        if (signal(caught_signals[i], SIG_IGN) != SIG_IGN)
            signal(caught_signals[i], on_signal);
    if (argc < 3)
        die_usage();

    // Walk the flag string one character at a time. Option letters just set a
    // flag; command letters (r d x t p m q) select the single command to run.
    for (cp = argv[1]; *cp; cp++)
        switch (*cp) {
        case 'u':
            ar.opt_update++;
            continue;

        case 'v':
            ar.opt_verbose++;
            continue;

        case 'l':
            ar.opt_local++;
            continue;

        case 'a':
            ar.opt_after++;
            continue;

        case 'b':
            ar.opt_before++;
            continue;

        case 'i':
            ar.opt_insert++;
            continue;

        case 'c':
            ar.opt_create++;
            continue;

        case 'n':
            // accepted for compatibility, ignored
            continue;

        case 'r':
            set_command(cmd_replace);
            continue;

        case 'd':
            set_command(cmd_delete);
            continue;

        case 'x':
            set_command(cmd_extract);
            continue;

        case 't':
            set_command(cmd_list);
            continue;

        case 'p':
            set_command(cmd_print);
            continue;

        case 'm':
            set_command(cmd_move);
            continue;

        case 'q':
            set_command(cmd_quick);
            continue;

        default:
            fprintf(stderr, "ar: unknown flag `%c'\n", *cp);
            finish(1);
        }

    // With -l, keep the temp files in the current directory instead of /tmp.
    if (ar.opt_local) {
        strcpy(ar.tmpl_main, "ar0XXXXXX");
        strcpy(ar.tmpl_before, "ar1XXXXXX");
        strcpy(ar.tmpl_move, "ar2XXXXXX");
    }
    if (ar.opt_insert) // -i is just another spelling of -b
        ar.opt_before++;

    // -a/-b/-i insert relative to a named member. That member's name is an
    // EXTRA argument sitting in argv[2], so grab it and shift argv left by one
    // so the archive name and file list line up the same as the normal case.
    if (ar.opt_after || ar.opt_before) {
        ar.insert_state  = 1;
        ar.position_name = basename_of(argv[2]);
        argv++;
        argc--;
        if (argc < 3)
            die_usage();
    }
    ar.archive_name = argv[2];
    ar.names        = argv + 3;
    ar.namecount    = argc - 3;

    // No command letter given. Plain "u" alone is treated as replace; otherwise
    // it is an error — the user must pick exactly one command.
    if (ar.command == 0) {
        if (ar.opt_update == 0) {
            fprintf(stderr, "ar: must be one of [%s]\n", command_letters);
            finish(1);
        }
        set_command(cmd_replace);
    }
    (*ar.command)();          // run it
    finish(report_missing()); // exit; status = count of named files not found
    return ar.exit_code;
}

// Record the one command to run, rejecting a second command letter.
//
// Only a single command (r/d/x/t/p/m/q) is allowed per invocation, so if one
// was already chosen this is a fatal usage error.
static void set_command(void (*fun)(void))
{
    if (ar.command != 0) {
        fprintf(stderr, "ar: only one of [%s] allowed\n", command_letters);
        finish(1);
    }
    ar.command = fun;
}
