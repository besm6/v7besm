// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.
//
// mail(1) for the BESM-6, task C28.  README.md is the account: no remote mail, the
// privilege drop, and why remail() forks.

#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// copylet flags
#define ZAP      3 // zap header and trailing empty line
#define ORDINARY 2
#define FORWARD  4

#define LSIZE  256
#define MAXLET 300 // maximum number of letters

#define MAILMODE 0133 // umask: 0666 & ~0133 == 0644, v7's ~0644 by another name

_Static_assert(BUFSIZ % 6 == 0, "a stdio buffer must be a whole number of words");

static char line[LSIZE];
static char resp[LSIZE];
static char sobuf[BUFSIZ]; // not an automatic: 512 words of a 4096-word stack

// +2: copymt() writes letadr[nlet] after letadr[nlet++], copyback() writes letadr[++nlet].
static off_t letadr[MAXLET + 2];
static char letchg[MAXLET + 1];
static int nlet = 0;

static char lfil[LSIZE];
static char lettmp[] = "/tmp/maXXXXX";
static char maildir[] = "/usr/spool/mail/";
static char mailfile[FILENAME_MAX] = "/usr/spool/mail/";
static char dead[]    = "dead.letter";
static char forwmsg[] = " forwarded\n";
static char from[]    = "From ";

static const char *curlock;
static int lockerror;
static FILE *tmpf;
static FILE *malf;
static char myname[16];
static time_t iop;
static int senderr;
static int locked;
static int changed;
static int forward;
static int privfile; // -f named a private file: do not touch its mode
static int flgf;
static int flgp;
static int delflg = 1;
static jmp_buf sjbuf;

// v7 caught 0..19; catching a fault turned it into a longjmp back to the `? ' prompt.
#define NCATCH 4
static const int catchsig[NCATCH] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };

static void printmail(int argc, char **argv);
static void sendmail(int argc, char **argv);
static void copyback(void);
static void copymt(FILE *f1, FILE *f2);
static void copylet(int n, FILE *f, int type);
static int isfrom(const char *lp);
static int remail(int n, const char *name);
static int deliver(int n, const char *name);
static void onsig(int sig);
static void setsig(int i, void (*f)(int));
static _Noreturn void done(void);
static void mboxlock(const char *file);
static void mboxunlock(void);
static void catpath(char *to, int size, const char *a, const char *b);
static char *getarg(char *s, int size, char *p);

int main(int argc, char **argv)
{
    int i;

    setbuf(stdout, sobuf);
    mktemp(lettmp);
    unlink(lettmp);

    // <pwd.h>'s buffer is shared and this name outlives the call.  Copy it out.
    {
        char *p = getlogin();
        if (p == NULL) {
            struct passwd *pwent = getpwuid(getuid());
            p                    = pwent == NULL ? "???" : pwent->pw_name;
        }
        strncpy(myname, p, sizeof(myname) - 1);
    }

    if (setjmp(sjbuf))
        done();
    for (i = 0; i < NCATCH; i++)
        setsig(catchsig[i], onsig);
    tmpf = fopen(lettmp, "w");
    if (tmpf == NULL) {
        fprintf(stderr, "mail: cannot open %s for writing\n", lettmp);
        senderr++;
        done();
    }
    // v7's argv[0][0]=='r' rmail dispatch is gone with uux.
    if (argc == 1 || argv[1][0] == '-')
        printmail(argc, argv);
    else
        sendmail(argc, argv);
    done();
}

static void setsig(int i, void (*f)(int))
{
    if (signal(i, SIG_IGN) != SIG_IGN)
        signal(i, f);
}

