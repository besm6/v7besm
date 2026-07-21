/* V7/x86 source code: see www.nordier.com/v7x86 for details. */
/* Copyright (c) 1998 Robert Nordier.  All rights reserved. */

/*
 * Kill the caller with SIGIOT, which dumps core.
 *
 * Untested, deliberately: b6sim services kill() by killing ITS OWN process (the guest
 * pid is the host pid, cmd/sim/syscall.cpp), so a test of abort() would take the
 * simulator down with the program and report as a harness crash rather than a result.
 * Under the real kernel it will be an ordinary signal, once delivery lands in phase 6.
 */
#include <signal.h>

int getpid(void);
int kill(int pid, int sig);

void abort(void)
{
    kill(getpid(), SIGIOT);
}
