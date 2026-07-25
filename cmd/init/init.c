/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// /etc/init -- process 1: the single-user shell, /etc/rc, and a getty per line.
//
// The v7 program, unchanged in what it does; the changes are the ones C11 forces on a
// source written for a compiler that defaulted everything to `int'.  b6parse has no
// implicit int, no K&R parameter lists and no untyped `register i;', so:
//
//   - every function has a prototype and an explicit return type, and the ones only
//     this file calls are `static';
//   - the K&R definitions (`term(p) register struct tab *p;') became prototypes, and
//     the bare `register' declarations became plain ones -- the register keyword bought
//     nothing here, the back end allocates its own;
//   - merge() and reset() are HANDLERS, so C11 gives them the `void (*)(int)' shape
//     <signal.h> declares.  v7 could hand signal() a niladic function; C11 cannot, so
//     merge() takes the signal it ignores and main() calls it as merge(0);
//   - the flag arguments are spelled (O_RDWR, SEEK_END) rather than written as the
//     small integers v7 used, now that <fcntl.h> and <unistd.h> name them.
//
// v7's two loop macros are gone as well, written out at their six call sites: ALL and
// EVER each stood for an entire for() control clause, semicolons included, which is a
// shape neither a reader nor clang-format expects.
//
// The one change of substance is in <utmp.h>, not here: ut_time is a time_t, where v7
// wrote `long'.  Same word, but time() takes a `time_t *', so the `long' would not
// compile.
//
// WHAT IT DOES ON THIS MACHINE TODAY.  With no /etc/ttys on the root image, merge()
// returns as soon as the open fails and multiple() falls straight through -- so this
// init is exactly the single-user loop the port needs: shutdown, a shell on
// /dev/console, /etc/rc, and around again when the shell exits.  getty and the
// multi-user half wait on a terminal driver (kernel/TODO.md, task 29).
//
// It IS the image's /etc/init since task 25b, and the boot reaches /bin/sh's root prompt.
// kernel/test/boot asserts that prompt; kernel/test/console types at the shell and then
// sends ^D, which is what drives one whole turn of the loop above -- the shell exits,
// runcom() runs /etc/rc, the motd appears and single() prompts again.
//
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

// How many terminal lines the system can have.  itab below is exactly this long, and a
// bigger /etc/ttys is simply ignored past the hundredth line.
#define TABSIZ 100

// Every file name this program uses, as a global rather than a string in the code -- v7
// wrote them this way, and it costs one copy of each instead of one per use.
char shell[] = "/bin/sh";       // the shell run for the single-user prompt and for /etc/rc
char getty[] = "/etc/getty";    // the program that puts a login: prompt on a terminal
char minus[] = "-";             // argv[0] for the shell and for getty; see single()
char runc[]  = "/etc/rc";       // the boot script the shell runs on the way to multi-user
char ifile[] = "/etc/ttys";     // the list of terminal lines; merge() reads it
char utmp[]  = "/etc/utmp";     // who is logged in RIGHT NOW -- one record per line in use
char wtmpf[] = "/usr/adm/wtmp"; // the permanent log of logins and logouts, appended to
char ctty[]  = "/dev/console";  // the operator's terminal, where the single-user shell goes
char dev[]   = "/dev/";         // the directory every terminal's device file lives in

// One accounting record, reused as scratch by rmut() for both files above.  A global, not
// a local, because it is 5 words and v7 counted stack.
struct utmp wtmp;

// The line of /etc/ttys that rline() has just read.  Each line of that file is a flag
// character, then one more character, then the terminal's name -- for example "11console"
// -- and this is that line taken apart:
struct {
    char line[8]; // the terminal's name, as found in /dev: "console", "tty01", ...
    char comn;    // passed to getty as its argument; getty reads it as the line's speed
    char flag;    // '1' = run a getty here, '0' = leave the line alone
} line;

// One terminal line, and what init has running on it.  itab is the whole set: it is what
// init remembers between passes over /etc/ttys, and the only state this program keeps.
struct tab {
    char line[8]; // the terminal's name; an empty name means this slot is unused
    char comn;    // the getty argument for it, copied from /etc/ttys
    int pid;      // the getty's process id, or 0 when nothing is running here
} itab[TABSIZ];

int fi;       // the open /etc/ttys, while merge() and rline() are reading it
char tty[20]; // scratch: "/dev/" + a terminal name, built by maktty()

// Where a hangup goes.  setjmp() in main() records this spot; the SIGHUP handler reset()
// longjmp()s back to it, which abandons whatever init was doing and starts the cycle over.
jmp_buf sjbuf;

