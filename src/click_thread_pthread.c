#include "click_thread.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "cli_output.h"
#include "click.h"
#include "macros.h"

static pthread_attr_t thread_attr;

// -----------------------------------------------------------------------------

static void mono_ms_wait_until(double target) {
	struct timespec ts;
	ms2ts(ts, target);
	for (;;) {
		int err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
		if (unlikely(err != 0)) {
			if (likely(err == EINTR)) continue;
			eprintln("click: clock_nanosleep: %s", strerror(err));
			break;
		}
		break;
	}
}

static void *click_thread(void *msp) {
	set_thread_name("click");

	double ms;
	memcpy(&ms, &msp, sizeof(double));

	mono_ms_wait_until(ms);

	// the wait is over and we weren't cancelled during that time.
	// now disable cancellation for non-cancellation-safe do_click()
	check_errcode(
	    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL),
	    "click: pthread_setcancelstate",
	    goto end);

	do_click_internal_dontuse();
end:
	return NULL;
}

// -----------------------------------------------------------------------------

_Bool click_thread_submit_click(long ms, uintptr_t *id_out) {
	pthread_t thread;

	ms += (long)mono_ms();

	void *msp;
	_Static_assert(sizeof(msp) >= sizeof(ms), "double doesn't fit in a pointer");
	memcpy(&msp, &ms, sizeof(double));

	check_errcode(
	    pthread_create(&thread, &thread_attr, click_thread, msp),
	    "click: pthread_create",
	    return false);

	if (id_out) *id_out = (uintptr_t)thread;

	return true;
}

_Bool click_thread_cancel_click(uintptr_t id) {
	return (0 == pthread_cancel((pthread_t)id));
}

// -----------------------------------------------------------------------------

void click_thread_init(void) {
	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&thread_attr, 0xffff); // glibc default: 8 MB
}

void click_thread_deinit(void) {
	pthread_attr_destroy(&thread_attr);
}
