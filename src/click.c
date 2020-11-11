#include "click.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static KeyCode keycode;

static void do_click(void) {
	if (unlikely(game_window_is_active == activeness_inactive)) return;
	pthread_mutex_lock(&click_lock);
	 if (unlikely(display == NULL || keycode == 0)) goto out_locked;
	 XTestFakeKeyEvent(display, keycode, True, 0);
	 XTestFakeKeyEvent(display, keycode, False, 0);
	 XFlush(display);
out_locked:
	pthread_mutex_unlock(&click_lock);
}

static bool click_set_key(const char *name);

__attribute__((cold))
void click_init(void) {
	pthread_mutex_lock(&click_lock);
	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("click: failed to open display!");
		goto out_locked;
	}
	click_set_key("f11");
out_locked:
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
VV	eprintln("click: waiting %lf ms", target-mono_ms());
	struct timespec ts = {
		.tv_sec = (time_t)(target/1000.0),
		.tv_nsec = (long)(fmod(target,1000.0)*1000000.0),
	};
	int err;
again:
	err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	if (likely(err == 0)) return;
	if (likely(err == EINTR)) goto again;
	eprintln("click: clock_nanosleep: %s", strerror(err));
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
	pthread_t thread;

	memcpy(&msp, &ms, sizeof(double));
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

void opportunistic_click(void) {
	if (getenv("CFGFS_NO_CLICK")) return;
	if (!buffer_list_is_empty(&buffers)) do_click();
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
	do_click();
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
	if (!getenv("CFGFS_NO_CLICK")) {
		 lua_pushcfunction(L, l_click);
		lua_setglobal(L, "_click");
		 lua_pushcfunction(L, l_click_after);
		lua_setglobal(L, "_click_after");
		 lua_pushcfunction(L, l_click_at);
		lua_setglobal(L, "_click_at");
		 lua_pushcfunction(L, l_click_set_key);
		lua_setglobal(L, "_click_set_key");
	} else {
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_click");
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_click_after");
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_click_at");
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_click_set_key");
	}
}
