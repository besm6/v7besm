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
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "besm6/ar.h"
#include "besm6/b.out.h"

#include "archive.h"

#define W 6 // BESM-6 word length in bytes; members are padded to a multiple of this

// Flag bits passed to copy_member() to describe how to move one member's bytes.
// They can be combined with '+', e.g. IODD + OODD + HEAD.
#define SKIP 1 // discard: read the input but write nothing (used to drop a member)
#define IODD 2 // input is word-padded: read and throw away its trailing pad bytes
#define OODD 4 // output must stay word-aligned: append pad bytes after the data
#define HEAD 8 // write a fresh member header before copying the data

// The command letters (exactly one required) and the option letters.
static char *command_letters = "mrxtdpq"; // one of these selects what ar does
static char *option_letters  = "uvnbail"; // modifiers; shown in the usage message

// Signals we trap so a half-written archive/temp file gets cleaned up on Ctrl-C.
static int caught_signals[] = { SIGHUP, SIGINT, SIGQUIT, 0 }; // 0-terminated list

// All mutable per-run state, bundled so that reset_state() can clear it in one
// memset and repeated ar_run() calls do not depend on one another.
struct arstate {
    struct stat   filestat; // stat of the on-disk file being added
    struct ar_hdr hdr;      // current member header

    void (*command)(void); // selected command function

    // Option flags, one per accepted command-line letter.
    int opt_update;  // 'u': replace only if the input file is newer
    int opt_verbose; // 'v': verbose (counted; vv = extra detail)
    int opt_local;   // 'l': put temp files in cwd, not /tmp
    int opt_after;   // 'a': insert after position_name
    int opt_before;  // 'b': insert before position_name
    int opt_insert;  // 'i': synonym for -b
    int opt_create;  // 'c': suppress the "creating archive" notice

    char **names;        // named-file argv slice
    int    namecount;    // number of named files
    char  *archive_name; // archive path
    char  *position_name; // member named by -a/-b/-i

    // Temporary file name templates (mkstemp replaces the last six 'X' chars).
    char   tmpl_main[20];   // main
    char   tmpl_before[20]; // -b staging
    char   tmpl_move[20];   // m staging
    char  *tmp_name;        // active main temp name (NULL until created)
    char  *tmp1_name;       // active before-temp name
    char  *tmp2_name;       // active move-temp name

    char  *cur_file;        // current file/member name
    char   member_name[31]; // member-name buffer
    int    arfd;            // archive fd
    int    tmpfd;           // main temp fd
    int    tmp1fd;          // before-temp fd
    int    tmp2fd;          // move-temp fd
    int    qfd;             // quick-append fd
    int    insert_state;    // -a/-b insertion state (0/1/2)
    char   iobuf[512];      // I/O buffer

    // Single exit point: finish() unwinds via longjmp back into ar_run() so
    // that the engine can be invoked repeatedly within one process (from tests).
    jmp_buf done_env;
    int     exit_code;
};

static struct arstate ar; // the one and only instance of the engine's state

static void finish(int c);
static void on_signal(int sig);
static void die_write_error(void);
static void die_usage(void);
static void die_no_archive(void);
static int open_archive(void);
static void open_archive_rw(void);
static int next_member(void);
static int match_member(void);
static void handle_position(void);
static int open_input(void);
static int report_missing(void);
static int count_pending(void);
static char *basename_of(char *s);
static void print_long_entry(void);
static void print_perm_bits(void);
static void print_perm_char(const int *pairp);
static void copy_member(int fi, int fo, int flag);
static void write_member(int f);
static void start_temp_archive(void);
static void append_new_files(void);
static void commit_archive(void);
static void log_action(int c);
static void phase_error(void);
static void set_command(void (*fun)(void));
static void cmd_replace(void);
static void cmd_delete(void);
static void cmd_extract(void);
static void cmd_list(void);
static void cmd_print(void);
static void cmd_move(void);
static void cmd_quick(void);

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
            fprintf(stderr, "ar: must be one of [%s]\n",
                    command_letters);
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
        fprintf(stderr, "ar: only one of [%s] allowed\n",
                command_letters);
        finish(1);
    }
    ar.command = fun;
}

