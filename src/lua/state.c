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

static const char *prev_locked_by = NULL;
static double      prev_locked_dur = 0.0;

static const char *locked_by = NULL;
static double      locked_at = 0.0;

// taking the lock may take up to this long or a warning is printed
// locker: name of who's trying to take the lock
static inline double acceptable_lock_delay_for_locker(const char *locker) {
	if (likely(0 == strcmp(locker, "cfgfs_read"))) {
		// super critical
		return 1.0;
	}
	// not really critical
	return 4.0;
}

// the total time holding the lock may be this long or a warning is printed
// locker: who the lock was held by
static inline double acceptable_call_delay_for_locker(const char *locker) {
	if (unlikely(0 == strcmp(locker, "cfgfs_init") ||
	             0 == strcmp(locker, "reloader"))) {
		// non-interactive
		return 16.67*2.0;
	}
	return 16.67/2.0;
}

static inline bool should_warn_for_contention(const char *me,
                                              const char *other) {
#if !(defined(WITH_D) || defined(WITH_V))
	if (likely(0 == strcmp(me, "cfgfs_read"))) {
		// super critical
		return true;
	}
	if (likely(other)) {
		if (0 == strcmp(other, "cfgfs_write/sft_console_log")) {
			// this needs to be fast
			return true;
		}
	}
	return false;
#else
	// D and V warn for all contention by default
	(void)me;
	(void)other;
	return true;
#endif
}

lua_State *lua_get_state(const char *who) {
	if (unlikely(!lua_lock_state(who))) {
		return NULL;
	}
	return g_L;
}

void lua_release_state(lua_State *L) {
	// this ↓ is meaningless. i don't know why it has this parameter
D	assert(L != NULL);
D	assert(L == g_L);
	lua_unlock_state();
}

bool lua_lock_state(const char *who) {
	double lock_start = mono_ms();
	bool contested = false;
	int err = pthread_mutex_trylock(&lua_mutex);
	if (unlikely(err == EBUSY)) {
		contested = true;
		err = pthread_mutex_lock(&lua_mutex);
	}
	double lock_end = mono_ms();
	double lock_dur = lock_end-lock_start;

	if (unlikely(err != 0)) {
V		eprintln("lua_lock_state: %s couldn't get lock: %s",
		    who, strerror(err));
		errno = err;
		return false;
	}

	if (unlikely(contested && should_warn_for_contention(who, prev_locked_by))) {
		eprintln("warning: %s: lock contention! lock was held by %s for %.2f ms",
		    who, prev_locked_by, prev_locked_dur);
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
	locked_at = lock_end;

	if (lock_dur > acceptable_lock_delay_for_locker(locked_by)) {
		eprintln("warning: %s: locking lua took %.2f ms while %s held the lock for %.2f ms",
		    locked_by, lock_dur, prev_locked_by, prev_locked_dur);
	}

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
	if (locked_for > acceptable_call_delay_for_locker(locked_by)) {
		eprintln("warning: %s: lua call took %.2f ms",
		    locked_by, locked_for);
	}

	prev_locked_by  = locked_by;
	prev_locked_dur = locked_for;
	locked_by       = NULL;
	locked_at       = 0.0;

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

// -----------------------------------------------------------------------------

int l_get_locker(lua_State *L) {
	lua_pushstring(L, locked_by);
	return 1;
}

const char *lua_get_locker(lua_State *L) {
D	assert(L != NULL);
D	assert(L == g_L);
	// note: may be null if it's locked using the unchecked functions
	return locked_by;
}
