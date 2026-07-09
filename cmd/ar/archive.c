//
// BESM-6 archiver: the archive/member streaming engine and insertion
// positioning.
//
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "intern.h"

static void phase_error(void); // file-local; defined below

// Create the main temporary file and start it as an empty archive.
//
// mkstemp() makes a uniquely-named file from the template; we remember its name
// (so finish() can delete it) and write the ARMAG magic word so the temp is a
// valid, if empty, archive ready to receive members.
void start_temp_archive(void)
{
    uword_t mbuf = ARMAG;

    ar.tmpfd = mkstemp(ar.tmpl_main);
    if (ar.tmpfd < 0) {
        fprintf(stderr, "%s: error: cannot create temporary file\n", ar.progname);
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
int open_archive(void)
{
    uword_t mbuf;

    ar.arfd = open(ar.archive_name, O_RDONLY);
    if (ar.arfd < 0)
        return (1);
    if (!getint(ar.arfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "%s: error: %s is not in archive format\n", ar.progname, ar.archive_name);
        finish(1);
    }
    return (0);
}

// Open the archive read-write for the q command, creating it if needed.
//
// If the archive doesn't exist yet, create it and write the magic word (warning
// unless -c was given). If it does exist, verify its magic. Leaves the open
// read-write descriptor in ar.qfd.
void open_archive_rw(void)
{
    uword_t mbuf;

    if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
        if (!ar.opt_create)
            fprintf(stderr, "%s: creating %s\n", ar.progname, ar.archive_name);
        close(creat(ar.archive_name, 0666));
        if ((ar.qfd = open(ar.archive_name, O_RDWR)) < 0) {
            fprintf(stderr, "%s: error: cannot create %s\n", ar.progname, ar.archive_name);
            finish(1);
        }
        mbuf = ARMAG;
        if (!putint(ar.qfd, mbuf))
            die_write_error();
    } else if (!getint(ar.qfd, &mbuf) || mbuf != ARMAG) {
        fprintf(stderr, "%s: error: %s is not in archive format\n", ar.progname, ar.archive_name);
        finish(1);
    }
}

// Append the named files that were not already in the archive, then commit.
//
// After the r command has streamed through the existing archive, any file name
// still left in ar.names[] is new and gets appended here. Finally the rebuilt
// temp file is written back over the archive.
void append_new_files(void)
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
            fprintf(stderr, "%s: error: cannot open %s\n", ar.progname, ar.cur_file);
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
void commit_archive(void)
{
    int i;

    for (i = 0; caught_signals[i]; i++)
        signal(caught_signals[i], SIG_IGN);
    if (ar.arfd < 0)
        if (!ar.opt_create)
            fprintf(stderr, "%s: creating %s\n", ar.progname, ar.archive_name);
    close(ar.arfd);
    ar.arfd = creat(ar.archive_name, 0666);
    if (ar.arfd < 0) {
        fprintf(stderr, "%s: error: cannot create %s\n", ar.progname, ar.archive_name);
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
void write_member(int f)
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
int open_input(void)
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
void copy_member(int fi, int fo, int flag)
{
    int pe;
    long size = ar.hdr.ar_size;            // actual number of data bytes
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
int next_member(void)
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
int match_member(void)
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
void handle_position(void)
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
            fprintf(stderr, "%s: error: cannot create second temporary file\n", ar.progname);
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
    fprintf(stderr, "%s: error: phase error on %s\n", ar.progname, ar.cur_file);
}
