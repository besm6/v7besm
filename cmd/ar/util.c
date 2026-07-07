//
// BESM-6 archiver: exit path, diagnostics, and small shared helpers.
//
#include <stdio.h>
#include <unistd.h>

#include "intern.h"

// Print the usage summary and exit with an error.
void die_usage(void)
{
    printf("Usage:\n");
    printf("    ar [-]{%s}[%s] archive file...\n", command_letters, option_letters);
    printf("Commands (exactly one required):\n");
    printf("    r           Replace or add files to the archive\n");
    printf("    d           Delete files from the archive\n");
    printf("    x           Extract files from the archive\n");
    printf("    t           List a table of contents\n");
    printf("    p           Print members to standard output\n");
    printf("    m           Move files within the archive\n");
    printf("    q           Quick-append files without checking for duplicates\n");
    printf("Options:\n");
    printf("    u           With r, replace a member only if the file is newer\n");
    printf("    v           Verbose: trace each action (repeat to trace unchanged members)\n");
    printf("    a           Position insert/move after the named member\n");
    printf("    b           Position insert/move before the named member\n");
    printf("    i           Synonym for b\n");
    printf("    c           Suppress the \"creating archive\" notice\n");
    printf("    l           Keep temporary files in the current directory, not /tmp\n");
    printf("    n           Accepted for compatibility, ignored\n");
    printf("    archive     The archive file to create or modify\n");
    printf("    file...     Files/members to operate on\n");
    finish(1);
}

// Complain that the archive doesn't exist and exit.
void die_no_archive(void)
{
    fprintf(stderr, "ar: %s not found\n", ar.archive_name);
    finish(1);
}

// Signal handler: an interrupt aborts the run (finish deletes temp files).
void on_signal(int sig)
{
    (void)sig;
    finish(100);
}

// The single exit point for the whole engine.
//
// Deletes any temp files that were created, stores the exit code, and jumps
// back to the setjmp() in ar_run(). Using longjmp instead of exit() lets the
// engine be invoked over and over in one test process without leaking temp
// files or state.
void finish(int c)
{
    if (ar.tmp_name)
        unlink(ar.tmp_name);
    if (ar.tmp1_name)
        unlink(ar.tmp1_name);
    if (ar.tmp2_name)
        unlink(ar.tmp2_name);
    ar.exit_code = c;
    longjmp(ar.done_env, 1);
}

// Report every named file that was never matched, and count them.
//
// The command loops null out ar.names[i] as each file is handled, so anything
// still non-NULL at the end was requested but not found. The count becomes the
// program's exit status.
int report_missing(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namecount; i++)
        if (ar.names[i]) {
            fprintf(stderr, "ar: %s not found\n", ar.names[i]);
            n++;
        }
    return (n);
}

// Count how many named files are still unhandled (ar.names[i] != NULL).
//
// Used by 'x' to know when every requested file has been extracted so it can
// stop scanning early.
int count_pending(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namecount; i++)
        if (ar.names[i])
            n++;
    return (n);
}

// Print a one-line "action - name" trace when -v is in effect.
//
// 'c' means "copied unchanged" and is only shown at double verbosity (-vv), to
// avoid drowning the output in noise for a large archive.
void log_action(int c)
{
    if (ar.opt_verbose)
        if (c != 'c' || ar.opt_verbose > 1)
            printf("%c - %s\n", c, ar.cur_file);
}

// Return the bare file name from a path: strip trailing slashes, then return
// everything after the last remaining slash.
//
// Two passes: the first walks back from the end turning trailing '/' into NUL
// (so "dir/" behaves like "dir"); the second scans forward remembering the
// character after the last '/'. Members are always stored under this short name.
char *basename_of(char *s)
{
    char *p1, *p2;

    for (p1 = s; *p1; p1++) // find end of string
        ;
    while (p1 > s) { // trim trailing slashes
        if (*--p1 != '/')
            break;
        *p1 = 0;
    }
    p2 = s;
    for (p1 = s; *p1; p1++) // remember char after the last slash
        if (*p1 == '/')
            p2 = p1 + 1;
    return (p2);
}

// Report a write failure (disk full, etc.) and exit.
void die_write_error(void)
{
    perror("ar write error");
    finish(1);
}
