#include "click.h"
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include "macros.h"

#define THE_KEY XK_F9
// - the only keyboard keys that work in the main menu are F1-F12
// - TF2 has default binds for all of them except F8, F9 and F11
// - F8 and F9 don't have any common associations while F11 is usually used for
//   full screening something (don't want to accidentally press it)

static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static unsigned keycode;

static void do_click(void) {
	if (unlikely(pthread_mutex_trylock(&click_lock) != 0)) return;
	XTestFakeKeyEvent(display, keycode, True, 0);
	XTestFakeKeyEvent(display, keycode, False, 0);
	XFlush(display);
	pthread_mutex_unlock(&click_lock);
}

static double _ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void *click_main(void *p) {
	double ms = *(double *)&p;
	ms = ms-_ms();
	if (likely(ms > 0)) {
		if (unlikely(usleep((unsigned int)ms*1000) != 0)) {
			perror("click: usleep");
			goto end;
		}
	}
	do_click();
end:
	pthread_exit(NULL);
	return NULL;
}

void click(double ms) {
	if (unlikely(display == NULL)) {
		pthread_mutex_lock(&click_lock);
		display = XOpenDisplay(NULL);
		if (display) keycode = XKeysymToKeycode(display, THE_KEY);
		pthread_mutex_unlock(&click_lock);
		if (unlikely(display == NULL)) {
			Dbg("failed to open display!");
			return;
		}
	}
	pthread_t thread;
	int err = pthread_create(&thread, NULL, click_main, *(void **)&ms);
	if (likely(err == 0)) {
		pthread_detach(thread);
	}
}
