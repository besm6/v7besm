//
// pwent -- the accounts and terminal routines, plus crypt.
//
// WHAT IS IN /etc/passwd REACHES THE OUTPUT NOW, and for a long time it could not.  This
// program adjudicates two runs against one .expected -- b6sim and the booted kernel -- and
// b6sim used to hand the literal path to the host, so every name read was a property of
// whoever was building (on macOS the logged-in user is not in that file at all).  b6sim
// serves the target's own /etc/passwd and /etc/group now (cmd/sim/etcfiles.cpp, and
// cmd/sim/test/etc_test.cpp is what keeps the two copies byte for byte), so BOTH RUNS READ
// THE SAME SIX ACCOUNTS AND THE SAME FOUR GROUPS and the file's content can be asserted.
//
// That argument is the one to make before adding a claim here, and it is not the same as
// running the test.  The image half runs under libtest, which is labelled `weekly' and is
// outside the daily gate, so a claim that is true under b6sim and false on the image would
// pass `make run' and surface days later.  Everything below is true because the two worlds
// read the same bytes.  Anything that is not -- ttyslot(), getlogin(), anything reaching
// /dev -- stays in ttyt, which is image-only.
//
// THE SELF-CONSISTENCY CHECKS STAY, and they are not made redundant by the content ones:
// every entry the walker yields must still be findable again by name and by id, which is a
// claim about the ROUTINES rather than about the file, and it would survive an edit to
// etc/passwd that the printed walk below would (deliberately) show as a diff.
//
// The lookup has to be done in two passes.  getpwnam() shares its statics with
// getpwent() AND rewinds the file underneath it, so a lookup made in the middle of a
// walk would both clobber the entry in hand and restart the walk.  The names and ids
// are copied out first; that they must be is itself the contract <pwd.h> states.
//
// TWO OF THE TERMINAL ROUTINES ANSWER FOR THEIR FAILURE PATHS HERE, and that is all a
// program running in both worlds can say about them: ttyname() reads /dev with read(2),
// as v7 did and as this kernel allows, and b6sim's read() is the host's and refuses a
// directory -- so a descriptor that is not a terminal, and a descriptor that is not
// open, are NULL on both.  ttyslot() and getlogin() are NOT here any more.  They used
// to be, answering 0 and NULL in both worlds, but only because /etc/ttys was missing;
// kernel task 29b put it on the image and the two worlds now disagree.  Their real
// answers are ttyt's, which runs on the image only.
//
// The crypt vectors are the HOST's crypt(3), not this program's own first output: DES
// has one right answer and the point is to agree with it.
//
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // crypt, since task C6

// getpw() is still the caller's to declare: <pwd.h> covers the getpwent family and this
// one is the old two-argument form nothing else calls.
int getpw(int uid, char buf[]);

#define MAXENT  64
#define MAXNAME 32

static char names[MAXENT][MAXNAME];
static int ids[MAXENT];

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

