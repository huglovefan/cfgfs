#include "click.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
 #include <sys/prctl.h>
#endif

#if defined(__linux__)
 #include <X11/extensions/XTest.h>
 #include <X11/keysym.h>
#else
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

#include <lua.h>
#include <lauxlib.h>

#include "attention.h"
#include "buffers.h"
#include "cli_output.h"
#include "keys.h"
#include "lua.h"
#include "macros.h"
#if defined(__linux__)
 #include "xlib.h"
#endif

static double pending_click;

static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static KeyCode keycode;
#if defined(__linux__)
 static Display *display;
#endif

static pthread_attr_t thread_attr;

void click_init_threadattr(void) {
	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO); // placebo
	pthread_attr_setstacksize(&thread_attr, 0xffff); // glibc default: 8 MB
}

void do_click(void) {
	double now = mono_ms();
	if (
#if defined(CFGFS_HAVE_ATTENTION)
	    game_window_is_active &&
#endif
	    (pending_click == 0.0 || (now-pending_click >= 50.0)) &&
	    (0 == pthread_mutex_trylock(&click_lock))) {
		pending_click = now;
#if defined(__linux__)
		if (display != NULL && keycode != 0) {
			XTestFakeKeyEvent(display, keycode, True, CurrentTime);
			XTestFakeKeyEvent(display, keycode, False, CurrentTime);
			XFlush(display);
		}
#else
		if (keycode != 0) {
			INPUT inputs[2] = {{0}};
			inputs[0].type = INPUT_KEYBOARD;
			inputs[0].ki.wVk = (WORD)keycode;
			inputs[1].type = INPUT_KEYBOARD;
			inputs[1].ki.wVk = (WORD)keycode;
			inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
			UINT sent = SendInput(2, inputs, sizeof(INPUT));
			if (sent != 2) {
				eprintln("SendInput: 0x%x", HRESULT_FROM_WIN32(GetLastError()));
			}
		}
#endif
		pthread_mutex_unlock(&click_lock);
	}
}

static bool click_set_key(const char *name);

__attribute__((minsize))
void click_init(void) {
	pthread_mutex_lock(&click_lock);

#if defined(__linux__)
	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("click: failed to open display!");
		goto out;
	}
	XSetErrorHandler(cfgfs_handle_xerror);
	click_set_key("f11");
out:
#else
	click_set_key("f11");
#endif

	pthread_mutex_unlock(&click_lock);
}

__attribute__((minsize))
void click_deinit(void) {
	pthread_mutex_lock(&click_lock);
#if defined(__linux__)
	if (display != NULL) XCloseDisplay(exchange(display, NULL));
#endif
	keycode = 0;
	pthread_mutex_unlock(&click_lock);
}

// -----------------------------------------------------------------------------

// NOTE: assumes click_lock is held
__attribute__((minsize))
static bool click_set_key(const char *name) {
#if defined(__linux__)
	if (!display) return false;
#endif
	KeySym ks = keys_name2keysym(name);
	if (ks == 0) {
		eprintln("click_set_key: key '%s' not supported", name);
		return false;
	}
#if defined(__linux__)
	KeyCode kc = XKeysymToKeycode(display, ks);
	if (kc == 0) {
		eprintln("click_set_key: couldn't get key code for '%s'!", name);
		return false;
	}
	keycode = kc;
V	eprintln("click_set_key: key set to %s (ks=%ld, kc=%d)", name, ks, kc);
#else
	keycode = ks;
V	eprintln("click_set_key: key set to %s (ks=%d)", name, ks);
#endif
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
	double ms = luaL_checknumber(L, 1);
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
	pthread_t thread = (pthread_t)(lua_touserdata(L, 1));
	int err = pthread_cancel(thread);
	if (err != 0 && err != ESRCH) {
		eprintln("cancel_click: pthread_cancel: %s", strerror(err));
	}
	return 0;
}

static int l_click_received(lua_State *L) {
	(void)L;
	pending_click = 0.0;
	return 0;
}

__attribute__((minsize))
static int l_click_set_key(lua_State *L) {
	const char *s = luaL_checkstring(L, 1);
	pthread_mutex_lock(&click_lock);
	lua_pushboolean(L, click_set_key(s));
	pthread_mutex_unlock(&click_lock);
	return 1;
}

const luaL_Reg l_click_fns[] = {
	{"_click", l_click},
	{"_click_cancel", l_cancel_click},
	{"_click_set_key", l_click_set_key},
	{"_click_received", l_click_received},
	{NULL, NULL},
};
