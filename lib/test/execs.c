/*
 * execs -- the exec family, tested by making the program exec ITSELF five times.
 *
 * b6sim really does load a BESM-6 a.out on an exec (sys_exec in cmd/sim/syscall.cpp),
 * so each of the five wrappers can be made to hand control to the next incarnation of
 * this same program, with a different argv[1] to say which one just ran.  The chain is
 *
 *      start --execl--> l --execv--> v --execle--> e --execvp--> p --execlp--> lp
 *
 * and a wrapper that built its vector wrongly stops it dead: the run ends early, the
 * .expected file does not match, and the stage that failed is the last one printed.
 *
 * WHAT IS REALLY UNDER TEST is the claim in lib/README.md and sys/execl.c that the
 * variadic wrappers need no copy -- that the C calling convention has already laid
 * execl's arguments out as a contiguous, NULL-terminated char *[] and the va_list
 * simply points at it.  If that were wrong, argv would arrive short, long, shifted or
 * full of rubbish, and the checks each incarnation makes on its own argv would say so.
 *
 * execle carries an environment of its own, which is the other half: it has to find
 * the terminating null in that same argument list and take the word AFTER it.  Stage
 * `e' checks the environment it was given entry by entry, and stage `p' checks that it
 * survived one more exec -- because execvp goes through execv, which passes `environ'.
 *
 * fflush() BEFORE EVERY exec: the stdio buffer lives in guest memory and an exec throws
 * the whole image away, so anything still in it is simply lost.
 *
 * Nothing host-dependent reaches the output: the environment printed is the one this
 * program handed over, and argv[0] is what the harness typed.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int execl(const char *name, ...);
int execv(const char *path, char **argv);
int execle(const char *name, ...);
int execvp(const char *name, char **argv);
int execlp(const char *name, ...);

extern char **environ;

#define MISSING "./no-such-program"

static void ok(const char *what, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", what);
}

/* The environment execle hands over, and the only one stages `e' and `p' should see. */
static char *myenv[] = { "EXECS=chained", "SECOND=two", 0 };

/* Every incarnation checks the same three things about how it was entered. */
static void entered(const char *stage, int argc, char **argv, char **envp)
{
    printf("stage %s\n", stage);
    ok("argc is 2", argc == 2);
    ok("argv[1] is the stage", argc > 1 && strcmp(argv[1], stage) == 0);
    ok("argv[2] is null", argv[2] == 0);
    ok("envp is environ", envp == environ);
}

static void showenv(void)
{
    char **p;

    for (p = environ; *p != 0; p++)
        printf("env %s\n", *p);
    ok("getenv finds the handed-over value", strcmp(getenv("EXECS"), "chained") == 0);
}

int main(int argc, char **argv, char **envp)
{
    char *self = argv[0];
    char *av[3];
    const char *stage = argc > 1 ? argv[1] : "start";

    av[0] = self;
    av[2] = 0;

    if (strcmp(stage, "start") == 0) {
        printf("stage start\n");
        ok("argv[0] is not empty", self != 0 && *self != '\0');

        /*
         * The failure arm first, while there is still a program here to notice it.
         * A successful exec never returns, so reaching the next statement IS the
         * error report; errno is the only thing that says which error.
         */
        av[1] = "x";
        ok("execl of a missing file returns", execl(MISSING, MISSING, (char *)0) == -1);
        ok("execl set ENOENT", errno == ENOENT);
        ok("execv of a missing file returns", execv(MISSING, av) == -1);
        ok("execv set ENOENT", errno == ENOENT);
        ok("execvp of a missing file returns", execvp(MISSING, av) == -1);
        ok("execle of a missing file returns", execle(MISSING, MISSING, (char *)0, myenv) == -1);
        ok("execlp of a missing file returns", execlp(MISSING, MISSING, (char *)0) == -1);

        fflush(stdout);
        execl(self, self, "l", (char *)0);
        ok("execl replaced the image", 0);
        return 1;
    }

    if (strcmp(stage, "l") == 0) {
        entered(stage, argc, argv, envp);
        av[1] = "v";
        fflush(stdout);
        execv(self, av);
        ok("execv replaced the image", 0);
        return 1;
    }

    if (strcmp(stage, "v") == 0) {
        entered(stage, argc, argv, envp);
        fflush(stdout);
        execle(self, self, "e", (char *)0, myenv);
        ok("execle replaced the image", 0);
        return 1;
    }

    if (strcmp(stage, "e") == 0) {
        entered(stage, argc, argv, envp);
        showenv();
        av[1] = "p";
        fflush(stdout);
        execvp(self, av); /* a name with a '/' in it: no path search */
        ok("execvp replaced the image", 0);
        return 1;
    }

    if (strcmp(stage, "p") == 0) {
        entered(stage, argc, argv, envp);
        showenv(); /* execvp went through execv, which passes environ */
        fflush(stdout);
        execlp(self, self, "lp", (char *)0);
        ok("execlp replaced the image", 0);
        return 1;
    }

    entered(stage, argc, argv, envp);
    printf("done\n");
    return 0;
}
