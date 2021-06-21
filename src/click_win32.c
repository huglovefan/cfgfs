#include "click.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <lauxlib.h>

#include "cli_output.h"
#include "click_thread.h"
#include "macros.h"

#define copycmd "exec cfgfs/click"

static double pending_click;
static pthread_mutex_t click_lock = PTHREAD_MUTEX_INITIALIZER;

// -----------------------------------------------------------------------------

void do_click(void) {
	click_thread_submit_click(0, NULL);
}

void do_click_internal_dontuse(void) {
	double now = mono_ms();
	if ((pending_click == 0.0 || (now-pending_click >= 50.0)) &&
	    (0 == pthread_mutex_trylock(&click_lock))) {
		pending_click = now;
		bool success = false;
		// from https://github.com/PazerOP/tf2_bot_detector/pull/58
		HWND gamewin = FindWindowA("Valve001", NULL);
		if (gamewin != NULL) {
			COPYDATASTRUCT data = {
				.dwData = 0,
				.cbData = sizeof(copycmd),
				.lpData = copycmd,
			};
			if (!SendMessageTimeoutA(gamewin, WM_COPYDATA, 0, (LPARAM)&data, 0, 1000, NULL)) {
				eprintln("click: failed to send command! (%d)", GetLastError());
				success = true;
			}
		}
		if (!success) pending_click = 0.0;
		pthread_mutex_unlock(&click_lock);
	}
}

// -----------------------------------------------------------------------------

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
	lua_pushboolean(L, true);
	return 1;
}

// -----------------------------------------------------------------------------

void click_init(void) {}
void click_deinit(void) {}


const luaL_Reg l_click_fns[] = {
	{"_click", l_click},
	{"_click_cancel", l_cancel_click},
	{"_click_set_key", l_click_set_key},
	{"_click_received", l_click_received},
	{NULL, NULL},
};