static void printmail(int argc, char **argv)
{
    // THE PRIVILEGE DROP.  Nothing below may run before it: everything here is chosen by
    // whoever is typing.  Permanent -- setuid(2) moves the real uid and there is no saved id.
    setuid(getuid());

    static int i, j, print; // read after a longjmp: C11 wants static or volatile
    int flg;
    char *p;

    catpath(mailfile, sizeof(mailfile), maildir, myname);
    for (; argc > 1; argv++, argc--) {
        if (argv[1][0] == '-') {
            if (argv[1][1] == 'q')
                delflg = 0;
            else if (argv[1][1] == 'p') {
                flgp++;
                delflg = 0;
            } else if (argv[1][1] == 'f') {
                if (argc >= 3) {
                    if (strlen(argv[2]) >= sizeof(mailfile)) {
                        // Diagnose, not truncate: a short path names some other file.
                        fprintf(stderr, "mail: file name too long\n");
                        senderr++;
                        done();
                    }
                    strcpy(mailfile, argv[2]);
                    privfile = 1;
                    argv++;
                    argc--;
                }
            } else if (argv[1][1] == 'r') {
                forward = 1;
            } else {
                fprintf(stderr, "mail: unknown option %c\n", argv[1][1]);
                senderr++;
                done();
            }
        } else
            break;
    }
    malf = fopen(mailfile, "r");
    if (malf == NULL) {
        fprintf(stdout, "No mail.\n");
        return;
    }
    mboxlock(mailfile);
    copymt(malf, tmpf);
    fclose(malf);
    fclose(tmpf);
    mboxunlock();
    tmpf = fopen(lettmp, "r");

    changed = 0;
    print   = 1;
    for (i = 0; i < nlet;) {
        j = forward ? i : nlet - i - 1;
        if (setjmp(sjbuf)) {
            print = 0;
        } else {
            if (print)
                copylet(j, stdout, ORDINARY);
            print = 1;
        }
        if (flgp) {
            i++;
            continue;
        }
        setjmp(sjbuf);
        fprintf(stdout, "? ");
        fflush(stdout);
        if (fgets(resp, LSIZE, stdin) == NULL)
            break;
        switch (resp[0]) {

        default:
            fprintf(stderr, "usage\n");
            // fall through to the summary
        case '?':
            print = 0;
            fprintf(stderr, "q\tquit\n");
            fprintf(stderr, "x\texit without changing mail\n");
            fprintf(stderr, "p\tprint\n");
            fprintf(stderr, "s[file]\tsave (default mbox)\n");
            fprintf(stderr, "w[file]\tsame without header\n");
            fprintf(stderr, "-\tprint previous\n");
            fprintf(stderr, "d\tdelete\n");
            fprintf(stderr, "+\tnext (no delete)\n");
            fprintf(stderr, "m user\tmail to user\n");
            fprintf(stderr, "! cmd\texecute cmd\n");
            break;

        case '+':
        case 'n':
        case '\n':
            i++;
            break;
        case 'x':
            changed = 0;
            // fall through
        case 'q':
            goto donep;
        case 'p':
            break;
        case '^':
        case '-':
            if (--i < 0)
                i = 0;
            break;
        case 'y':
        case 'w':
        case 's':
            flg = 0;
            if (resp[1] != '\n' && resp[1] != ' ') {
                fprintf(stdout, "illegal\n");
                print = 0;
                continue;
            }
            if (resp[1] == '\n' || resp[1] == '\0')
                catpath(resp + 1, LSIZE - 1, "mbox", "");
            for (p = resp + 1; (p = getarg(lfil, sizeof(lfil), p)) != NULL;) {
                malf = fopen(lfil, "a");
                if (malf == NULL) {
                    fprintf(stdout, "mail: cannot append to %s\n", lfil);
                    flg++;
                    continue;
                }
                copylet(j, malf, resp[0] == 'w' ? ZAP : ORDINARY);
                fclose(malf);
            }
            if (flg)
                print = 0;
            else {
                letchg[j] = 'd';
                changed++;
                i++;
            }
            break;
        case 'm':
            flg = 0;
            if (resp[1] == '\n' || resp[1] == '\0') {
                i++;
                continue;
            }
            if (resp[1] != ' ') {
                fprintf(stdout, "invalid command\n");
                print = 0;
                continue;
            }
            for (p = resp + 1; (p = getarg(lfil, sizeof(lfil), p)) != NULL;)
                if (!remail(j, lfil)) // couldn't send it
                    flg++;
            if (flg)
                print = 0;
            else {
                letchg[j] = 'd';
                changed++;
                i++;
            }
            break;
        case '!':
            system(resp + 1);
            fprintf(stdout, "!\n");
            print = 0;
            break;
        case 'd':
            letchg[j] = 'd';
            changed++;
            i++;
            if (resp[1] == 'q')
                goto donep;
            break;
        }
    }
donep:
    delflg = 0; // sjbuf is this frame's; past here onsig() must finish, not longjmp
    if (changed)
        copyback();
}

