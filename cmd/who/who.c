/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// who -- who is on the system.
//
//      who             every live record of /etc/utmp
//      who file        every record of that file, blank ones included
//      who am i        the one record whose line is this terminal's
//
// Task C6's first program, and the first reader /etc/utmp has ever had.  Everything that
// writes that file was already here -- cmd/init/init.c's merge() creates it and rmut()
// blanks a record, cmd/login/login.c writes one, lib/libc/gen/getlogin.c reads the single
// record it is standing on -- but nothing had ever printed the table.
//
// THE FIELDS ARE NOT NUL-TERMINATED, and that is the whole of what this port had to get
// right.  <utmp.h> is `char ut_line[8]; char ut_name[8]; time_t ut_time;', login writes
// exactly eight bytes with strncpy, and a name or a line of exactly eight characters
// therefore runs straight into the next field.  v7 read them with strcpy() and strcmp()
// anyway.  Three consequences here:
//
//   - the printf conversions are v7's `%-8.8s' and the PRECISION is what saves them:
//     lib/libc/stdio/doprnt.c stops at dwidth or at a NUL, whichever comes first, so an
//     eight-character name prints as eight characters and reads no further.
//   - the "am i" comparison is strncmp() over the field width, not strcmp().  Both sides
//     are NUL-padded rather than NUL-terminated, so eight bytes is the exact question.
//   - the fabricated record in the no-terminal path is filled with strncpy() bounded by
//     sizeof, where v7 wrote strcpy() into a field the name may not fit in.
//
// THE LINE NAME IS DERIVED THE WAY login DERIVES IT.  ut_line holds what login stored,
// which is `strchr(ttyn + 1, '/') + 1' of the terminal's path -- "console", "tty1".  This
// asks the same question of ttyname(0) with the same expression, so the two cannot drift;
// strrchr() would answer the same for every name /dev has today and differently for the
// first one with a subdirectory in it.
//
// TWO DIVERGENCES, both about a diagnostic and both marked `Note:' in who.1.  v7 printed
// `who: cannot open utmp' with puts(), on STANDARD OUTPUT and naming a file it might not
// have been given; this names the file it actually failed on and prints it on standard
// error, as every other program in cmd/ does.
//
// RECORD 0 IS ALWAYS BLANK and that is not a defect: login seeks to ttyslot() * sizeof,
// and ttyslot() counts /etc/ttys from 1.  v7's `ut_name[0] == '\0'' skip covers it, and
// that skip is deliberately conditional on argc == 1 -- `who file' shows the blank
// records, which is how a logout reads in a file that keeps them.
//
// NOT SETUID: /etc/utmp is 0644 and every field it prints is public.
//
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

static struct utmp utmp;

static void putline(void)
{
    char *cbuf;

    printf("%-8.8s %-8.8s", utmp.ut_name, utmp.ut_line);
    cbuf = ctime(&utmp.ut_time);
    printf("%.12s\n", cbuf + 4);
}

int main(int argc, char *argv[])
{
    struct passwd *pw;
    char *tp, *s;
    FILE *fi;

    tp = NULL;
    s  = "/etc/utmp";
    if (argc == 2)
        s = argv[1];
    if (argc == 3) {
        // "who am i": the record for this terminal, and nothing else.
        tp = ttyname(0);
        if (tp != NULL)
            tp = strchr(tp + 1, '/');
        if (tp != NULL) {
            tp++;
        } else {
            // No terminal.  The best guess is the password file, and the answer
            // says so: the line is printed as `tty??' rather than invented.
            pw = getpwuid(getuid());
            strncpy(utmp.ut_name, pw ? pw->pw_name : "?", sizeof(utmp.ut_name));
            strncpy(utmp.ut_line, "tty??", sizeof(utmp.ut_line));
            time(&utmp.ut_time);
            putline();
            return 0;
        }
    }
    if ((fi = fopen(s, "r")) == NULL) {
        fprintf(stderr, "who: cannot open %s\n", s);
        return 1;
    }
    while (fread((char *)&utmp, sizeof(utmp), 1, fi) == 1) {
        if (argc == 3) {
            if (strncmp(utmp.ut_line, tp, sizeof(utmp.ut_line)) != 0)
                continue;
            putline();
            return 0;
        }
        if (utmp.ut_name[0] == '\0' && argc == 1)
            continue;
        putline();
    }
    return 0;
}
