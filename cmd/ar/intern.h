//
// Internal shared declarations for the BESM-6 archiver engine.
//
// Global engine state, the shared constants, and the prototypes of the
// functions split across ar.c, command.c, archive.c, list.c and util.c.  This
// is a C-only header; the public, C++-safe entry point is in archive.h.
//
#ifndef BESM6_AR_INTERN_H
#define BESM6_AR_INTERN_H

#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "archive.h"
#include "besm6/ar.h"
#include "besm6/b.out.h"

#define W 6 // BESM-6 word length in bytes; members are padded to a multiple of this

// Flag bits passed to copy_member() to describe how to move one member's bytes.
// They can be combined with '+', e.g. IODD + OODD + HEAD.
#define SKIP 1 // discard: read the input but write nothing (used to drop a member)
#define IODD 2 // input is word-padded: read and throw away its trailing pad bytes
#define OODD 4 // output must stay word-aligned: append pad bytes after the data
#define HEAD 8 // write a fresh member header before copying the data

// All mutable per-run state, bundled so that reset_state() can clear it in one
// memset and repeated ar_run() calls do not depend on one another.
struct arstate {
    struct stat filestat; // stat of the on-disk file being added
    struct ar_hdr hdr;    // current member header

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
    int namecount;       // number of named files
    char *archive_name;  // archive path
    char *position_name; // member named by -a/-b/-i

    // Temporary file name templates (mkstemp replaces the last six 'X' chars).
    char tmpl_main[20];   // main
    char tmpl_before[20]; // -b staging
    char tmpl_move[20];   // m staging
    char *tmp_name;       // active main temp name (NULL until created)
    char *tmp1_name;      // active before-temp name
    char *tmp2_name;      // active move-temp name

    char *cur_file;                    // current file/member name
    char member_name[ARMAXNAME + 1];   // member-name buffer
    char *progname;       // diagnostic prefix: basename of argv[0]
    int arfd;             // archive fd
    int tmpfd;            // main temp fd
    int tmp1fd;           // before-temp fd
    int tmp2fd;           // move-temp fd
    int qfd;              // quick-append fd
    int insert_state;     // -a/-b insertion state (0/1/2)
    char iobuf[512];      // I/O buffer

    // Single exit point: finish() unwinds via longjmp back into ar_run() so
    // that the engine can be invoked repeatedly within one process (from tests).
    jmp_buf done_env;
    int exit_code;
};

extern struct arstate ar;                      // defined in ar.c
extern char *command_letters, *option_letters; // defined in ar.c
extern int caught_signals[];                   // defined in ar.c

// Driver / dispatch (ar.c)
int ar_run(int argc, char **argv);

// Command handlers (command.c)
void cmd_replace(void);
void cmd_delete(void);
void cmd_extract(void);
void cmd_list(void);
void cmd_print(void);
void cmd_move(void);
void cmd_quick(void);

// Archive & member streaming (archive.c)
void start_temp_archive(void);
int open_archive(void);
void open_archive_rw(void);
int next_member(void);
int match_member(void);
void handle_position(void);
void copy_member(int fi, int fo, int flag);
void write_member(int f);
int open_input(void);
void append_new_files(void);
void commit_archive(void);

// Table-of-contents listing (list.c)
void print_long_entry(void);

// Diagnostics, exit path, small helpers (util.c)
void finish(int c);
void on_signal(int sig);
void die_usage(void);
void die_no_archive(void);
void die_write_error(void);
int report_missing(void);
int count_pending(void);
void log_action(int c);
char *basename_of(char *s);

#endif // BESM6_AR_INTERN_H
