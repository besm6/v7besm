/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Turn the trailing run of `X' in as into a name no file has yet.
 *
 * The digits of the process id fill the Xs from the right, then a single letter is
 * tried in the first of them until access() says the name is free.  Returns as, or
 * "/" -- a name no creat() can take -- when all 26 letters are spoken for.  v7's
 * bargain, kept as it stands: it is a race, and every caller has always known it.
 */
int access(const char *path, int mode);
int getpid(void);

char *mktemp(char *as)
{
    char *s;
    unsigned pid;
    int i;

    pid = getpid();
    s   = as;
    while (*s++)
        ;
    s--;
    while (*--s == 'X') {
        *s = (pid % 10) + '0';
        pid /= 10;
    }
    s++;
    i = 'a';
    while (access(as, 0) != -1) {
        if (i == 'z')
            return "/";
        *s = i++;
    }
    return as;
}
