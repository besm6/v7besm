/*
 * environ -- the environment as crt0 finds it, and getenv over it.
 *
 * The exec gate lays argc, the argv[] vector, a null, the envp[] vector and a second
 * null at the fixed base 070000; crt0.s computes `environ' from argc alone, by
 * stepping past the argv[] terminator (kernel/sys1.c, cmd/sim/machine.cpp,
 * lib/libc/csu/crt0.s).  main()'s third parameter and the global must therefore be the
 * same vector, which is the first thing checked below.
 *
 * WHAT THE .expected FILE CAN SAY.  b6sim hands the guest a curated whitelist of the
 * host's variables (ENV_WHITELIST in cmd/sim/session.cpp), so neither the names nor
 * even their number is the same on two machines.  So nothing here prints a name or a
 * value: the program checks getenv against the vector it was given, entry by entry,
 * and reports only counts and verdicts.  That is the stronger test anyway -- it holds
 * getenv to the whole environment rather than to the one variable a test author
 * happened to think of.
 *
 * The prefix case at the end is what nvmatch() exists for: a name that is a proper
 * prefix of a real one must NOT match, or getenv("PAT") would return PATH's value.
 */
#include <string.h>

int write(int fd, char *buf, int n);
char *getenv(const char *name);
char *index(const char *sp, char c);

extern char **environ;

/* One string to the standard output, without stdio (phase 4). */
static void put(char *s)
{
    write(1, s, strlen(s));
}

static void putdigit(int d)
{
    char *p = "0123456789";

    while (d-- > 0)
        p++;
    write(1, p, 1);
}

static void putnum(int v)
{
    if (v < 0) {
        put("-");
        v = -v;
    }
    if (v >= 10)
        putnum(v / 10);
    putdigit(v % 10);
}

static void ok(char *what, int cond)
{
    put(cond ? "ok   " : "FAIL ");
    put(what);
    put("\n");
}

int main(int argc, char **argv, char **envp)
{
    char name[64];
    char *eq, *v;
    int n, bad, noeq, i;

    ok("environ is main's envp", environ == envp);

    /*
     * Every entry is "NAME=value".  Copy the name out, ask getenv for it, and require
     * the answer to be the very byte after the `=' in THAT entry -- not merely a string
     * that compares equal, which a second copy of the value would also satisfy.
     */
    n = bad = noeq = 0;
    for (i = 0; environ[i] != 0; i++) {
        eq = index(environ[i], '=');
        if (eq == 0) {
            noeq++;
            continue;
        }
        if (eq - environ[i] >= (int)sizeof(name)) /* absurdly long: skip, don't fail */
            continue;
        strncpy(name, environ[i], eq - environ[i]);
        name[eq - environ[i]] = 0;
        v                     = getenv(name);
        if (v != eq + 1)
            bad++;
        n++;
    }
    ok("the environment is not empty", n > 0);
    ok("every entry is NAME=value", noeq == 0);
    ok("getenv found every one of them", bad == 0);

    /*
     * The COUNT is deliberately not printed.  It is whatever of the whitelist the host
     * happens to have set, and it changes between a run under make -- which exports
     * MAKEFLAGS -- and the same run by hand, so a number here would make the .expected
     * file a record of one machine's shell rather than of getenv's behaviour.
     */

    /* A name that cannot be there. */
    ok("getenv of an absent name", getenv("NO_SUCH_VARIABLE_AT_ALL") == 0);
    ok("getenv of the empty name", getenv("") == 0);

    /*
     * A proper prefix of the first entry's name: nvmatch must reject it.  Built from
     * whatever the environment actually holds, so it needs no PATH to exist.
     */
    if (environ[0] != 0) {
        eq = index(environ[0], '=');
        if (eq != 0 && eq - environ[0] > 1) {
            strncpy(name, environ[0], eq - environ[0] - 1);
            name[eq - environ[0] - 1] = 0;
            ok("getenv rejects a prefix of a real name", getenv(name) == 0);
        }
    }

    put("done\n");
    return 0;
}
