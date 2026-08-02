/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

//
// tsort -- topological sort.
//
//      tsort [ file ]
//
// Input is a sequence of pairs of blank-free strings.  A pair of different items is a
// directed edge in the graph; a pair of identical items merely says the node is present.
// Output is an ordering of the items consistent with the graph.  One of task C5f's seven
// (../TODO.md).
//
// No §2 -- not a pointer relational in the file -- no `long', no `%D', no table indexed by a
// character, and nothing that assumes sixteen bits.  Three things had to change.
//
// index() COLLIDES WITH libc's index(3), and it is §1's rename-on-sight in its most
// dangerous form.  v7's returns a `struct nodelist *' where lib/libc/gen/index.c returns a
// `char *', and b6ld pulls an archive member only for a symbol still undefined -- so the
// program's own definition would have satisfied the program's own calls and the collision
// would have waited silently for whatever wanted the real one.  chmod.c's abs() and
// chown.c's isnumber() are the precedent.  It is nodefor() here.
//
// error() WAS CALLED WITH ONE ARGUMENT FROM TWO PLACES.  findloop()'s `error("error 1")' and
// `error("error 2")' pass one string to a function declared with two, so the second reached
// fprintf as whatever the frame happened to hold.  A C11 constraint violation, and one that
// only fires on a cyclic graph -- which is the case this program exists to diagnose.
//
// AND `%s' WITH NO FIELD WIDTH INTO char[50].  tsort's caller is lorder(1), which feeds it
// ARCHIVE MEMBER NAMES, and a member name here may be 255 bytes (cross/besm6/ar.h's
// ARMAXNAME) where a PDP-11 archive's was 14.  So the buffer that was generous upstream is
// a fifth of what this machine's own archiver can hand it, and the overrun is into the
// frame.  The fscanf is gone: getname() reads a blank-delimited token with a bound and
// DIAGNOSES a name that does not fit, where `%255s' would have split it in two and
// silently re-paired the whole rest of the file (§6 -- a bound that produces a plausible
// wrong answer is worse than one that stops).  It also makes the blank set explicit, so a
// byte above 0177 is a name character here rather than whatever a table says (§11).
//
// NOT SETUID: it opens what the caller could open itself.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Long enough for an archive member name, which is what lorder(1) feeds this program:
// cross/besm6/ar.h's ARMAXNAME is 255 bytes.  A guest program cannot include that header --
// it is the host cross tree -- so the number is written here with its source named.
#define NAMESIZE 256

enum liveness { DEAD, LIVE, ONCE, TWICE };

//
//	the nodelist always has an empty element at the end to
//	make it easy to grow in natural order
//
struct nodelist {
    struct nodelist *nextnode;
    struct predlist *inedges;
    char *name;
    enum liveness live;
};

//
//	a predecessor list tells all the immediate
//	predecessors of a given node
//
struct predlist {
    struct predlist *nextpred;
    struct nodelist *pred;
};

static struct nodelist firstnode = { NULL, NULL, NULL, DEAD };

static const char *empty = "";

static int getname(FILE *f, char *buf);
static struct nodelist *nodefor(const char *s);
static struct nodelist *findloop(void);
static int present(const struct nodelist *i, const struct nodelist *j);
static int anypred(const struct nodelist *i);
static _Noreturn void error(const char *s, const char *t);
static void note(const char *s, const char *t);

//
//	the first for loop reads in the graph,
//	the second prints out the ordering
//
int main(int argc, char **argv)
{
    struct predlist *t;
    FILE *input = stdin;
    struct nodelist *i, *j;
    int x;
    char precedes[NAMESIZE], follows[NAMESIZE];

    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (input == NULL)
            error("cannot open ", argv[1]);
    }
    for (;;) {
        // v7 wrote fscanf("%s%s") with no width, into fifty bytes, for names lorder(1) takes
        // out of an archive -- where a member name may be 255.
        if (!getname(input, precedes))
            break;
        if (!getname(input, follows))
            error("odd data", empty);
        i = nodefor(precedes);
        j = nodefor(follows);
        if (i == j || present(i, j))
            continue;
        t = (struct predlist *)malloc(sizeof(struct predlist));
        if (t == NULL)
            error("too many items", empty);
        t->nextpred = j->inedges;
        t->pred     = i;
        j->inedges  = t;
    }
    for (;;) {
        x = 0; // anything LIVE on this sweep?
        for (i = &firstnode; i->nextnode != NULL; i = i->nextnode) {
            if (i->live == LIVE) {
                x = 1;
                if (!anypred(i))
                    break;
            }
        }
        if (x == 0)
            break;
        if (i->nextnode == NULL)
            i = findloop();
        printf("%s\n", i->name);
        i->live = DEAD;
    }
    return 0;
}