int main(void)
{
    struct passwd *pw;
    struct group *gr;
    char buf[256];
    char **m;
    int n, i, bad, mem, fd;

    // ---- /etc/passwd ----
    printf("--- passwd\n");
    n = 0;
    setpwent();
    while ((pw = getpwent()) != 0 && n < MAXENT) {
        //
        // Entries whose name does not fit are skipped rather than truncated: a
        // truncated name would not be found again and the check below would report a
        // failure of this program's buffer.  /etc/passwd on a host may hold comment
        // lines, which have no colons at all and so become one very long "name".
        //
        if (strlen(pw->pw_name) >= MAXNAME)
            continue;
        strcpy(names[n], pw->pw_name);
        ids[n] = pw->pw_uid;
        n++;
        // The walk itself, so that an edit to etc/passwd shows up here as a readable diff
        // rather than as a count that moved.  Name, uid, gid and home directory; the
        // password field is asserted below and is not printed beside a name.
        printf("%-8s %3d %3d %s\n", pw->pw_name, pw->pw_uid, pw->pw_gid, pw->pw_dir);
    }
    endpwent();
    ok("the walk terminates", n >= 0 && n <= MAXENT);
    ok("the file is the target's six accounts", n == 6);

    bad = 0;
    for (i = 0; i < n; i++) {
        pw = getpwnam(names[i]);
        if (pw == 0 || strcmp(pw->pw_name, names[i]) != 0)
            bad++;
    }
    ok("every entry is found again by name", bad == 0);

    bad = 0;
    for (i = 0; i < n; i++) {
        pw = getpwuid(ids[i]);
        if (pw == 0 || pw->pw_uid != ids[i])
            bad++;
    }
    ok("every entry is found again by uid", bad == 0);

    //
    // Two things every entry must have whatever the file says: a name that is not the
    // empty string, and a shell field that is inside the same line as the name -- the
    // whole entry is one buffer split in place, so a pointer outside it would mean the
    // splitting walked off the end.
    //
    bad = 0;
    for (i = 0; i < n; i++) {
        if (names[i][0] == '\0')
            bad++;
    }
    ok("no entry has an empty name", bad == 0);

    ok("a name nobody has is not found", getpwnam("no-such-user-at-all") == 0);
    ok("a uid nobody has is not found", getpwuid(-12345) == 0);

    //
    // The two accounts anything else on this system depends on.  root is uid 0 with the
    // root directory for a home and NO SHELL FIELD -- the entry ends at the last colon, so
    // pw_shell is the empty string and not a null pointer, which is the difference between
    // a line split in place and one parsed into fresh storage.  guest is the only non-root
    // identity on the image (lib/test/suidt drops to it) and the one login(1) puts a user
    // on; its gid 3 is `bin' in /etc/group below.
    //
    pw = getpwnam("root");
    ok("root is there", pw != 0);
    if (pw != 0) {
        ok("root is uid 0", pw->pw_uid == 0);
        ok("root is gid 1", pw->pw_gid == 1);
        ok("root's home is /", strcmp(pw->pw_dir, "/") == 0);
        ok("root has no shell field", pw->pw_shell != 0 && pw->pw_shell[0] == '\0');
        ok("root has a password", pw->pw_passwd[0] != '\0');
    }

    pw = getpwuid(7);
    ok("uid 7 is guest", pw != 0 && strcmp(pw->pw_name, "guest") == 0);
    if (pw != 0) {
        ok("guest is gid 3", pw->pw_gid == 3);
        ok("guest's home is /home/guest", strcmp(pw->pw_dir, "/home/guest") == 0);
        ok("guest has no password", pw->pw_passwd[0] == '\0');
    }

    // uucp is the one entry with a shell, and so the only one that proves the LAST field
    // is reached at all: five of the six lines end before it.
    pw = getpwnam("uucp");
    ok("uucp's shell is uucico",
       pw != 0 && strcmp(pw->pw_shell, "/usr/lib/uucico") == 0);

    // setpwent may be called twice, and endpwent on a stream never opened.
    setpwent();
    setpwent();
    endpwent();
    endpwent();
    ok("setpwent and endpwent are repeatable", 1);

    // ---- /etc/group ----
    printf("--- group\n");
    n   = 0;
    mem = 0;
    setgrent();
    while ((gr = getgrent()) != 0 && n < MAXENT) {
        if (strlen(gr->gr_name) >= MAXNAME)
            continue;
        strcpy(names[n], gr->gr_name);
        ids[n] = gr->gr_gid;
        n++;
        printf("%-8s %3d", gr->gr_name, gr->gr_gid);
        // The member vector must be NULL-terminated inside its own bounds, and printing it
        // is the only thing in either family that walks a vector rather than a field.
        for (m = gr->gr_mem; *m != 0; m++) {
            printf(" %s", *m);
            mem++;
            if (mem > MAXENT * 100)
                break;
        }
        printf("\n");
    }
    endgrent();
    ok("the walk terminates", n >= 0 && n <= MAXENT);
    ok("the file is the target's four groups", n == 4);
    ok("every member vector is terminated", mem <= MAXENT * 100);

    bad = 0;
    for (i = 0; i < n; i++) {
        gr = getgrnam(names[i]);
        if (gr == 0 || strcmp(gr->gr_name, names[i]) != 0)
            bad++;
    }
    ok("every entry is found again by name", bad == 0);

    bad = 0;
    for (i = 0; i < n; i++) {
        gr = getgrgid(ids[i]);
        if (gr == 0 || gr->gr_gid != ids[i])
            bad++;
    }
    ok("every entry is found again by gid", bad == 0);

    ok("a group nobody has is not found", getgrnam("no-such-group-at-all") == 0);
    ok("a gid nobody has is not found", getgrgid(-12345) == 0);

    //
    // The member list, which is the one thing /etc/group has and /etc/passwd has not.  bin
    // is gid 3, the group guest is in, and its two members are the only place in either
    // file where a comma-separated vector is parsed at all.
    //
    gr = getgrnam("bin");
    ok("bin is there", gr != 0);
    if (gr != 0) {
        ok("bin is gid 3", gr->gr_gid == 3);
        m = gr->gr_mem;
        ok("bin's members are bin and guest",
           m[0] != 0 && strcmp(m[0], "bin") == 0 && m[1] != 0 && strcmp(m[1], "guest") == 0 &&
               m[2] == 0);
    }

    gr = getgrgid(1);
    ok("gid 1 is other", gr != 0 && strcmp(gr->gr_name, "other") == 0);
    if (gr != 0) {
        m = gr->gr_mem;
        ok("other's members are root and daemon",
           m[0] != 0 && strcmp(m[0], "root") == 0 && m[1] != 0 && strcmp(m[1], "daemon") == 0 &&
               m[2] == 0);
    }

    // ---- getpw, v7's obsolete one ----
    printf("--- getpw\n");
    ok("a uid nobody has fails", getpw(-12345, buf) == 1);
    // The WHOLE LINE, newline stripped, which is all this call has ever promised -- and the
    // reason fsck(1M) still uses it (cmd/fsck/fsck.c pinode()).  It keeps a stream of its
    // own, so this also says that the two are reading the same file.
    ok("uid 0 comes back as its whole line",
       getpw(0, buf) == 0 && strcmp(buf, "root:.8Y/JOGhfuk1I:0:1::/:") == 0);

    // ---- the terminal three ----
    printf("--- terminals\n");
    fd = open("pwent.c", O_RDONLY);
    ok("a regular file is not a terminal", fd < 0 || ttyname(fd) == 0);
    if (fd >= 0)
        close(fd);
    ok("ttyname of a closed descriptor is null", ttyname(31) == 0);
    // ttyslot() AND getlogin() USED TO BE ASSERTED HERE and are not any more, and the reason
    // is no longer the one this comment used to give.  It is NOT that /etc/ttys is missing
    // under b6sim -- b6sim serves it now, the same seventeen bytes the image carries.  It is
    // that ttyslot() starts from ttyname(0), which reads /dev with read(2), and b6sim's
    // read(2) is the host's and refuses a directory: the two worlds disagree about the
    // DIRECTORY and not about the file.  Their positive answers are in ttyt, image-only for
    // that reason, and moving them back here would fail on one harness only.

    // ---- crypt ----
    printf("--- crypt\n");
    printf("crypt(\"\",\"..\")          %s\n", crypt("", ".."));
    printf("crypt(\"a\",\"sa\")         %s\n", crypt("a", "sa"));
    printf("crypt(\"password\",\"Z.\")  %s\n", crypt("password", "Z."));
    printf("crypt(\"abcdefgh\",\"zz\")  %s\n", crypt("abcdefgh", "zz"));
    printf("crypt(\"abcdefghi\",\"9x\") %s\n", crypt("abcdefghi", "9x"));
    printf("crypt(\"besm6\",\"aQ\")     %s\n", crypt("besm6", "aQ"));
    ok("crypt agrees with the host's DES", strcmp(crypt("besm6", "aQ"), "aQR1E1yZh1bPE") == 0);
    ok("only the first eight characters count",
       strcmp(crypt("abcdefgh", "zz"), crypt("abcdefghXYZ", "zz")) == 0);
    ok("the salt is the first two characters of the answer",
       strncmp(crypt("password", "Z."), "Z.", 2) == 0);
    //
    // And the one vector that is not arbitrary: the hash root actually carries on this
    // system.  It ties three things together that no single test could reach before -- the
    // password file b6sim and the kernel now agree on, this DES implementation, and
    // kernel/test/login.ini, which types `root' at the login prompt and expects a shell.
    // If this line ever fails, either crypt changed or nobody can log in.
    //
    pw = getpwnam("root");
    ok("root's password is crypt(\"root\", \".8\")",
       pw != 0 && strcmp(crypt("root", ".8"), pw->pw_passwd) == 0);

    printf("done\n");
    return 0;
}
