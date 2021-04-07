#include "click.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <lua.h>
#include <lauxlib.h>

#include "attention.h"
#include "buffers.h"
#include "cli_output.h"
#include "keys.h"
#include "lua.h"
#include "macros.h"
#include "xlib.h"

static double pending_click;

static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static KeyCode keycode;

static pthread_attr_t thread_attr;

__attribute__((constructor))
static void _init_attr(void) {
	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO); // placebo
	pthread_attr_setstacksize(&thread_attr, 0xffff); // glibc default: 8 MB
}

void do_click(void) {
	double now = mono_ms();
	if (game_window_is_active &&
	    (pending_click == 0.0 || (now-pending_click >= 50.0)) &&
	    (0 == pthread_mutex_trylock(&click_lock))) {
		pending_click = now;
		if (display != NULL && keycode != 0) {
			XTestFakeKeyEvent(display, keycode, True, CurrentTime);
			XTestFakeKeyEvent(display, keycode, False, CurrentTime);
			XFlush(display);
		}
		pthread_mutex_unlock(&click_lock);
	}
}

static bool click_set_key(const char *name);

__attribute__((cold))
void click_init(void) {
	pthread_mutex_lock(&click_lock);

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("click: failed to open display!");
		goto out;
	}
	XSetErrorHandler(cfgfs_handle_xerror);

	click_set_key("f11");
out:
	pthread_mutex_unlock(&click_lock);
}

__attribute__((cold))
void click_deinit(void) {
	pthread_mutex_lock(&click_lock);
	if (display != NULL) XCloseDisplay(exchange(display, NULL));
	keycode = 0;
	pthread_mutex_unlock(&click_lock);
}

// -----------------------------------------------------------------------------

// NOTE: assumes click_lock is held
__attribute__((cold))
static bool click_set_key(const char *name) {
	if (!display) return false;
	KeySym ks = keys_name2keysym(name);
	if (ks == 0) {
		eprintln("click_set_key: key '%s' not supported", name);
		return false;
	}
	KeyCode kc = XKeysymToKeycode(display, ks);
	if (kc == 0) {
		eprintln("click_set_key: couldn't get key code for '%s'!", name);
		return false;
	}
	keycode = kc;
V	eprintln("click_set_key: key set to %s (ks=%ld, kc=%d)", name, ks, kc);
	return true;
}

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

	do_click();
end:
	return NULL;
}

// ~

static bool click_at(double ms, pthread_t *thread) {
	pthread_t nothread;
	if (thread == NULL) thread = &nothread;

	void *msp;
	_Static_assert(sizeof(msp) >= sizeof(ms), "double doesn't fit in a pointer");
	memcpy(&msp, &ms, sizeof(double));

	check_errcode(
	    pthread_create(thread, &thread_attr, click_thread, msp),
	    "click: pthread_create",
	    return false);

	return true;
}

// -----------------------------------------------------------------------------

static int l_click(lua_State *L) {
	double ms = lua_tonumber(L, 1);
	if (ms > 0) {
		pthread_t thread;
		if (click_at(mono_ms()+ms, &thread)) {
			lua_pushlightuserdata(L, (void *)thread);
			return 1;
		} else {
			return 0;
		}
	} else {
		do_click();
		return 0;
	}
	compiler_enforced_unreachable();
}

static int l_cancel_click(lua_State *L) {
	if (lua_islightuserdata(L, 1)) {
		pthread_t thread = (pthread_t)(lua_touserdata(L, 1));
		int err = pthread_cancel(thread);
		if (err != 0 && err != ESRCH) {
			eprintln("cancel_click: pthread_cancel: %s", strerror(err));
		}
	}
	return 0;
}

static int l_click_received(lua_State *L) {
	(void)L;
	pending_click = 0.0;
	return 0;
}

__attribute__((cold))
static int l_click_set_key(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	if (s == NULL) {
		return luaL_error(L, "click_set_key: invalid key name");
	}
	pthread_mutex_lock(&click_lock);
	lua_pushboolean(L, click_set_key(s));
	pthread_mutex_unlock(&click_lock);
	return 1;
}

__attribute__((cold))
void click_init_lua(void *L) {
	 lua_pushcfunction(L, l_click);
	lua_setglobal(L, "_click");
	 lua_pushcfunction(L, l_cancel_click);
	lua_setglobal(L, "_click_cancel");
	 lua_pushcfunction(L, l_click_set_key);
	lua_setglobal(L, "_click_set_key");
	 lua_pushcfunction(L, l_click_received);
	lua_setglobal(L, "_click_received");
}