// Declared here so each can call the others in any order.  The five that main() calls are
// the stages of the cycle, in order; the rest are helpers.
static void shutdown(void);       // stop everything that is running
static void single(void);         // one shell on the console, and wait for it
static void runcom(void);         // run the /etc/rc boot script
static void merge(int sig);       // read /etc/ttys and start a getty per line
static void multiple(void);       // wait for gettys and restart them as they die
static void term(struct tab *p);  // kill what is on one line, and forget it
static int rline(void);           // read one line of /etc/ttys
static void maktty(char *lin);    // build "/dev/NAME" in tty[]
static int get(void);             // read one character of /etc/ttys
static void dfork(struct tab *p); // start a getty on one line
static void rmut(struct tab *p);  // record in utmp/wtmp that a line was logged out
static void reset(int sig);       // the SIGHUP handler

//
// Process 1, and the whole of what init does.  The kernel starts this program at boot and
// it must never exit -- if it did, the system would have nothing left running.
//
// So it is one endless cycle through the five stages, and the system's "state" is simply
// which of them init is in.  It ends up sitting in multiple(), waiting; a hangup on the
// console jumps back to the top and takes the machine down to single-user again, which is
// what the setjmp() below is for.
//
int main(void)
{
    // Mark this spot for the SIGHUP handler to jump back to, then arm that handler.  The
    // first time through, setjmp() just returns 0 and execution carries straight on.
    setjmp(sjbuf);
    signal(SIGHUP, reset);
    for (;;) {
        shutdown(); // nothing of the old state survives
        single();   // the operator gets a shell; the cycle waits here until it exits
        runcom();   // /etc/rc brings the system up: fsck, mounts, daemons
        merge(0);   // a getty on every enabled terminal -- users can log in now
        multiple(); // and stay here, respawning them, until there is nothing left to wait for
    }
}

//
// Take the system down to nothing: no user processes, no open files, an empty line table.
// Called at the top of every cycle, so init always builds up from a known state.
//
static void shutdown(void)
{
    int i;
    struct tab *p;

    // An interrupt (^C) must not be able to break out of this.
    signal(SIGINT, SIG_IGN);

    // Kill the getty or login shell on every line, and empty the table.
    for (p = &itab[0]; p < &itab[TABSIZ]; p++)
        term(p);

    // Now kill everything else in the system.  kill(-1, ...) means "every process I am
    // allowed to signal", and SIGKILL cannot be caught or ignored; five rounds because a
    // process killed mid-fork can leave a child behind.
    //
    // The alarm is the safety net.  wait() below blocks until a child dies, so a process
    // that somehow refuses to go would hang init forever; after 60 seconds SIGALRM fires,
    // reset() longjmps back to main(), and the cycle restarts instead of freezing.
    signal(SIGALRM, reset);
    alarm(60);
    for (i = 0; i < 5; i++)
        kill(-1, SIGKILL);

    // Collect the dead.  wait() returns -1 once there are no children left at all.
    while (wait((int *)0) != -1)
        ;

    // Made it -- cancel the alarm and put the signal back as it was.
    alarm(0);
    signal(SIGALRM, SIG_DFL);

    // Close every file init might still hold open.  Ten is simply more than it can have.
    for (i = 0; i < 10; i++)
        close(i);
}

//
// The single-user shell: one shell on the console, with nothing else running, and the
// system stays here until whoever is at the console exits it (^D).  This is where a
// damaged system gets repaired, and it is the state this port reaches today.
//
// The shape of it is the standard Unix way to run a program, and it appears three more
// times below.  fork() makes a copy of this process, and returns twice: 0 in the copy (the
// "child") and the child's process id in the original ("the parent").  The child rearranges
// what it needs and calls execl(), which REPLACES it with the new program -- so nothing
// after execl() runs unless the exec failed.  The parent meanwhile calls wait().
//
static void single(void)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        // ---- the child ----
        /*
                alarm(300);
        */
        // init ignores or catches these; the shell must have them back to normal, or a ^C
        // typed at the prompt would do nothing.
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        // Give the shell its three standard file descriptors, all on the console.  init
        // closed everything in shutdown(), so this open() gets descriptor 0 (standard
        // input), and each dup() copies it to the next free one -- 1 (standard output) and
        // 2 (standard error).  The shell inherits them across the exec.
        open(ctty, O_RDWR);
        dup(0);
        dup(0);

        // Become the shell.  Its argv[0] is "-", which is how a shell is told it is a
        // LOGIN shell: it reads the profile and prints a prompt.
        execl(shell, minus, (char *)0);
        exit(0); // only reached if the exec failed -- no /bin/sh, say
    }

    // ---- the parent ----  Wait for that particular child to exit.  Other children may
    // die first (a stray process from before), so keep waiting until this pid comes back.
    while (wait((int *)0) != pid)
        ;
}

