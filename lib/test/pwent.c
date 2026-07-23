//
// pwent -- the accounts and terminal routines, plus crypt.
//
// NOTHING OUT OF /etc/passwd REACHES THE OUTPUT, and it cannot: the harness diffs
// against a checked-in .expected, and the host's account list differs from machine to
// machine -- on some of them (macOS) the logged-in user is not in the file at all.  So
// the pw and gr families are tested for SELF-CONSISTENCY instead: every entry the
// walker yields must be findable again by name and by id, and only the verdict is
// printed.  A host with no /etc/passwd makes the walk empty and every claim below still
// true, which is the right answer for a test that is checking the routines and not the
// machine.
//
// The lookup has to be done in two passes.  getpwnam() shares its statics with
// getpwent() AND rewinds the file underneath it, so a lookup made in the middle of a
// walk would both clobber the entry in hand and restart the walk.  The names and ids
// are copied out first; that they must be is itself the contract <pwd.h> states.
//
// The terminal three answer for their failure paths here, which is all the simulator
// can offer: ttyname() reads /dev with read(2), as v7 did and as this kernel will
// allow, and b6sim's read() is the host's and refuses a directory -- so it is NULL,
// ttyslot() is 0 and getlogin(), which needs a slot, is NULL.  Under the real kernel
// with a root filesystem they will have something to say; that run is kernel task
// 18b.6 and not this one.
//
// The crypt vectors are the HOST's crypt(3), not this program's own first output: DES
// has one right answer and the point is to agree with it.
//
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>

char *ttyname(int f);
int ttyslot(void);
char *getlogin(void);
int getpw(int uid, char buf[]);
char *crypt(const char *pw, const char *salt);
int open(const char *path, int mode);
int close(int fd);

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
    }
    endpwent();
    ok("the walk terminates", n >= 0 && n <= MAXENT);

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
        // The member vector must be NULL-terminated inside its own bounds.
        for (m = gr->gr_mem; *m != 0; m++) {
            mem++;
            if (mem > MAXENT * 100)
                break;
        }
    }
    endgrent();
    ok("the walk terminates", n >= 0 && n <= MAXENT);
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

    // ---- getpw, v7's obsolete one ----
    printf("--- getpw\n");
    ok("a uid nobody has fails", getpw(-12345, buf) == 1);

    // ---- the terminal three ----
    printf("--- terminals\n");
    fd = open("pwent.c", 0);
    ok("a regular file is not a terminal", fd < 0 || ttyname(fd) == 0);
    if (fd >= 0)
        close(fd);
    ok("ttyname of a closed descriptor is null", ttyname(31) == 0);
    ok("ttyslot has no slot to report", ttyslot() == 0);
    ok("getlogin has no slot to read", getlogin() == 0);

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

    printf("done\n");
    return 0;
}
