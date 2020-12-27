#include "click.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lua.h>
 #include <lauxlib.h>
#endif

#include "attention.h"
#include "buffers.h"
#include "cli_output.h"
#include "keys.h"
#include "lua.h"
#include "macros.h"

// used to combine clicks for "wait(0)" calls
// set when lua clicks without a delay
// if set, further clicks without a delay do nothing
// cleared when we get a click
static bool pending_click;

static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static KeyCode keycode;

void do_click(void) {
	if (game_window_is_active != attn_inactive &&
	    (0 == pthread_mutex_trylock(&click_lock))) {
		if (display != NULL && keycode != 0) {
			XTestFakeKeyEvent(display, keycode, True, CurrentTime);
			XTestFakeKeyEvent(display, keycode, False, CurrentTime);
			XFlush(display);
		}
		pthread_mutex_unlock(&click_lock);
	}
}

static bool click_set_key(const char *name);

void click_init(void) {
	pthread_mutex_lock(&click_lock);

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("click: failed to open display!");
		goto out;
	}

	click_set_key("f11");

out:
	pthread_mutex_unlock(&click_lock);
}

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
	int err;
again:
	err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	if (likely(err == 0)) return;
	if (likely(err == EINTR)) goto again;
	perror("click: clock_nanosleep");
}

static void *click_thread(void *msp) {
	double ms;
	memcpy(&ms, &msp, sizeof(double));
	mono_ms_wait_until(ms);
	do_click();
	return NULL;
}

// ~

static void click_at(double ms) {
	void *msp;
	_Static_assert(sizeof(msp) >= sizeof(ms));
	memcpy(&msp, &ms, sizeof(double));

	pthread_t thread;
	int err = pthread_create(&thread, NULL, click_thread, msp);
	if (unlikely(err != 0)) {
V		eprintln("click_at: pthread_create: %s", strerror(err));
		return;
	}

	pthread_detach(thread);
}

static void click_after(double ms) {
	click_at(mono_ms()+ms);
}

// -----------------------------------------------------------------------------

static int l_click_after(lua_State *L) {
	click_after(lua_tonumber(L, 1));
	return 0;
}
static int l_click_at(lua_State *L) {
	click_at(lua_tonumber(L, 1));
	return 0;
}
static int l_click(lua_State *L) {
	(void)L;
	if (!pending_click++) do_click();
	return 0;
}
static int l_click_received(lua_State *L) {
	(void)L;
	pending_click = false;
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
	 lua_pushcfunction(L, l_click_after);
	lua_setglobal(L, "_click_after");
	 lua_pushcfunction(L, l_click_at);
	lua_setglobal(L, "_click_at");
	 lua_pushcfunction(L, l_click_set_key);
	lua_setglobal(L, "_click_set_key");
	 lua_pushcfunction(L, l_click_received);
	lua_setglobal(L, "_click_received");
}
