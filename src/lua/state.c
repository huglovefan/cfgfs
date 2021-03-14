#include "state.h"
#include "state_priv.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <lualib.h>
#include <lauxlib.h>

#include "../buffers.h"
#include "../cli_output.h"
#include "../click.h"
#include "../macros.h"

static lua_State *g_L;
static pthread_mutex_t lua_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

#if defined(WITH_D) || defined(WITH_V)
 #define ACCEPTABLE_DELAY (16.67/2)
#else
 #define ACCEPTABLE_DELAY (16.67)
#endif

static const char *locked_by = NULL;
static double      locked_at = 0.0;

lua_State *lua_get_state(const char *who) {
	if (unlikely(!lua_lock_state(who))) {
		return NULL;
	}
	return g_L;
}

void lua_release_state(lua_State *L) {
	// this is meaningless. i don't know why it has this parameter
D	assert(L != NULL);
D	assert(L == g_L);
	lua_unlock_state();
}

bool lua_lock_state(const char *who) {
	int err = pthread_mutex_lock(&lua_mutex);
	if (unlikely(err != 0)) {
V		eprintln("lua_lock_state: %s couldn't get lock: %s",
		    who, strerror(err));
		errno = err;
		return false;
	}
	if (unlikely(g_L == NULL)) {
		pthread_mutex_unlock(&lua_mutex);
V		eprintln("lua_lock_state: %s couldn't get lock: g_L == NULL",
		    who);
		errno = EIO;
		return false;
	}
D	assert(stack_is_clean(g_L));
	locked_by = who;
	locked_at = mono_ms();
	return true;
}

void lua_unlock_state(void) {
	bool click = (!buffer_list_is_empty(&buffers));
	lua_unlock_state_no_click();
	if (click) do_click();
}

lua_State *lua_get_state_already_locked(void) {
D	assert(g_L != NULL);
	return g_L;
}

void lua_unlock_state_no_click(void) {
D	assert(stack_is_clean(g_L));
else	assert(stack_is_clean_quick(g_L));
	double locked_for = mono_ms()-locked_at;
	if (locked_for > ACCEPTABLE_DELAY) {
		eprintln("warning: lua call took %.2fms (%s)",
		    locked_for, locked_by);
	}
	locked_by = NULL;
	locked_at = 0.0;
	pthread_mutex_unlock(&lua_mutex);
}

void lua_unlock_state_and_click(void) {
	lua_unlock_state_no_click();
	do_click();
}

// -----------------------------------------------------------------------------

// state_priv.h

void lua_set_state_unchecked(void *L) {
	g_L = L;
}

void *lua_get_state_unchecked(void) {
	return g_L;
}

_Bool lua_lock_state_unchecked(void) {
	int err = pthread_mutex_lock(&lua_mutex);
	if (unlikely(err != 0)) {
		errno = err;
		return false;
	}
	return true;
}

void lua_unlock_state_unchecked(void) {
	pthread_mutex_unlock(&lua_mutex);
}
