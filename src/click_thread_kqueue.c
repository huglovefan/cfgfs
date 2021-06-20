#include "click_thread.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#if defined(__FreeBSD__)
 #include <pthread_np.h>
#endif

#include <sys/event.h>

#include "cli_output.h"
#include "click.h"
#include "pipe_io.h"

#if !defined(__FreeBSD__)
 #define USING_LIBKQUEUE
#endif

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
};

// -----------------------------------------------------------------------------

static int kq = -1;
static _Atomic(uintptr_t) click_id;

static void *click_main(void *ud) {
	(void)ud;
	set_thread_name("click");
	for (;;) {
		struct kevent ev = {0};
		int evcnt = kevent(kq, NULL, 0, &ev, 1, NULL);
		if (unlikely(evcnt == -1)) {
			if (likely(errno == EINTR)) continue;
			perror("click: kevent read");
			break;
		}
#if defined(USING_LIBKQUEUE)
		if (unlikely(evcnt == 0)) continue;
#else
D		assert(evcnt != 0);
#endif
D		assert(evcnt == 1);
		if (unlikely(ev.filter == EVFILT_READ &&
		    (int)ev.ident == msgpipe[0])) {
			switch (readch(msgpipe[0])) {
			case msg_exit:
				goto out;
			}
			assert_unreachable();
		} else {
V			eprintln("click() <- %d processed", (int)ev.ident);
			do_click();
		}
	}
out:
	return NULL;
}

// -----------------------------------------------------------------------------

_Bool click_thread_submit_click(unsigned int ms, uintptr_t *id_out) {
	struct kevent ev = {
		.ident = click_id++,
		.filter = EVFILT_TIMER,
		.flags = EV_ADD|EV_ONESHOT,
#if defined(USING_LIBKQUEUE)
		.fflags = NOTE_USECONDS,
		.data = ms*1000,
#else
		.fflags = NOTE_MSECONDS,
		.data = ms,
#endif
	};
	bool ok = (-1 != kevent(kq, &ev, 1, NULL, 0, NULL));
	if (id_out) *id_out = (uintptr_t)ev.ident;
V	if (ok) eprintln("click(%u) -> %d", ms, (int)ev.ident);
	else    eprintln("click(%u) -> submit failed: %s", ms, strerror(errno));
	return ok;
}

_Bool click_thread_cancel_click(uintptr_t id) {
	struct kevent ev = {
		.ident = id,
		.filter = EVFILT_TIMER,
		.flags = EV_DELETE,
	};
	bool ok = (-1 != kevent(kq, &ev, 1, NULL, 0, NULL));
V	if (ok) eprintln("click: %d cancelled", (int)ev.ident);
	else    eprintln("click: %d cancel failed: %s", (int)ev.ident, strerror(errno));
	return ok;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void click_thread_init(void) {
	if (thread != 0) return;

	check_minus1(
	    pipe(msgpipe),
	    "click: pipe",
	    goto err);

	check_minus1(
	    kq = kqueue(),
	    "click: kqueue",
	    goto err);

	struct kevent ev = {
		.ident = (uintptr_t)msgpipe[0],
		.filter = EVFILT_READ,
		.flags = EV_ADD,
	};
	check_minus1(
	    kevent(kq, &ev, 1, NULL, 0, NULL),
	    "click: kevent add msgpipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, click_main, NULL),
	    "click: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	if (kq != -1) close(exchange(kq, -1));
	thread = 0;
}

void click_thread_deinit(void) {
	if (thread == 0) return;

	writech(msgpipe[1], msg_exit);

	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	int err = pthread_timedjoin_np(thread, NULL, &ts);
	if (err != 0) {
		return;
	}

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	close(exchange(kq, -1));

	thread = 0;
}
