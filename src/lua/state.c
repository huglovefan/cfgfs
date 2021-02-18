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
#include "../click.h"
#include "../macros.h"

static lua_State *g_L;
static pthread_mutex_t lua_mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

lua_State *lua_get_state(void) {
	lua_lock_state();
D	assert(g_L != NULL);
	return g_L;
}

void lua_release_state(lua_State *L) {
D	assert(L != NULL);
D	assert(L == g_L);
	lua_unlock_state();
}

bool lua_lock_state(void) {
	int err = pthread_mutex_lock(&lua_mutex);
	if (unlikely(err != 0)) {
		errno = err;
		return false;
	}
	if (unlikely(g_L == NULL)) {
		pthread_mutex_unlock(&lua_mutex);
		errno = EIO;
		return false;
	}
D	assert(stack_is_clean(g_L));
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
