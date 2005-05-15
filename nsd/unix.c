/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/* 
 * unix.c --
 *
 *  Unix specific routines.
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include <pwd.h>
#include <grp.h>


/*
 * Static functions defined in this file.
 */

static int Pipe(int *fds, int sockpair);
static void FatalSignalHandler(int signal);

/*
 * Static variables defined in this file.
 */

static Ns_Mutex lock;
static int debugMode;



/*
 *----------------------------------------------------------------------
 *
 * NsBlockSignals --
 *
 *  Block signals at startup.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  Signals will be pending until NsHandleSignals.
 *
 *----------------------------------------------------------------------
 */

void
NsBlockSignals(int debug)
{
    sigset_t set;

    /*
     * Block SIGHUP, SIGPIPE, SIGTERM, and SIGINT. This mask is
     * inherited by all subsequent threads so that only this
     * thread will catch the signals in the sigwait() loop below.
     * Unfortunately this makes it impossible to kill the
     * server with a signal other than SIGKILL until startup
     * is complete.
     */

    debugMode = debug;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    if (!debugMode) {
        /* NB: Don't block SIGINT in debug mode for Solaris dbx. */
        sigaddset(&set, SIGINT);
    }
    ns_sigmask(SIG_BLOCK, &set, NULL);

    /* 
     * Make sure "synchronous" signals (those generated by execution
     * errors like SIGSEGV or SIGILL which get delivered to the thread
     * that caused them) have an appropriate handler installed.
     */

    ns_signal(SIGILL, FatalSignalHandler); 
    ns_signal(SIGTRAP, FatalSignalHandler); 
    ns_signal(SIGBUS, FatalSignalHandler); 
    ns_signal(SIGSEGV, FatalSignalHandler); 
    ns_signal(SIGFPE, FatalSignalHandler); 
}


/*
 *----------------------------------------------------------------------
 * NsRestoreSignals --
 *
 *  Restore all signals to their default value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A new thread will be created.
 *
 *----------------------------------------------------------------------
 */

