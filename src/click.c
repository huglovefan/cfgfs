#include "click.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include "attention.h"
#include "buffers.h"
#include "cli_output.h"
#include "macros.h"

#define THE_KEY XK_F11
// ~> the only keyboard keys that work in the main menu are F1-F12
// ~> TF2 has default binds for all of them except F8, F9 and F11
// ~> F8 and F9 don't work in itemtest

static Display *display;
static unsigned keycode;

static bool click_init(void) {
	static bool failed_before = false;
	if (failed_before) return false;

	assert(display == NULL);

	display = XOpenDisplay(NULL);
	if (unlikely(!display)) {
		failed_before = true;
		return false;
	}
	keycode = XKeysymToKeycode(display, THE_KEY);
	return true;
}

// -----------------------------------------------------------------------------

void click(void) {
	static _Atomic(bool) clicking = false;
	if (unlikely(clang_atomics_bug(clicking++))) return;
	if (unlikely(game_window_is_active == game_window_inactive)) goto out;
	if (unlikely(!display) && !click_init()) goto out;
	XTestFakeKeyEvent(display, keycode, True, 0);
	XTestFakeKeyEvent(display, keycode, False, 0);
	XFlush(display);
out:
	clicking = false;
}

// -----------------------------------------------------------------------------

static void *click_thread(void *msp) {
	double ms;
	memcpy(&ms, &msp, sizeof(double));
	usleep((unsigned int)(ms*1000));
	click();
	return NULL;
}
void click_after(double ms) {
	void *msp;
	memcpy(&msp, &ms, sizeof(double));

	pthread_t thread;
	int err = pthread_create(&thread, NULL, click_thread, msp);
	if (unlikely(err != 0)) {
		return;
	}
	pthread_detach(thread);
}

// -----------------------------------------------------------------------------

void opportunistic_click(void) {
	if (!buffer_list_is_empty(&buffers)) click();
}
