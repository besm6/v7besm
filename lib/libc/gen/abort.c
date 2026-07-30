// V7/x86 source code: see www.nordier.com/v7x86 for details.
// Copyright (c) 1998 Robert Nordier.  All rights reserved.

//
// Kill the caller with SIGABRT, which dumps core.  That is signal 6, which v7 called
// SIGIOT after a PDP-11 instruction this machine has not got; only C11's name is
// defined here (<sys/signal.h>).
//
// Untested, deliberately, and for a reason signal delivery did not remove: SIGABRT is
// left at SIG_DFL here, and b6sim services a kill() that the guest does not catch by
// killing ITS OWN process (the guest pid is the host pid, cmd/sim/syscall.cpp), so a
// test of abort() would take the simulator down with the program and report as a
// harness crash rather than a result.  Under the real kernel it is an ordinary signal,
// and dumps core.
//
#include <signal.h>
#include <unistd.h>

void abort(void)
{
    kill(getpid(), SIGABRT);
}
