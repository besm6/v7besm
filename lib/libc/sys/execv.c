/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * execv(path, argv) -- exec with the vector already built.
 *
 * The whole of it is the environment: exece() takes one and exec() does not, and this
 * is where the caller's own `environ' is handed over.  v7 named the gate execve; in
 * this tree it is sysent[59] `exece' (sys/exece.S).
 */

extern char **environ;

int exece(const char *path, char **argv, char **envp);

int execv(const char *path, char **argv)
{
    return exece(path, argv, environ);
}