void
NsRestoreSignals(void)
{
    sigset_t set;
    int sig;

    for (sig = 1; sig < NSIG; ++sig) {
        ns_signal(sig, (void (*)(int)) SIG_DFL);
    }
    sigfillset(&set);
    ns_sigmask(SIG_UNBLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsHandleSignals --
 *
 *  Loop forever processing signals until a term signal
 *      is received.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  HUP callbacks may be called.
 *
 *----------------------------------------------------------------------
 */

int
NsHandleSignals(void)
{
    sigset_t set;
    int err, sig;
    
    /*
     * Wait endlessly for trigger wakeups.
     */

    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    if (!debugMode) {
        sigaddset(&set, SIGINT);
    }
    do {
    do {
        err = ns_sigwait(&set, &sig);
    } while (err == EINTR);
    if (err != 0) {
        Ns_Fatal("signal: ns_sigwait failed: %s", strerror(errno));
    }
    if (sig == SIGHUP) {
        NsRunSignalProcs();
    }
    } while (sig == SIGHUP);

    /*
     * Unblock the signals and exit.
     */

    ns_sigmask(SIG_UNBLOCK, &set, NULL);

    return sig;
}


/*
 *----------------------------------------------------------------------
 *
 * NsSendSignal --
 *
 *  Send a signal to the main thread.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  Main thread in NsHandleSignals will wakeup.
 *
 *----------------------------------------------------------------------
 */

void
NsSendSignal(int sig)
{
    if (kill(Ns_InfoPid(), sig) != 0) {
        Ns_Fatal("unix: kill() failed: '%s'", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 * ns_sockpair, ns_pipe --
 *
 *      Create a pipe/socketpair with fd's set close on exec.
 *
 * Results:
 *      0 if ok, -1 otherwise.
 *
 * Side effects:
 *      Updates given fd array.
 *
 *----------------------------------------------------------------------
 */

int
ns_sockpair(int *socks)
{
    return Pipe(socks, 1);
}

int
ns_pipe(int *fds)
{
    return Pipe(fds, 0);
}

static int
Pipe(int *fds, int sockpair)
{
    int err;

    if (sockpair) {
        err = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    } else {
        err = pipe(fds);
    }
    if (!err) {
        fcntl(fds[0], F_SETFD, 1);
        fcntl(fds[1], F_SETFD, 1);
    }
    return err;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetNameForUid --
 *
 *      Get the user name given the id
 *
 * Results:
 *      NS_TRUE if id is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetNameForUid(Ns_DString *dsPtr, int uid)
{
    struct passwd *pw = NULL;

    Ns_MutexLock(&lock);
    pw = getpwuid((uid_t)uid);
    if (pw != NULL && dsPtr) {
        Ns_DStringAppend(dsPtr, pw->pw_name);
    }
    Ns_MutexUnlock(&lock);
    return (pw != NULL) ? NS_TRUE : NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetNameForGid --
 *
 *      Get the group name given the id
 *
 * Results:
 *      NS_TRUE if id is found; NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetNameForGid(Ns_DString *dsPtr, int gid)
{
    struct group *gr = NULL;

    Ns_MutexLock(&lock);
    gr = getgrgid((gid_t)gid);
    if (gr != NULL && dsPtr) {
        Ns_DStringAppend(dsPtr, gr->gr_name);
    }
    Ns_MutexUnlock(&lock);

    return (gr != NULL) ? NS_TRUE : NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUserHome --
 *
 *      Get the home directory name for a user name
 *
 * Results:
 *      Return NS_TRUE if user name is found in /etc/passwd file and 
 *  NS_FALSE otherwise.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUserHome(Ns_DString *ds, char *user)
{
    struct passwd *pw = NULL;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw != NULL) {
        Ns_DStringAppend(ds, pw->pw_dir);
    }
    Ns_MutexUnlock(&lock);

    return (pw != NULL) ? NS_TRUE : NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUserGid --
 *
 *      Get the group id for a user name
 *
 * Results:
 *  Group id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUserGid(char *user)
{
    struct passwd *pw;
    int retcode;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw == NULL) {
        retcode = -1;
    } else {
        retcode = (int) pw->pw_gid;
    }
    Ns_MutexUnlock(&lock);
    return retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUid --
 *
 *      Get user id for a user name.
 *
 * Results:
 *      User id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUid(char *user)
{
    struct passwd *pw;
    int retcode;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw == NULL) {
        retcode = -1;
    } else {
        retcode = (int) pw->pw_uid;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}

/*
 *----------------------------------------------------------------------
 * Ns_GetGid --
 *
 *      Get the group id from a group name.
 *
 * Results:
 *  Group id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetGid(char *group)
{
    struct group *gr;
    int retcode;

    Ns_MutexLock(&lock);
    gr = getgrnam(group);
    if (gr == NULL) {
        retcode = -1;
    } else {
        retcode = (int) gr->gr_gid;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}

#ifndef HAVE_POLL
/*
 * -----------------------------------------------------------------
 *  Copyright 1994 University of Washington
 *
 *  Permission is hereby granted to copy this software, and to
 *  use and redistribute it, except that this notice may not be
 *  removed.  The University of Washington does not guarantee
 *  that this software is suitable for any purpose and will not
 *  be held liable for any damage it may cause.
 * -----------------------------------------------------------------
 * 
 *  Modified to work properly on Darwin 10.2 or less.
 *  Also, heavily reformatted to be more readable.
 */

int
poll(struct pollfd *fds, unsigned long int nfds, int timo)
{
    struct timeval timeout, *toptr;
    fd_set ifds, ofds, efds;
    int i, rc, n = -1;
    FD_ZERO(&ifds);
    FD_ZERO(&ofds);
    FD_ZERO(&efds);
    for (i = 0; i < nfds; ++i) {
        if (fds[i].fd == -1) {
            continue;
        }
        if (fds[i].fd > n) {
            n = fds[i].fd;
        }
        if ((fds[i].events & POLLIN)) {
            FD_SET(fds[i].fd, &ifds);
        }
        if ((fds[i].events & POLLOUT)) {
            FD_SET(fds[i].fd, &ofds);
        }
        if ((fds[i].events & POLLPRI)) {
            FD_SET(fds[i].fd, &efds);
        }
    }
    if (timo < 0) {
        toptr = NULL;
    } else {
        toptr = &timeout;
        timeout.tv_sec = timo / 1000;
        timeout.tv_usec = (timo - timeout.tv_sec * 1000) * 1000;
    }
    rc = select(++n, &ifds, &ofds, &efds, toptr);
    if (rc <= 0) {
        return rc;
    }
    for (i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (fds[i].fd == -1) {
            continue;
        }
        if (FD_ISSET(fds[i].fd, &ifds)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET(fds[i].fd, &ofds)) {
            fds[i].revents |= POLLOUT;
        }
        if (FD_ISSET(fds[i].fd, &efds)) {
            fds[i].revents |= POLLPRI;
        }
    }

    return rc;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * FatalSignalHandler --
 *
 *      Ensure that we drop core on fatal signals like SIGBUS and
 *      SIGSEGV.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A core file will be left wherever the server was running.
 *
 *----------------------------------------------------------------------
 */

static void
FatalSignalHandler(int signal)
{
#ifdef __linux
    /*
     * LinuxThreads thread manager needs to kill all child threads
     * on fatal signals, else they get left behind as dead threads.
     * As of glibc 2.3 with NPTL, this should be a no-op.
     */

    pthread_kill_other_threads_np();
#endif

    Ns_Log(Fatal, "received fatal signal %d", signal);
    abort();
}
