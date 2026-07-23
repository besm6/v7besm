// UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details.

//
// getlogin() -- the login name recorded for this terminal in /etc/utmp, or NULL.
//
// Not getpwuid(getuid()): a user who has su'd or whose uid is shared by several logins
// is still sitting at one terminal, and the terminal is what utmp is indexed by.
// ttyslot() supplies that index, so this answers NULL for a process with no terminal.
//
// ONE FIX to v7.  It wrote `ubuf.ut_name[8] = ' '' -- one past the eight-character
// field -- as a sentinel for the scan that follows, relying on ut_time coming next in
// the struct and on nobody minding that it was clobbered.  The scan is bounded here
// instead, and the field is copied out into a buffer of its own so that the name is
// NUL-terminated without writing into the struct at all.
//
// ut_name is blank-padded and not terminated, which is why there is a scan to do.
//
// No header declares it; a caller declares it itself.
//
#include <utmp.h>

int ttyslot(void);
int open(const char *path, int mode);
int read(int fd, char *buf, int n);
int close(int fd);
int lseek(int fd, int off, int whence);

static const char UTMP[] = "/etc/utmp";

//
// ut_name is eight characters and is not NUL-terminated when it fills the field, so
// `name' is one longer.  The size is written out because b6cc will not take a
// `sizeof' in an array bound -- "Array size is not literal" -- and the assertion is
// what keeps it tied to <utmp.h>.
//
#define UT_NAMESZ 8

static struct utmp ubuf;
static char name[UT_NAMESZ + 1];

_Static_assert(sizeof ubuf.ut_name == UT_NAMESZ, "UT_NAMESZ must match struct utmp");

char *getlogin(void)
{
    int me, uf, i;

    if ((me = ttyslot()) == 0)
        return 0;
    if ((uf = open(UTMP, 0)) < 0)
        return 0;
    lseek(uf, me * (int)sizeof ubuf, 0);
    if (read(uf, (char *)&ubuf, sizeof ubuf) != sizeof ubuf) {
        close(uf);
        return 0;
    }
    close(uf);

    for (i = 0; i < (int)sizeof ubuf.ut_name; i++) {
        if (ubuf.ut_name[i] == ' ' || ubuf.ut_name[i] == '\0')
            break;
        name[i] = ubuf.ut_name[i];
    }
    name[i] = '\0';
    if (i == 0)
        return 0;
    return name;
}