// Command 'r' — replace (or add) members.
//
// Streams the existing archive into a new temp file. For each old member: if it
// matches one of the named files, write the fresh copy from disk instead (or,
// with -u, only if the disk copy is newer); otherwise copy the old member
// through unchanged. Named files that weren't already in the archive are then
// appended by append_new_files(), which also commits the temp over the archive.
static void cmd_replace(void)
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
                    fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
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
static void cmd_delete(void)
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
static void cmd_extract(void)
{
    int f;

    if (open_archive())
        die_no_archive();
    while (!next_member()) {
        if (ar.namecount == 0 || match_member()) {
            f = creat(ar.cur_file, ar.hdr.ar_mode & 0777);
            if (f < 0) {
                fprintf(stderr, "ar: cannot create %s\n", ar.cur_file);
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
static void cmd_print(void)
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
static void cmd_move(void)
{
    start_temp_archive();
    if (open_archive())
        die_no_archive();
    ar.tmp2fd = mkstemp(ar.tmpl_move);
    if (ar.tmp2fd < 0) {
        fprintf(stderr, "ar: cannot create third temporary file\n");
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
static void cmd_list(void)
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
static void cmd_quick(void)
{
    int i, f;

    if (ar.opt_after || ar.opt_before) {
        fprintf(stderr, "ar: abi and q incompatible\n");
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
            fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
            continue;
        }
        // write_member() emits into ar.tmpfd, so point that at the archive fd
        // for the duration of the call, then restore it.
        ar.tmpfd = ar.qfd;
        write_member(f);
        ar.qfd = ar.tmpfd;
    }
}

// Create the main temporary file and start it as an empty archive.
//
// mkstemp() makes a uniquely-named file from the template; we remember its name
// (so finish() can delete it) and write the ARMAG magic word so the temp is a
// valid, if empty, archive ready to receive members.
static void start_temp_archive(void)
{
    uword_t mbuf = ARMAG;

    ar.tmpfd = mkstemp(ar.tmpl_main);
    if (ar.tmpfd < 0) {
        fprintf(stderr,
                "ar: cannot create temporary file\n");
        finish(1);
    }
    ar.tmp_name = ar.tmpl_main;
    if (!putint(ar.tmpfd, mbuf))
        die_write_error();
}

// Open the archive read-only and verify its magic word.
//
// Returns 1 if the file simply doesn't exist (the caller decides whether that
// is fatal); returns 0 on success. A file that exists but has the wrong magic
// is not an archive and is a hard error.
static int open_archive(void)
{
    uword_t mbuf;

    ar.arfd = open(ar.archive_name, O_RDONLY);
    if (ar.arfd < 0)
        return (1);
    if (!getint(ar.arfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.archive_name);
        finish(1);
    }
    return (0);
}

// Open the archive read-write for the q command, creating it if needed.
//
// If the archive doesn't exist yet, create it and write the magic word (warning
// unless -c was given). If it does exist, verify its magic. Leaves the open
// read-write descriptor in ar.qfd.
static void open_archive_rw(void)
{
    uword_t mbuf;

    if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
        if (!ar.opt_create)
            fprintf(stderr, "ar: creating %s\n", ar.archive_name);
        close(creat(ar.archive_name, 0666));
        if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
            fprintf(stderr, "ar: cannot create %s\n", ar.archive_name);
            finish(1);
        }
        mbuf = ARMAG;
        if (!putint(ar.qfd, mbuf))
            die_write_error();
    } else if (!getint(ar.qfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "ar: %s is not in archive format\n",
                ar.archive_name);
        finish(1);
    }
}

// Print the usage line and exit with an error.
static void die_usage(void)
{
    printf("Usage: ar [%s][%s] archive file...\n", option_letters,
           command_letters);
    finish(1);
}

// Complain that the archive doesn't exist and exit.
static void die_no_archive(void)
{
    fprintf(stderr, "ar: %s not found\n", ar.archive_name);
    finish(1);
}

// Signal handler: an interrupt aborts the run (finish deletes temp files).
static void on_signal(int sig)
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
static void finish(int c)
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
static int report_missing(void)
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
static int count_pending(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ar.namecount; i++)
        if (ar.names[i])
            n++;
    return (n);
}

// Append the named files that were not already in the archive, then commit.
//
// After the r command has streamed through the existing archive, any file name
// still left in ar.names[] is new and gets appended here. Finally the rebuilt
// temp file is written back over the archive.
static void append_new_files(void)
{
    int i, f;

    for (i = 0; i < ar.namecount; i++) {
        ar.cur_file = ar.names[i];
        if (ar.cur_file == 0)
            continue;
        ar.names[i] = 0;
        log_action('a');
        f = open_input();
        if (f < 0) {
            fprintf(stderr, "ar: cannot open %s\n", ar.cur_file);
            continue;
        }
        write_member(f);
    }
    commit_archive();
}

// Write the rebuilt temp file(s) back over the real archive.
//
// Recreates the archive from scratch and concatenates the temp files into it.
// ORDER MATTERS: main temp first (members up to the insertion point), then the
// move-temp tmp2 (members relocated by the m command), then the before-temp
// tmp1 (members that came AFTER the insertion point). Together they reproduce
// the archive with insertions/moves in the right place. Interrupts are ignored
// here so this final rewrite is not left half-done.
static void commit_archive(void)
{
    int i;

    for (i = 0; caught_signals[i]; i++)
        signal(caught_signals[i], SIG_IGN);
    if (ar.arfd < 0)
        if (!ar.opt_create)
            fprintf(stderr, "ar: creating %s\n", ar.archive_name);
    close(ar.arfd);
    ar.arfd = creat(ar.archive_name, 0666);
    if (ar.arfd < 0) {
        fprintf(stderr, "ar: cannot create %s\n", ar.archive_name);
        finish(1);
    }
    if (ar.tmp_name) { // main temp: everything up to the insertion point
        lseek(ar.tmpfd, 0l, SEEK_SET);
        while ((i = read(ar.tmpfd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
    if (ar.tmp2_name) { // move-temp: members relocated by 'm'
        lseek(ar.tmp2fd, 0l, SEEK_SET);
        while ((i = read(ar.tmp2fd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
    if (ar.tmp1_name) { // before-temp: members after the insertion point
        lseek(ar.tmp1fd, 0l, SEEK_SET);
        while ((i = read(ar.tmp1fd, ar.iobuf, 512)) > 0)
            if (write(ar.arfd, ar.iobuf, i) != i)
                die_write_error();
    }
}

// Add the on-disk file open on fd 'f' to the temp archive as a new member.
//
// Fills in the member header from the file's stat info (name trimmed to its
// basename and packed into the fixed-width ar_name field), then copies the file
// bytes into the main temp with a header (HEAD) and output padding (OODD).
static void write_member(int f)
{
    const char *cp;
    int i;

    cp = basename_of(ar.cur_file);
    // Copy the name into the fixed-size ar_name field. Once the source string
    // ends, stop advancing cp so the remaining bytes are filled with its NUL.
    for (i = 0; i < (int)sizeof(ar.hdr.ar_name); i++)
        if ((ar.hdr.ar_name[i] = *cp))
            cp++;
    ar.hdr.ar_size = ar.filestat.st_size;
    ar.hdr.ar_date = ar.filestat.st_mtime;
    ar.hdr.ar_uid  = ar.filestat.st_uid;
    ar.hdr.ar_gid  = ar.filestat.st_gid;
    ar.hdr.ar_mode = ar.filestat.st_mode;
    copy_member(f, ar.tmpfd, OODD + HEAD);
    close(f);
}

// Open the named file (ar.cur_file) for reading and stat it.
//
// Returns the open fd, with the file's metadata left in ar.filestat for
// write_member() to use. Returns a negative value if it can't be opened or
// stat'd.
static int open_input(void)
{
    int f;

    f = open(ar.cur_file, O_RDONLY);
    if (f < 0)
        return (f);
    if (fstat(f, &ar.filestat) < 0) {
        close(f);
        return (-1);
    }
    return (f);
}

// Copy one member's bytes from fd fi to fd fo, honoring the flag bits.
//
// This is the workhorse behind every command. The 'flag' argument is a bitwise
// combination of HEAD/IODD/OODD/SKIP (see their #defines):
//   HEAD  - first emit a member header on fo.
//   IODD  - the input on fi is word-padded; consume those pad bytes too.
//   OODD  - keep the output word-aligned by writing pad bytes after the data.
//   SKIP  - a "dry read": pull the bytes off fi but write nothing (fo may be -1).
// The size to copy comes from the current header ar.hdr.ar_size.
//
// An archive member is zero-padded to a whole BESM-6 word (W=6 bytes), and the
// already-aligned size is written into the header: ld steps to the next member
// by `ar_size + ARHDRSZ` without rounding, so ar_size must be a multiple of W.
static void copy_member(int fi, int fo, int flag)
{
    int pe;
    long size = ar.hdr.ar_size; // actual number of data bytes
    int pad   = (int)((W - size % W) % W); // padding to the word boundary (0..W-1)

    if (flag & HEAD) {
        ar.hdr.ar_size = size + pad; // header records the padded (word-aligned) size
        if (!putarhdr(fo, &ar.hdr))
            die_write_error();
    }
    // Copy the data in 512-byte chunks. 'pe' counts short reads (a corrupt or
    // truncated archive), reported once at the end as a "phase error".
    pe = 0;
    while (size > 0) {
        int i, o;
        i = o = (size < 512) ? (int)size : 512;
        if (read(fi, ar.iobuf, i) != i)
            pe++;
        if ((flag & SKIP) == 0)
            if (write(fo, ar.iobuf, o) != o)
                die_write_error();
        size -= 512;
    }
    // Deal with the word-alignment padding.
    if (pad) {
        if (flag & IODD)
            if (read(fi, ar.iobuf, pad) != pad) // consume the padding from the input
                pe++;
        if ((flag & OODD) && (flag & SKIP) == 0) {
            char zero[W];
            memset(zero, 0, sizeof(zero));
            if (write(fo, zero, pad) != pad) // write matching padding to the output
                die_write_error();
        }
    }
    if (pe)
        phase_error();
}

// Read the next member header from the archive into ar.hdr.
//
// Returns 0 and sets ar.cur_file to the member's name on success, or 1 at end
// of archive. At EOF, if an insertion temp (tmp1) was opened, swap it into the
// main temp slot: this makes commit_archive() write the pre-insertion members
// (now in tmp1... actually the two fds are exchanged so the halves join in the
// correct order). See handle_position() for how the split happens.
static int next_member(void)
{
    int i;

    if (!getarhdr(ar.arfd, &ar.hdr)) {
        if (ar.tmp1_name) { // stitch the two insertion temps back together
            i         = ar.tmpfd;
            ar.tmpfd  = ar.tmp1fd;
            ar.tmp1fd = i;
        }
        return (1);
    }
    // The stored name isn't NUL-terminated in the header, so copy it into a
    // buffer that is, and point cur_file at it.
    for (i = 0; i < (int)sizeof(ar.hdr.ar_name); i++)
        ar.member_name[i] = ar.hdr.ar_name[i];
    ar.cur_file = ar.member_name;
    return (0);
}

// Is the current member one of the files named on the command line?
//
// Compares the current member's name against each still-pending name. On a
// match, marks that name "consumed" (sets ar.names[i] = NULL so it won't be
// added again or reported missing), repoints ar.cur_file at the caller's
// spelling, and returns 1. Returns 0 if the member wasn't requested.
static int match_member(void)
{
    int i;

    for (i = 0; i < ar.namecount; i++) {
        if (ar.names[i] == 0)
            continue;
        if (strcmp(basename_of(ar.names[i]), ar.cur_file) == 0) {
            ar.cur_file = ar.names[i];
            ar.names[i] = 0;
            return (1);
        }
    }
    return (0);
}

// Implements the -a/-b/-i "insert at a position" feature for r and m.
//
// Called once per existing member while streaming the archive. It is a tiny
// state machine:
//   state 1 = still looking for the member named by position_name. When found,
//             -a ("after") returns now so the current member is copied first;
//             -b/-i ("before") falls straight through to state 2.
//   state 2 = reached the splice point: open a SECOND temp file (tmp1) and
//             redirect the rest of the old members into it. New files were/are
//             written to the main temp, so they end up in the middle. next_
//             member() rejoins the two temps at end of archive.
//   state 0 = nothing more to do.
static void handle_position(void)
{
    int f;

    switch (ar.insert_state) {
    case 1:
        if (strcmp(ar.cur_file, ar.position_name) != 0)
            return; // not the target member yet
        ar.insert_state = 2;
        if (ar.opt_after)
            return; // -a: let this member through, splice after it next time
        // fallthrough

    case 2:
        ar.insert_state = 0;
        f               = mkstemp(ar.tmpl_before);
        if (f < 0) {
            fprintf(stderr, "ar: cannot create second temporary file\n");
            return;
        }
        // Divert the remaining members: tmp1 keeps the old main-temp fd (the
        // pre-splice part), and the main temp fd becomes the new file so the
        // post-splice members are written separately.
        ar.tmp1_name = ar.tmpl_before;
        ar.tmp1fd    = ar.tmpfd;
        ar.tmpfd     = f;
    }
}

// Warn that a member was shorter than its header claimed (corrupt archive).
static void phase_error(void)
{
    fprintf(stderr, "ar: phase error on %s\n", ar.cur_file);
}

// Print a one-line "action - name" trace when -v is in effect.
//
// 'c' means "copied unchanged" and is only shown at double verbosity (-vv), to
// avoid drowning the output in noise for a large archive.
static void log_action(int c)
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
static char *basename_of(char *s)
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

// Permission and file-type bits, matching the on-disk st_mode layout (octal).
#define IFMT  060000 // mask for the file-type field (unused here)
#define ISARG 01000  // "argument" bit (unused here)
#define LARGE 010000 // large-file bit (unused here)
#define SUID  04000  // set-user-id on execution
#define SGID  02000  // set-group-id on execution
#define ROWN  0400   // owner read
#define WOWN  0200   // owner write
#define XOWN  0100   // owner execute
#define RGRP  040    // group read
#define WGRP  020    // group write
#define XGRP  010    // group execute
#define ROTH  04     // other read
#define WOTH  02     // other write
#define XOTH  01     // other execute
#define STXT  01000  // sticky ("save text") bit

// Print the long, ls -l style listing line for the current member (t -v).
//
// Shows the permission string, owner/group ids, size, and formatted date. The
// ctime() string is chopped up with printf field widths to pick out the
// "Mon DD HH:MM" and year portions.
static void print_long_entry(void)
{
    const char *cp;
    time_t t;

    print_perm_bits();
    printf("%3d/%1d", (int)ar.hdr.ar_uid, (int)ar.hdr.ar_gid);
    printf("%7ld", (long)ar.hdr.ar_size);
    t  = ar.hdr.ar_date;
    cp = ctime(&t);
    printf(" %-12.12s %-4.4s ", cp + 4, cp + 20);
}

// Permission-display tables. Each row has the form
//     { count, mask, char, mask, char, ... }
// print_perm_char() tests the masks in order and prints the char paired with
// the first mask that is set, or the final fallback char if none match. The
// nine rows drive the nine columns of an "rwxrwxrwx" permission string.
static int m1[] = { 1, ROWN, 'r', '-' };             // owner read
static int m2[] = { 1, WOWN, 'w', '-' };             // owner write
static int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };  // owner exec / setuid
static int m4[] = { 1, RGRP, 'r', '-' };             // group read
static int m5[] = { 1, WGRP, 'w', '-' };             // group write
static int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };  // group exec / setgid
static int m7[] = { 1, ROTH, 'r', '-' };             // other read
static int m8[] = { 1, WOTH, 'w', '-' };             // other write
static int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };  // other exec / sticky

static int *m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 }; // the nine columns in order

// Print the nine-character "rwxrwxrwx" permission field for the current member.
static void print_perm_bits(void)
{
    int **mp;

    for (mp = &m[0]; mp < &m[9];)
        print_perm_char(*mp++);
}

// Print one character of the permission string from a table row.
//
// The row starts with a count of (mask, char) pairs to try. The first pair
// whose mask is set in ar_mode wins; if none match, the char just past the last
// pair is the fallback (typically '-').
static void print_perm_char(const int *pairp)
{
    int n;
    const int *ap;

    ap = pairp;
    n  = *ap++;
    while (--n >= 0 && (ar.hdr.ar_mode & *ap++) == 0)
        ap++;
    putchar(*ap);
}

// Report a write failure (disk full, etc.) and exit.
static void die_write_error(void)
{
    perror("ar write error");
    finish(1);
}
