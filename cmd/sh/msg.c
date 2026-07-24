/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */
/* Changes: Copyright (c) 1999 Robert Nordier. All rights reserved. */

//
// Every string constant and keyword table the shell has, in one place -- v7's
// arrangement, kept.  MSG is `char[]' and SYSTAB is `struct sysnod[]' (mode.h), so
// these are the definitions that complete the incomplete array types defs.h declares.
//
#include "defs.h"
#include "sym.h"

// Bourne's own version stamp, kept as it was.  Nothing reads it; it is here so that the
// string appears in the binary, which is how one used to tell builds apart.
MSG version = "\nVERSION sys137	DATE 1978 Nov 6 14:29:22\n";

/* error messages */
MSG badopt  = "bad option(s)";
MSG mailmsg = "you have mail\n";
MSG nospace = "no space";
MSG synmsg  = "syntax error";

MSG badnum     = "bad number";
MSG badparam   = "parameter not set";
MSG badsub     = "bad substitution";
MSG badcreate  = "cannot create";
MSG illegal    = "illegal io";
MSG restricted = "restricted";
MSG piperr     = "cannot make pipe";
MSG badopen    = "cannot open";
MSG coredump   = " - core dumped";
MSG arglist    = "arg list too long";
MSG txtbsy     = "text busy";
MSG toobig     = "too big";
MSG badexec    = "cannot execute";
MSG notfound   = "not found";
MSG badfile    = "bad file number";
MSG badshift   = "cannot shift";
MSG baddir     = "bad directory";
MSG badtrap    = "bad trap";
MSG wtfailed   = "is read only";
MSG notid      = "is not an identifier";

/* built in names */
MSG pathname = "PATH";
MSG homename = "HOME";
MSG mailname = "MAIL";
MSG fngname  = "FILEMATCH";
MSG ifsname  = "IFS";
MSG ps1name  = "PS1";
MSG ps2name  = "PS2";

/* string constants */
MSG nullstr    = "";
MSG sptbnl     = " \t\n";
MSG defpath    = ":/bin:/usr/bin";
MSG colon      = ": ";
MSG minus      = "-";
MSG endoffile  = "end of file";
MSG unexpected = " unexpected";
MSG atline     = " at line ";
MSG devnull    = "/dev/null";
MSG execpmsg   = "+ ";
MSG readmsg    = "> ";
MSG stdprompt  = "$ ";
MSG supprompt  = "# ";
MSG profile    = ".profile";

/* tables */
//
// The reserved words, and the symbol number the parser knows each by.  syslook() searches
// this whenever a word is read in a position where the grammar would accept a keyword --
// which is why `echo if' still echoes the word `if'.
//
SYSNOD reserved[] = {
    { "in", INSYM },    { "esac", ESSYM }, { "case", CASYM },  { "for", FORSYM },
    { "done", ODSYM },  { "if", IFSYM },   { "while", WHSYM }, { "do", DOSYM },
    { "then", THSYM },  { "else", ELSYM }, { "elif", EFSYM },  { "fi", FISYM },
    { "until", UNSYM }, { "{", BRSYM },    { "}", KTSYM },     { 0, 0 },
};

//
// How to describe a child that died on a signal, one entry per signal number.  A NULL
// entry means SAY NOTHING -- which is why interrupting a command prints no complaint,
// while one that dies of a bus error does.
//
STRING sysmsg[] = {
    0,
    "Hangup",
    0, /* Interrupt */
    "Quit",
    "Illegal instruction",
    "Trace/BPT trap",
    "IOT trap",
    "EMT trap",
    "Floating exception",
    "Killed",
    "Bus error",
    "Memory fault",
    "Bad system call",
    0, /* Broken pipe */
    "Alarm call",
    "Terminated",
    "Signal 16",
};

MSG export   = "export";
MSG readonly = "readonly";
//
// The built-in commands: the ones execute() runs in THIS process rather than forking for.
// They have to be built in -- a `cd' or an assignment done in a child would be thrown
// away when the child exited.
//
SYSNOD commands[] = {
    { "cd", SYSCD },
    { "read", SYSREAD },
    /*
                    {"[",		SYSTST},
    */
    { "set", SYSSET },
    { ":", SYSNULL },
    { "trap", SYSTRAP },
    { "login", SYSLOGIN },
    { "wait", SYSWAIT },
    { "eval", SYSEVAL },
    { ".", SYSDOT },
    { "newgrp", SYSLOGIN },
    { readonly, SYSRDONLY },
    { export, SYSXPORT },
    { "chdir", SYSCD },
    { "break", SYSBREAK },
    { "continue", SYSCONT },
    { "shift", SYSSHFT },
    { "exit", SYSEXIT },
    { "exec", SYSEXEC },
    { "times", SYSTIMES },
    { "umask", SYSUMASK },
    { 0, 0 },
};
