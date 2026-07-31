/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /bin/login -- check a password and become the user's shell.
//
//      login [ name ]
//
// The second of task 29b's two (../../kernel/TODO.md), and THE FIRST PROGRAM ON THIS MACHINE
// THAT MAKES A SHELL BELONGING TO SOMEBODY OTHER THAN ROOT.  Until now init exec'd /bin/sh
// directly, so every process the system has ever run above the icode has had uid 0; the only
// exception was lib/test/suidt, which drops to uid 7 to prove the setuid bit works.  Here the
// drop is the point rather than the experiment: getty execs this program as root, it checks
// the password, and setgid()/setuid() hand the terminal to whoever answered.
//
// The chain around it: init forks /etc/getty per line of /etc/ttys, getty reads the name and
// execs THIS with the name as argv[1], and this execs the shell.  All one process, so when
// the user logs out that process exits and init's multiple() puts a fresh getty on the line.
//
// THE ORDER OF THE PRIVILEGED CALLS IS LOAD-BEARING and must not be tidied.  chown() on the
// terminal is gated on suser() in this kernel (kernel/sys4.c), so it has to happen while this
// is still root -- which means before setgid()/setuid() and not after.  Moving that line down
// makes a login that "works" and leaves every terminal owned by root.
//
// The C11 pass is ../README.md §1's and mostly mechanical, with four things in it that are
// not:
//
//   - §2, THE POINTER COMPARISON.  `if (namep < utmp.ut_name+8)' bounded the name buffer with
//     a relational between two char *, which did not order them when this was ported.  It is
//     an int index now.  Same bug class as the one that made getpass() -- which this program
//     is the first caller of -- return the empty string for months (lib/libc/README.md).
//
//   - §3, THE LONGS.  `lseek(f, (long)(t*sizeof(utmp)), 0)' and `lseek(f, 0L, 2)': off_t is
//     ONE WORD here, so the casts say nothing and are gone, and the whence arguments are
//     spelled SEEK_SET/SEEK_END now that <unistd.h> names them.
//
//   - THE K&R DECLARATIONS AT THE HEAD HAD TO GO rather than be modernized.  <pwd.h> declares
//     getpwnam()/setpwent()/endpwent() and <unistd.h> declares ttyname(), and a second
//     declaration of a different shape is an error rather than a redundancy -- the lesson
//     cmd/tty/tty.c learned first.  crypt(), getpass() and ttyslot() are the other way round:
//     no header in this tree declares any of them (each source's head says the caller must),
//     so those three are written out below, as lib/test/pwent.c writes them out.
//
//   - AN UPSTREAM BUG.  v7 passed utmp.ut_name -- a char[8] that SCPYN (strncpy) leaves
//     UNTERMINATED for a name of exactly eight characters -- straight to getpwnam(), which
//     then read on into ut_time.  The name is read into a local of nine bytes here and copied
//     into the record; the record still holds v7's eight unterminated bytes, because that is
//     what /etc/utmp's format is.
//
// WHAT IS NOT ON THIS IMAGE, and is left in rather than cut, because each is one call that
// fails cleanly and each comes back with a task of its own:
//
//   /usr/adm/wtmp   the permanent login history.  cmd/init writes it too and treats its
//                   absence as benign; nothing reads it until `last', which no task carries,
//                   so a file appended to on every getty respawn would have no reader.  The
//                   open() fails and login carries on.  See README.md.
//   /usr/spool/mail the mail spool.  access() fails, "You have mail." never prints, and mail
//                   itself is task C10's.
//   /usr/bin        named by envinit's PATH, which is v7's string kept verbatim.  The shell's
//                   own default already reaches /bin, and the missing directory costs one
//                   failed exec per command not found.
//
// NOT SETUID.  It is exec'd by getty, which init runs as root, so the privilege is already
// there; and the v7 reference tree ships /bin/login mode 0755 rather than 4755.  A setuid
// login would let any user re-run it from a shell, which is a door this system has no reason
// to open before cmd/su exists (task C6).
//
#include <fcntl.h>
#include <pwd.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#define SCPYN(a, b) strncpy(a, b, sizeof(a))

// No header in this tree declares these three; each source's head says the caller must.
char *crypt(const char *pw, const char *salt);
char *getpass(const char *prompt);
int ttyslot(void);

static char maildir[30]     = "/usr/spool/mail/";
static struct passwd nouser = { "", "nope" };
static struct sgttyb ttyb;
static struct utmp utmp;
static char minusnam[16] = "-";
static char homedir[64]  = "HOME=";
static char *envinit[]   = { homedir, "PATH=:/bin:/usr/bin", 0 };
static struct passwd *pwd;

static int stopmotd;

static void catchintr(int sig);
static void showmotd(void);

int main(int argc, char **argv)
{
    char *namep;
    char name[9]; // eight characters and a terminator; see the header
    int t, f, c, n;
    char *ttyn;

    // A minute to get logged in, and no way to interrupt out of the password prompt.
    alarm(60);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGINT, SIG_IGN);

    // v7's way of putting nice back to zero from wherever it was: down as far as it will go
    // (root may, kernel/sys4.c), then up, then to nothing.
    nice(-100);
    nice(20);
    nice(0);

    // The erase and kill characters the whole system agrees on -- the same two getty set, and
    // the same two ttychars() installs in the kernel.
    gtty(0, &ttyb);
    ttyb.sg_erase = '#';
    ttyb.sg_kill  = '@';
    stty(0, &ttyb);

    // Nothing but the terminal may be inherited across the exec below.
    for (t = 3; t < 20; t++)
        close(t);

    ttyn = ttyname(0);
    if (ttyn == 0)
        ttyn = "/dev/tty??";