//
// Run /etc/rc, the boot script: check and mount the file systems, clean out /tmp, start
// the daemons.  Same fork/exec/wait shape as single(), and init waits for the script to
// finish before it lets anyone log in.
//
static void runcom(void)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        // The script needs three descriptors but has no terminal to put them on -- this
        // stage runs unattended.  Opening the root DIRECTORY is v7's way of filling slots
        // 0, 1 and 2 with something harmless: a directory can be neither read nor written
        // by an ordinary program, so anything the script prints simply fails quietly.
        open("/", O_RDONLY);
        dup(0);
        dup(0);

        // "sh /etc/rc": argv[0] is the shell's own name this time, not "-", so it is an
        // ordinary shell running a script rather than a login shell.
        execl(shell, shell, runc, (char *)0);
        exit(0);
    }
    while (wait((int *)0) != pid)
        ;
}

//
// Multi-user: the state the system spends its life in.  There is a getty on every enabled
// terminal, and this loop does one thing -- when one of them dies, start another.
//
// That is the whole trick behind logging in.  getty prompts, login replaces it, the shell
// replaces login; they are all the same process, so when the user logs out that process
// finally exits, init notices here, and puts a fresh getty on the line for the next user.
//
static void multiple(void)
{
    struct tab *p;
    int pid;

    for (;;) {
        // Sleep until any child dies; wait() gives back which one.
        pid = wait((int *)0);

        // -1 means there are no children left to wait for -- nothing is running on any
        // line, so there is nothing to keep this state alive.  Back to main(), which
        // shuts down and offers the single-user shell again.  This is what happens on
        // this port today, since /etc/ttys is not on the disk yet and merge() started
        // no gettys at all.
        if (pid == -1)
            return;

        // Find whose line that was, note the logout, and start a getty there again.
        for (p = &itab[0]; p < &itab[TABSIZ]; p++)
            if (p->pid == pid || p->pid == -1) {
                rmut(p);
                dfork(p);
            }
    }
}

//
// Shut one line down: kill whatever is running on it and empty its slot in itab.  Used
// both when the system goes down and when a line disappears from /etc/ttys.
//
static void term(struct tab *p)
{
    if (p->pid != 0) { // 0 means nothing is running there, so there is nothing to kill
        rmut(p);       // record the logout first, while the line's name is still here
        kill(p->pid, SIGKILL);
    }
    p->pid     = 0; // the slot is now free ...
    p->line[0] = 0; // ... and an empty name is how the rest of the program sees that
}

//
// Read the next line of /etc/ttys into the `line' global above.  A line looks like
//
//      11console
//      ^^^^^^^^^
//      ||+------ the terminal's name, up to seven characters
//      |+------- the character handed to getty (which baud rate to try)
//      +-------- '1' to run a getty here, '0' to leave the line alone
//
// Returns 0 at the end of the file and 1 otherwise -- so 1 does NOT mean the line was
// good.  A line init cannot make sense of, or a terminal it cannot open, comes back
// marked '0' instead, and the caller skips it.  A bad entry must not stop init reading
// the rest of the file.
//
static int rline(void)
{
    int c, i;

    c = get();
    if (c < 0) // end of file: there is no next line
        return (0);
    if (c == 0) // an empty line
        goto bad;
    line.flag = c;

    c = get();
    if (c <= 0) // the line stopped before the getty character
        goto bad;
    line.comn = c;

    // The name.  Clear all eight bytes first, so a short name is padded with zeroes and
    // the comparisons in merge() and rmut() -- which always look at all eight -- agree.
    for (i = 0; i < 8; i++)
        line.line[i] = 0;
    for (i = 0; i < 7; i++) {
        c = get();
        if (c <= 0) // end of the line, or of the file
            break;
        line.line[i] = c;
    }

    // Anything past seven characters is skipped, up to the end of the line.
    while (c > 0)
        c = get();

    // Finally, can init actually use this terminal?  06 asks for read and write
    // permission on /dev/NAME; if the device file is missing or closed to init, there is
    // no point starting a getty on it.
    maktty(line.line);
    if (access(tty, 06) < 0)
        goto bad;
    return (1);

bad:
    line.flag = '0'; // "disabled" -- the caller will pass over this line
    return (1);
}