// copy temp or whatever back to /usr/spool/mail
static void copyback(void)
{
    int i, n, c;
    int new = 0;
    struct stat stbuf;

    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    mboxlock(mailfile);
    stat(mailfile, &stbuf);
    if (stbuf.st_size != letadr[nlet]) { // new mail has arrived
        malf = fopen(mailfile, "r");
        if (malf == NULL) {
            fprintf(stdout, "mail: can't re-read %s\n", mailfile);
            done();
        }
        fseek(malf, letadr[nlet], 0);
        fclose(tmpf);
        tmpf = fopen(lettmp, "a");
        fseek(tmpf, letadr[nlet], 0);
        while ((c = fgetc(malf)) != EOF)
            fputc(c, tmpf);
        fclose(malf);
        fclose(tmpf);
        tmpf = fopen(lettmp, "r");
        if (nlet < MAXLET)
            letadr[++nlet] = stbuf.st_size;
        new = 1;
    }
    malf = fopen(mailfile, "w");
    if (malf == NULL) {
        // v7 printed lfil, whatever the last `s' named.
        fprintf(stderr, "mail: can't rewrite %s\n", mailfile);
        done();
    }
    n = 0;
    for (i = 0; i < nlet; i++)
        if (letchg[i] != 'd') {
            copylet(i, malf, ORDINARY);
            n++;
        }
    fclose(malf);
    if (new)
        fprintf(stdout, "new mail arrived\n");
    mboxunlock();
}

// copy mail (f1) to temp (f2)
static void copymt(FILE *f1, FILE *f2)
{
    off_t nextadr;

    nlet = nextadr = 0;
    letadr[0]      = 0;
    while (fgets(line, LSIZE, f1) != NULL) {
        if (isfrom(line)) {
            if (nlet >= MAXLET) {
                fprintf(stderr, "mail: more than %d letters\n", MAXLET);
                senderr++;
                done();
            }
            letadr[nlet++] = nextadr;
        }
        nextadr += strlen(line);
        fputs(line, f2);
    }
    letadr[nlet] = nextadr; // last plus 1
}

static void copylet(int n, FILE *f, int type)
{
    int ch = '\n', k;

    fseek(tmpf, letadr[n], 0);
    k = letadr[n + 1] - letadr[n];
    while (k-- > 1 && (ch = fgetc(tmpf)) != '\n')
        if (type != ZAP)
            fputc(ch, f);
    if (type == FORWARD)
        fputs(forwmsg, f);
    else if (type == ORDINARY)
        fputc(ch, f);
    while (k-- > 1)
        fputc(ch = fgetc(tmpf), f);
    if (type != ZAP || ch != '\n')
        fputc(fgetc(tmpf), f);
}

static int isfrom(const char *lp)
{
    const char *p;

    for (p = from; *p;)
        if (*lp++ != *p++)
            return 0;
    return 1;
}

static void sendmail(int argc, char **argv)
{
    time(&iop);
    fprintf(tmpf, "%s%s %s", from, myname, ctime(&iop));
    flgf = 1;
    while (fgets(line, LSIZE, stdin) != NULL) {
        if (line[0] == '.' && line[1] == '\n')
            break;
        if (isfrom(line))
            fputs(">", tmpf);
        fputs(line, tmpf);
        flgf = 0;
    }
    fputs("\n", tmpf);
    nlet      = 1;
    letadr[0] = 0;
    letadr[1] = ftell(tmpf);
    fclose(tmpf);
    if (flgf)
        return;
    tmpf = fopen(lettmp, "r");
    if (tmpf == NULL) {
        fprintf(stderr, "mail: cannot reopen %s for reading\n", lettmp);
        return;
    }
    while (--argc > 0)
        if (!deliver(0, *++argv)) // couldn't send to him
            senderr++;
    if (senderr) {
        setuid(getuid());
        malf = fopen(dead, "w");
        if (malf == NULL) {
            fprintf(stdout, "mail: cannot open %s\n", dead);
            fclose(tmpf);
            return;
        }
        copylet(0, malf, ZAP);
        fclose(malf);
        fprintf(stdout, "Mail saved in %s\n", dead);
    }
    fclose(tmpf);
}

