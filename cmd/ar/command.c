//
// BESM-6 archiver: the seven command handlers (r d x t p m q).
//
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "intern.h"

// Command 'r' — replace (or add) members.
//
// Streams the existing archive into a new temp file. For each old member: if it
// matches one of the named files, write the fresh copy from disk instead (or,
// with -u, only if the disk copy is newer); otherwise copy the old member
// through unchanged. Named files that weren't already in the archive are then
// appended by append_new_files(), which also commits the temp over the archive.
void cmd_replace(void)
{
    int f;

    start_temp_archive();
    open_archive();
    while (!next_member()) {
        handle_position(); // honor -a/-b insertion point, if any
        if (ar.namecount == 0 || match_member()) {
            f = open_input(); // the newer on-disk version of this member
            if (f < 0) {
                if (ar.namecount)
                    fprintf(stderr, "%s: error: cannot open %s\n", ar.progname, ar.cur_file);
                goto cp; // can't read it: keep the old member instead
            }
            if (ar.opt_update)
                if (ar.filestat.st_mtime <= ar.hdr.ar_date) {
                    close(f);
                    goto cp; // -u and not newer: keep the old member
                }
            log_action('r');
            copy_member(ar.arfd, -1, IODD + SKIP); // skip past the old member
            write_member(f);                       // and emit the new one
            continue;
        }
    cp:
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD); // copy old member as-is
    }
    append_new_files();
}

// Command 'd' — delete named members.
//
// Copies every member into the temp file EXCEPT those named on the command
// line, then commits the temp over the archive.
void cmd_delete(void)
{
    start_temp_archive();
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (match_member()) {
            log_action('d');
            copy_member(ar.arfd, -1, IODD + SKIP); // drop this member
            continue;
        }
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD); // keep this member
    }
    commit_archive();
}

// Command 'x' — extract members to individual files on disk.
//
// For each member that is named (or all members if no names were given),
// create a real file with the stored permission bits and write the member's
// bytes into it. The archive itself is left untouched. Stops early once every
// named file has been extracted.
void cmd_extract(void)
{
    int f;

    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            f = creat(ar.cur_file, ar.hdr.ar_mode & 0777);
            if (f < 0) {
                fprintf(stderr, "%s: error: cannot create %s\n", ar.progname, ar.cur_file);
                goto sk;
            }
            log_action('x');
            copy_member(ar.arfd, f, IODD); // member bytes -> the new file
            close(f);
            continue;
        }
    sk:
        log_action('c');
        copy_member(ar.arfd, -1, IODD + SKIP); // not wanted: skip over it
        if (ar.namecount > 0 && !count_pending())
            finish(0); // all requested files done, stop scanning
    }
}

// Command 'p' — print members' contents to standard output.
//
// With -v, precedes each member with a "<name>" banner. Nothing is modified.
void cmd_print(void)
{
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            if (ar.opt_verbose) {
                printf("\n<%s>\n\n", ar.cur_file);
                fflush(stdout);
            }
            copy_member(ar.arfd, 1, IODD); // fd 1 == stdout
            continue;
        }
        copy_member(ar.arfd, -1, IODD + SKIP);
    }
}

// Command 'm' — move named members to a new position.
//
// Uses THREE outputs while streaming: the main temp (members that stay put),
// plus a move-temp (tmp2fd) that collects the members being moved. handle_
// position() decides where they get reinserted. commit_archive() concatenates
// them in the right order at the end.
void cmd_move(void)
{
    start_temp_archive();
    if (open_archive())
        die_no_archive();
    ar.tmp2fd = mkstemp(ar.tmpl_move);
    if (ar.tmp2fd < 0) {
        fprintf(stderr, "%s: error: cannot create third temporary file\n", ar.progname);
        finish(1);
    }
    ar.tmp2_name = ar.tmpl_move;
    while (!next_member()) {
        handle_position();
        if (match_member()) {
            log_action('m');
            copy_member(ar.arfd, ar.tmp2fd, IODD + OODD + HEAD); // set aside to move
            continue;
        }
        log_action('c');
        copy_member(ar.arfd, ar.tmpfd, IODD + OODD + HEAD); // stays in place
    }
    commit_archive();
}

// Command 't' — list the table of contents.
//
// Prints each (named, or all) member's name; with -v also prints a long,
// ls-style line with permissions, owner, size, and date. Reads only.
void cmd_list(void)
{
    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            if (ar.opt_verbose)
                print_long_entry();
            printf("%s\n", basename_of(ar.cur_file));
        }
        copy_member(ar.arfd, -1, IODD + SKIP); // advance past the data
    }
}

// Command 'q' — quick append.
//
// Adds the named files to the END of the archive without scanning or rewriting
// what's already there — fast, but does no duplicate checking. It writes the
// archive in place (via qfd) rather than through a temp file. Incompatible with
// the positioning options -a/-b.
void cmd_quick(void)
{
    int i, f;

    if (ar.opt_after || ar.opt_before) {
        fprintf(stderr, "%s: error: abi and q incompatible\n", ar.progname);
        finish(1);
    }
    open_archive_rw();
    // From here on, ignore interrupts: we are modifying the real archive and a
    // half-done append is better left to complete than aborted mid-write.
    for (i = 0; caught_signals[i]; i++)
        signal(caught_signals[i], SIG_IGN);
    lseek(ar.qfd, 0l, SEEK_END); // append position
    for (i = 0; i < ar.namecount; i++) {
        ar.cur_file = ar.names[i];
        if (ar.cur_file == 0)
            continue;
        ar.names[i] = 0; // mark this name consumed
        log_action('q');
        f = open_input();
        if (f < 0) {
            fprintf(stderr, "%s: error: cannot open %s\n", ar.progname, ar.cur_file);
            continue;
        }
        // write_member() emits into ar.tmpfd, so point that at the archive fd
        // for the duration of the call, then restore it.
        ar.tmpfd = ar.qfd;
        write_member(f);
        ar.qfd = ar.tmpfd;
    }
}