//
// Turn a terminal's name into the path of its device file: "console" -> "/dev/console".
// The answer is left in the global tty[], which every caller then uses immediately -- it
// is one shared scratch buffer, so nothing may hold on to it across another call.
//
// This is strcpy/strcat written out by hand, as v7 did in a program that links no library.
//
static void maktty(char *lin)
{
    int i, j;

    for (i = 0; dev[i]; i++) // copy "/dev/"
        tty[i] = dev[i];
    for (j = 0; lin[j]; j++) { // then the name after it
        tty[i] = lin[j];
        i++;
    }
    tty[i] = 0; // and the zero byte that ends every C string
}

//
// One character from /etc/ttys, with three possible answers rather than the usual two --
// which is why rline() above tests both `< 0' and `<= 0':
//
//      -1  end of the file (or it could not be read)
//       0  end of this line
//       c  the character itself
//
// There is no buffering: this is one read() system call per character, which v7 could
// afford for a file of a few dozen lines and which keeps the program library-free.
//
static int get(void)
{
    char b;

    if (read(fi, &b, 1) != 1)
        return (-1);
    if (b == '\n')
        return (0);
    return (b);
}

//
// Bring itab into line with /etc/ttys, and put a getty on every line that needs one.
// This is the densest function in the program, so, at length:
//
// The goal is not simply "start a getty everywhere".  merge() also runs while the system
// is up (it is the SIGINT handler -- see below), and then most lines already have a
// working getty or a logged-in user on them.  Those must be left ALONE.  Only lines that
// are new in the file get a getty started, and only lines that have vanished from it get
// killed.
//
// It does that by sorting itab into the file's own order as it reads.  q marks the border:
// everything before it has been matched to a line of the file already, in the order the
// file gave them, and everything from q on is still unsorted.  For each enabled line of
// the file, the loop finds that terminal's existing slot (or the first free one), swaps it
// down to q, and moves q past it.  When the file runs out, everything still beyond q is a
// line the file no longer mentions, and gets killed.
//
// The swap carries the pid along, and that is the point of the whole exercise: a terminal
// that was already running a getty arrives at its new position still running the same one.
//
// The signal argument exists only because C11 requires a handler to take one; merge() is
// its own SIGINT handler, so pressing the interrupt key on the console makes init re-read
// /etc/ttys.  That is v7's way of saying "I have edited the file, look again".
//
static void merge(int sig)
{
    struct tab *p, *q;
    int i;

    (void)sig;

    // Truncate /etc/utmp to nothing: no one is logged in as of now.  creat() makes an
    // empty file where one already exists, and the descriptor it hands back is of no use
    // here, so it is closed straight away.
    close(creat(utmp, 0644));

    // Ask to be called again on the next interrupt (v7 signal handlers are one-shot: the
    // disposition goes back to the default once the handler runs, so a handler that wants
    // to stay installed must say so each time).
    signal(SIGINT, merge);

    fi = open(ifile, O_RDONLY);
    if (fi < 0) // no /etc/ttys: nothing to start, and no lines are wanted
        return;

    q = &itab[0]; // the border starts at the beginning -- nothing is sorted yet
    while (rline()) {
        if (line.flag == '0') // disabled, or a line rline() could not use
            continue;

        // Find where this terminal belongs: its own slot if it already has one, otherwise
        // the first empty slot.
        for (p = &itab[0]; p < &itab[TABSIZ]; p++) {
            if (p->line[0] != 0) // an occupied slot: is it this terminal?
                for (i = 0; i < 8; i++)
                    if (p->line[i] != line.line[i])
                        goto contin; // no -- try the next slot

            // Here p is either this terminal's slot or an empty one.  If p is already
            // below the border it has been dealt with on an earlier pass; otherwise move
            // it down to q, exchanging with whatever sits there.
            if (p >= q) {
                i      = p->pid; // exchange the pids ...
                p->pid = q->pid;
                q->pid = i;
                for (i = 0; i < 8; i++) // ... give p the name that was at q ...
                    p->line[i] = q->line[i];
                p->comn = q->comn;
                for (i = 0; i < 8; i++) // ... and q the name from the file
                    q->line[i] = line.line[i];
                q->comn = line.comn;
                q++; // this slot is settled; the border moves up
            }
            break; // done with this line of the file
        contin:;
        }
    }
    close(fi);

    // Everything left above the border is a terminal /etc/ttys no longer lists.  Kill
    // whatever is on it and clear the slot.
    for (; q < &itab[TABSIZ]; q++)
        term(q);

    // And now the actual work: a getty on every line that has a name but nothing running.
    // Lines that already had one were never touched, which is exactly the intent.
    for (p = &itab[0]; p < &itab[TABSIZ]; p++)
        if (p->line[0] != 0 && p->pid == 0)
            dfork(p);
}