//
// Read one blank-delimited token into buf, which is NAMESIZE bytes.  Answers 0 at end of
// file.  A token that does not fit is a diagnostic rather than a truncation: `%255s' would
// have made two names out of one and re-paired every line after it.
//
static int getname(FILE *f, char *buf)
{
    int c, n;

#define BLANK(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\f' || (c) == '\v')

    while ((c = getc(f)) != EOF && BLANK(c))
        ;
    if (c == EOF)
        return 0;
    n = 0;
    while (c != EOF && !BLANK(c)) {
        if (n >= NAMESIZE - 1) {
            buf[n] = '\0';
            error("name too long: ", buf);
        }
        buf[n++] = c;
        c        = getc(f);
    }
    buf[n] = '\0';
    return 1;
}

//
//	is i present on j's predecessor list?
//
static int present(const struct nodelist *i, const struct nodelist *j)
{
    const struct predlist *t;

    for (t = j->inedges; t != NULL; t = t->nextpred)
        if (t->pred == i)
            return 1;
    return 0;
}

//
//	is there any live predecessor for i?
//
static int anypred(const struct nodelist *i)
{
    const struct predlist *t;

    for (t = i->inedges; t != NULL; t = t->nextpred)
        if (t->pred->live == LIVE)
            return 1;
    return 0;
}

//
//	turn a string into a node pointer.  v7 called this index(), which is libc's.
//
static struct nodelist *nodefor(const char *s)
{
    struct nodelist *i;
    char *t;

    for (i = &firstnode; i->nextnode != NULL; i = i->nextnode)
        if (strcmp(s, i->name) == 0)
            return i;
    t            = malloc(strlen(s) + 1);
    i->nextnode  = (struct nodelist *)malloc(sizeof(struct nodelist));
    if (i->nextnode == NULL || t == NULL)
        error("too many items", empty);
    strcpy(t, s);
    i->name                = t;
    i->live                = LIVE;
    i->nextnode->nextnode  = NULL;
    i->nextnode->inedges   = NULL;
    i->nextnode->name      = NULL;
    i->nextnode->live      = DEAD;
    return i;
}

static _Noreturn void error(const char *s, const char *t)
{
    note(s, t);
    exit(1);
}

static void note(const char *s, const char *t)
{
    fprintf(stderr, "tsort: %s%s\n", s, t);
}

//
//	given that there is a cycle, find some
//	node in it
//
static struct nodelist *findloop(void)
{
    struct nodelist *i, *j;
    struct predlist *p;

    for (i = &firstnode; i->nextnode != NULL; i = i->nextnode)
        if (i->live == LIVE)
            break;
    note("cycle in reverse order", empty);
    while (i->live == LIVE) {
        i->live = ONCE;
        for (p = i->inedges;; p = p->nextpred) {
            if (p == NULL)
                error("error 1", empty); // v7 passed one argument to a function of two
            i = p->pred;
            if (i->live != DEAD)
                break;
        }
    }
    while (i->live == ONCE) {
        i->live = TWICE;
        note(i->name, empty);
        for (p = i->inedges;; p = p->nextpred) {
            if (p == NULL)
                error("error 2", empty);
            i = p->pred;
            if (i->live != DEAD)
                break;
        }
    }
    for (j = &firstnode; j->nextnode != NULL; j = j->nextnode)
        if (j->live != DEAD)
            j->live = LIVE;
    return i;
}
