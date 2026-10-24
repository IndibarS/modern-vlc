/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*****************************************************************************
 * getaddrinfo.c: Linux asynchronous resolver cancellation regression tests
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1, or any later version.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc_network.h>

const char vlc_module_name[] = "test_getaddrinfo";

enum scenario { COMPLETE, QUEUED, RUNNING, NOTIFY_PENDING, ENQUEUE_ERROR };
static enum scenario scenario;
static vlc_interrupt_t *interrupt;
static struct sigevent notification;
static vlc_thread_t notifier;
static atomic_bool notified;
static struct addrinfo answer;
static int cancel_calls;

static void notify(void)
{
    atomic_store(&notified, true);
    notification.sigev_notify_function(notification.sigev_value);
}

static void *notify_later(void *opaque)
{
    (void) opaque;
    const struct timespec delay = { .tv_nsec = 20000000 };
    nanosleep(&delay, NULL);
    notify();
    return NULL;
}

static int test_enqueue(int mode, struct gaicb *list[], int count,
                        struct sigevent *event)
{
    assert(mode == GAI_NOWAIT && count == 1);
    assert(event->sigev_notify == SIGEV_THREAD);
    if (scenario == ENQUEUE_ERROR)
        return EAI_AGAIN;
    notification = *event;
    list[0]->ar_result = &answer;
    if (scenario == COMPLETE)
        notify();
    else
        vlc_interrupt_raise(interrupt);
    return 0;
}

static int test_cancel(struct gaicb *request)
{
    (void) request;
    ++cancel_calls;
    if (scenario == COMPLETE)
        return EAI_ALLDONE;
    if (scenario == QUEUED)
        return EAI_CANCELED; /* glibc sends no notification in this case. */
    assert(scenario == RUNNING || scenario == NOTIFY_PENDING);
    assert(vlc_clone(&notifier, notify_later, NULL) == 0);
    return scenario == RUNNING ? EAI_NOTCANCELED : EAI_ALLDONE;
}

static int test_suspend(const struct gaicb *const list[], int count,
                        const struct timespec *timeout)
{
    (void) list; (void) timeout;
    assert(count == 1);
    return 0;
}

static int test_error(struct gaicb *request)
{
    (void) request;
    /* Completion status alone cannot guarantee that the callback has finished
     * accessing its stack-backed semaphore. */
    assert(atomic_load(&notified));
    return 0;
}

static int test_resolve(const char *, unsigned, const struct addrinfo *,
                        struct addrinfo **);

#define getaddrinfo_a test_enqueue
#define gai_cancel test_cancel
#define gai_suspend test_suspend
#define gai_error test_error
#define vlc_getaddrinfo_i11e test_resolve
#include "../../../src/linux/getaddrinfo.c"
#undef vlc_getaddrinfo_i11e
#undef gai_error
#undef gai_suspend
#undef gai_cancel
#undef getaddrinfo_a

int main(void)
{
    alarm(10); /* The old queued-cancellation path waits forever. */
    interrupt = vlc_interrupt_create();
    assert(interrupt != NULL);
    vlc_interrupt_set(interrupt);
    const struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
    for (scenario = COMPLETE; scenario <= ENQUEUE_ERROR; ++scenario)
    {
        struct addrinfo *result = NULL;
        atomic_store(&notified, false);
        cancel_calls = 0;
        int ret = test_resolve("localhost", 80, &hints, &result);
        if (scenario == QUEUED || scenario == ENQUEUE_ERROR)
        {
            assert(ret == (scenario == QUEUED ? EAI_CANCELED : EAI_AGAIN));
            assert(result == NULL);
            assert(!atomic_load(&notified));
        }
        else
        {
            assert(ret == 0 && result == &answer);
            assert(atomic_load(&notified));
        }
        if (scenario == QUEUED)
            assert(cancel_calls == 1);
        if (scenario == RUNNING || scenario == NOTIFY_PENDING)
            vlc_join(notifier, NULL);
    }
    /* Exercise the real libc path too, without depending on external DNS. */
    const struct addrinfo numeric = {
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV,
        .ai_socktype = SOCK_STREAM,
    };
    for (unsigned i = 0; i < 128; ++i)
    {
        struct addrinfo *result = NULL;
        vlc_interrupt_raise(interrupt);
        int ret = vlc_getaddrinfo_i11e("127.0.0.1", 80, &numeric, &result);
        assert(ret == 0 || ret == EAI_CANCELED);
        if (ret == 0)
            freeaddrinfo(result);
        else
            assert(result == NULL);
    }
    vlc_interrupt_set(NULL);
    vlc_interrupt_destroy(interrupt);
    alarm(0);
    return 0;
}