//
// Start a getty on one terminal -- the "d" is for the daemon it leaves behind.  Same
// fork/exec shape as single(), with one difference that matters: init does NOT wait here.
// It records the child's pid in the line's slot and returns, so that all the terminals'
// gettys run at once.  multiple() is where the waiting happens, for all of them together.
//
static void dfork(struct tab *p)
{
    int pid;

    pid = fork();
    if (pid == 0) {
        // ---- the child ----
        signal(SIGHUP, SIG_DFL);
        signal(SIGINT, SIG_DFL);

        // Hand the terminal to root and set its permissions: 0622 is "the owner may read
        // and write, anyone may write".  The last part is what lets other users send
        // messages to this terminal; login() narrows it once someone logs in.
        maktty(p->line);
        chown(tty, 0, 0);
        chmod(tty, 0622);

        // Standard input, output and error, all on this terminal -- see single().
        open(tty, O_RDWR);
        dup(0);
        dup(0);

        // Now reuse the tty[] buffer, which has done its job, as getty's one-character
        // argument: overwrite the first byte with the line's getty character and put the
        // string's terminating zero right after it.  So "/dev/tty01" becomes "1".
        tty[0] = p->comn;
        tty[1] = 0;
        execl(getty, minus, tty, (char *)0);
        exit(0); // the exec failed; give up on this line for now
    }

    // ---- the parent ----  Remember who is on this line, and carry straight on.
    p->pid = pid;
}

//
// Note that a terminal has been logged out.  "rm ut" -- remove from utmp.  Two files, and
// they are different in kind:
//
//   /etc/utmp     who is logged in right now.  One fixed-size record per line in use, and
//                 the record for this terminal has to be found and blanked in place.  This
//                 is what `who' reads.
//   /usr/adm/wtmp the permanent history, which is only ever appended to.  This is what
//                 `last' reads.
//
// Both are optional: if either file is missing the open() fails, and init simply carries
// on.  Accounting is not worth failing a boot over.
//
static void rmut(struct tab *p)
{
    int i, f;

    // ---- /etc/utmp: find this terminal's record and blank the user name ----
    f = open(utmp, O_RDWR);
    if (f >= 0) {
        while (read(f, (char *)&wtmp, sizeof(wtmp)) == sizeof(wtmp)) {
            for (i = 0; i < 8; i++) // is this record about our terminal?
                if (wtmp.ut_line[i] != p->line[i])
                    goto contin; // no -- on to the next record

            // Yes.  The read() just moved the file's position past this record, so step
            // back over it (a negative offset from the current position) to overwrite the
            // very record just read, rather than the one after it.
            lseek(f, -(off_t)sizeof(wtmp), SEEK_CUR);
            for (i = 0; i < 8; i++) // an empty name means "nobody is on this line"
                wtmp.ut_name[i] = 0;
            time(&wtmp.ut_time); // stamped with the moment of the logout
            write(f, (char *)&wtmp, sizeof(wtmp));
        contin:;
        }
        close(f);
    }

    // ---- /usr/adm/wtmp: append the same fact to the history ----
    f = open(wtmpf, O_WRONLY);
    if (f >= 0) {
        for (i = 0; i < 8; i++) {
            wtmp.ut_name[i] = 0; // no name: this record IS the logout
            wtmp.ut_line[i] = p->line[i];
        }
        time(&wtmp.ut_time);
        lseek(f, 0, SEEK_END); // seek to the end, so the write appends
        write(f, (char *)&wtmp, sizeof(wtmp));
        close(f);
    }
}

//
// The SIGHUP handler, and shutdown()'s SIGALRM handler as well.
//
// It does not return to whatever init was doing -- longjmp() abandons that outright and
// resumes at the setjmp() in main(), however deep in the program the signal arrived.  So a
// hangup on the console takes the machine back to single-user, and an alarm that goes off
// in shutdown() rescues init from waiting on a process that will not die.
//
// The argument is unused; C11 requires every handler to accept the signal number.
//
static void reset(int sig)
{
    (void)sig;
    longjmp(sjbuf, 1);
}
