#include "click.h"

#include <pthread.h>
#include <stdbool.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include "attention.h"
#include "cli_output.h"
#include "click_thread.h"
#include "keys.h"
#include "macros.h"
#include "xlib.h"

static double pending_click;
static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;
static Display *display;
static KeyCode keycode;

// -----------------------------------------------------------------------------

void do_click(void) {
	double now = mono_ms();
	if (game_window_is_active &&
	    (pending_click == 0.0 || (now-pending_click >= 50.0)) &&
	    (0 == pthread_mutex_trylock(&click_lock))) {
		pending_click = now;
		bool success = false;
		if (display != NULL && keycode != 0) {
			XTestFakeKeyEvent(display, keycode, True, CurrentTime);
			XTestFakeKeyEvent(display, keycode, False, CurrentTime);
			XFlush(display);
			success = true;
		}
		if (!success) pending_click = 0.0;
		pthread_mutex_unlock(&click_lock);
	}
}

void do_click_internal_for_click_thread(void) {
	do_click();
}

// -----------------------------------------------------------------------------

__attribute__((minsize))
static bool click_set_key(const char *name) {
	if (!display) goto err;
	KeySym ks = keys_name2keysym(name);
	if (ks == 0) goto err_ks;
	KeyCode kc = XKeysymToKeycode(display, ks);
	if (kc == 0) goto err_kc;
	keycode = kc;
V	eprintln("click_set_key: key set to %s (ks=%ld, kc=%d)", name, ks, kc);
	return true;
err_ks:
	eprintln("click_set_key: key '%s' not supported", name);
	goto err;
err_kc:
	eprintln("click_set_key: couldn't get key code for '%s'!", name);
	goto err;
err:
	return false;
}

// -----------------------------------------------------------------------------

// todo: non-x11-specific functions should go somewhere else

static int l_click(lua_State *L) {
	int ok;
	long ms = lua_tointegerx(L, 1, &ok);
	if (unlikely(!ok)) {
		double n = luaL_checknumber(L, 1);
		ms = (long)n;
		ms += (n >= 0) ? 1 : -1;
	}
	if (ms > 0) {
		uintptr_t id;
		if (likely(click_thread_submit_click(ms, &id))) {
			lua_pushlightuserdata(L, (void *)id);
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
	uintptr_t id = (uintptr_t)(void *)lua_touserdata(L, 1);
	click_thread_cancel_click(id);
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
	 bool rv = click_set_key(s);
	pthread_mutex_unlock(&click_lock);
	lua_pushboolean(L, rv);
	return 1;
}

// -----------------------------------------------------------------------------

__attribute__((minsize))
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

__attribute__((minsize))
void click_deinit(void) {
	pthread_mutex_lock(&click_lock);
	 if (display != NULL) XCloseDisplay(exchange(display, NULL));
	 keycode = 0;
	pthread_mutex_unlock(&click_lock);
}

const luaL_Reg l_click_fns[] = {
	{"_click", l_click},
	{"_click_cancel", l_cancel_click},
	{"_click_set_key", l_click_set_key},
	{"_click_received", l_click_received},
	{NULL, NULL},
};