// Hand letter n on by re-execing /bin/mail: this process dropped privilege in printmail()
// and can no longer write another user's mailbox.  A fresh setuid mail can.
static int remail(int n, const char *name)
{
    FILE *rmf;
    char cmd[LSIZE + 8];
    int pid, sts, i;

    if (*name == '\0') {
        fprintf(stdout, "null name\n");
        return 0;
    }
    if (strchr(name, '!') != NULL) {
        fprintf(stdout, "mail: no remote mail: %s\n", name);
        return 0;
    }
    if ((pid = fork()) == -1) {
        fprintf(stderr, "mail: can't create proc for remote\n");
        return 0;
    }
    if (pid) {
        while (wait(&sts) != pid) {
            if (wait(&sts) == -1)
                return 0;
        }
        return !sts;
    }
    // Not onsig() here: a SIGPIPE would longjmp and done() would unlink the parent's temp.
    for (i = 0; i < NCATCH; i++)
        signal(catchsig[i], SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    setuid(getuid());
    sprintf(cmd, "mail %s", name);
    if ((rmf = popen(cmd, "w")) == NULL)
        _exit(1);
    copylet(n, rmf, FORWARD);
    pclose(rmf);
    _exit(0); // not exit: it would flush the buffer inherited from the parent
}

// send letter n to name
static int deliver(int n, const char *name)
{
    char file[FILENAME_MAX];
    int mask, uid, gid;
    struct passwd *pw;

    if (strchr(name, '!') != NULL) {
        fprintf(stdout, "mail: no remote mail: %s\n", name);
        return 0;
    }
    if ((pw = getpwnam(name)) == NULL) {
        fprintf(stdout, "mail: can't send to %s\n", name);
        return 0;
    }
    // <pwd.h>'s buffer is shared; take the scalars before anything else can call getpwent.
    uid = pw->pw_uid;
    gid = pw->pw_gid;

    catpath(file, sizeof(file), maildir, name);
    mask = umask(MAILMODE);
    malf = fopen(file, "a");
    umask(mask);
    if (malf == NULL) {
        fprintf(stdout, "mail: cannot append to %s\n", file);
        return 0;
    }
    mboxlock(file);
    chown(file, uid, gid);
    copylet(n, malf, ORDINARY);
    fclose(malf);
    mboxunlock();
    return 1;
}

static void onsig(int sig)
{
    setsig(sig, onsig); // this kernel resets a caught signal to SIG_DFL on delivery
    fprintf(stderr, "\n");
    if (delflg)
        longjmp(sjbuf, 1);
    done();
}

static _Noreturn void done(void)
{
    if (!lockerror)
        mboxunlock();
    unlink(lettmp);
    exit(senderr + lockerror);
}

// The lock is the mailbox's user-execute bit; stat and chmod are not atomic between them,
// which is the race mail(1) BUGS admits to.  There is no flock(2) here.
static void mboxlock(const char *file)
{
    struct stat stbuf;

    // -f names a private mbox: no other mail process looks at it, so do not chmod it.
    if (locked || flgf || privfile)
        return;
    if (stat(file, &stbuf) < 0)
        return;
    if (stbuf.st_mode & 01) { // user x bit is the lock
        if (stbuf.st_ctime + 60 >= time((time_t *)0)) {
            fprintf(stderr, "%s busy; try again in a minute\n", file);
            lockerror++;
            done();
        }
    }
    locked  = stbuf.st_mode & ~01;
    curlock = file;
    chmod(file, stbuf.st_mode | 01);
}

static void mboxunlock(void)
{
    if (locked)
        chmod(curlock, locked);
    locked = 0;
}

static void catpath(char *to, int size, const char *a, const char *b)
{
    int i, j;

    j = 0;
    for (i = 0; a[i] && j < size - 1; i++)
        to[j++] = a[i];
    for (i = 0; b[i] && j < size - 1; i++)
        to[j++] = b[i];
    to[j] = 0;
}

// copy p... into s, update p
static char *getarg(char *s, int size, char *p)
{
    int n = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\n' || *p == '\0')
        return NULL;
    while (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0') {
        if (n < size - 1)
            s[n++] = *p;
        p++;
    }
    s[n] = '\0';
    return p;
}