loop:
    name[0] = 0;
    SCPYN(utmp.ut_name, "");
    if (argc > 1) {
        strncpy(name, argv[1], 8);
        name[8] = 0;
        argc    = 0;
    }
    while (name[0] == '\0') {
        // THE FLUSH IS NOT v7's AND IS NOT OPTIONAL.  This prompt has no newline, and stdout
        // on a terminal is LINE BUFFERED in this libc (lib/libc/stdio/flsbuf.c) where v7's
        // went fully unbuffered and spent a write(2) per character.  Line buffering is what
        // C11 asks for and the cheaper of the two, but it means a prompt sits in the buffer
        // until something ends the line -- and nothing does, because the next thing this
        // program executes is a read.  There is no tie between stdin and stdout here to do it
        // either: getchar() flushes nothing.  Without this the second and every later `login:'
        // is invisible and the terminal simply looks dead.  Nothing else in this file needs
        // it; every other message ends in \n.
        printf("login: ");
        fflush(stdout);
        n = 0;
        while ((c = getchar()) != '\n') {
            if (c == ' ')
                c = '_';
            if (c == EOF)
                exit(0);
            // §2: an int index, where v7 wrote `namep < utmp.ut_name+8'.
            if (n < 8)
                name[n++] = c;
        }
        name[n] = 0;
    }
    SCPYN(utmp.ut_name, name);

    setpwent();
    if ((pwd = getpwnam(name)) == NULL)
        pwd = &nouser;
    endpwent();

    // An unknown name still asks for a password, and still fails: nouser's encrypted field is
    // "nope", which no crypt() output can equal.  That is v7's arrangement and it is
    // deliberate -- a login that skipped the prompt would say which names exist.
    if (*pwd->pw_passwd != '\0') {
        namep = crypt(getpass("Password:"), pwd->pw_passwd);
        if (strcmp(namep, pwd->pw_passwd)) {
            printf("Login incorrect\n");
            goto loop;
        }
    }
    if (chdir(pwd->pw_dir) < 0) {
        printf("No directory\n");
        goto loop;
    }

    // Record the login.  ut_line is the terminal's name without the "/dev/": ttyn+1 skips the
    // leading slash so that strchr finds the second one.
    time(&utmp.ut_time);
    t = ttyslot();
    if (t > 0 && (f = open("/etc/utmp", O_WRONLY)) >= 0) {
        lseek(f, t * (off_t)sizeof(utmp), SEEK_SET);
        SCPYN(utmp.ut_line, strchr(ttyn + 1, '/') + 1);
        write(f, (char *)&utmp, sizeof(utmp));
        close(f);
    }
    if (t > 0 && (f = open("/usr/adm/wtmp", O_WRONLY)) >= 0) {
        lseek(f, 0, SEEK_END);
        write(f, (char *)&utmp, sizeof(utmp));
        close(f);
    }

    // THE TERMINAL IS HANDED OVER BEFORE THE PRIVILEGE IS DROPPED.  chown(2) is suser()-gated
    // (kernel/sys4.c), so these three lines are in this order or not at all.
    chown(ttyn, pwd->pw_uid, pwd->pw_gid);
    setgid(pwd->pw_gid);
    setuid(pwd->pw_uid);

    // Every account in this image's /etc/passwd leaves the shell field empty, so this is not
    // a fallback here but the ordinary path.
    if (*pwd->pw_shell == '\0')
        pwd->pw_shell = "/bin/sh";

    environ = envinit;
    strncat(homedir, pwd->pw_dir, sizeof(homedir) - 6);
    if ((namep = strrchr(pwd->pw_shell, '/')) == NULL)
        namep = pwd->pw_shell;
    else
        namep++;
    strncat(minusnam, namep, sizeof(minusnam) - 2);

    alarm(0);
    umask(02);
    showmotd();
    strncat(maildir, pwd->pw_name, sizeof(maildir) - 17);
    if (access(maildir, 4) == 0) {
        struct stat statb;
        stat(maildir, &statb);
        if (statb.st_size)
            printf("You have mail.\n");
    }
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    // argv[0] is "-sh": the leading minus is how a shell is told it is a LOGIN shell, so that
    // it reads the profile and prints a prompt (cmd/sh/main.c).
    execlp(pwd->pw_shell, minusnam, (char *)0);
    printf("No shell\n");
    exit(0);
}

//
// ^C during the message of the day stops it and nothing else: the login has already
// succeeded by the time showmotd() runs.
//
static void catchintr(int sig)
{
    (void)sig;
    signal(SIGINT, SIG_IGN);
    stopmotd++;
}

static void showmotd(void)
{
    FILE *mf;
    int c;

    signal(SIGINT, catchintr);
    if ((mf = fopen("/etc/motd", "r")) != NULL) {
        while ((c = getc(mf)) != EOF && stopmotd == 0)
            putchar(c);
        fclose(mf);
    }
    signal(SIGINT, SIG_IGN);
}
