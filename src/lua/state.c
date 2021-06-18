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
static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *prev_locked_by = NULL;
static double      prev_locked_dur = 0.0;

static const char *locked_by = NULL;
static double      locked_at = 0.0;

// note: string comparisons here are done with ==
// it works if the compiler de-duplicates string constants (clang and gcc do,
//  tcc doesn't)
// the lua_lock_state()/lua_get_state() macros ensure that they're only called
//  with constant strings

// fixme: seems to not work with cygwin

#if (defined(__clang__) || defined(__GNUC__)) && defined(__linux__) && !defined(SANITIZER)
 #define CHEAP_COMPARE(s1, s2) (s1 == s2)
#else
 #define CHEAP_COMPARE(s1, s2) (s1 && s2 && 0 == strcmp(s1, s2))
#endif

// D only: check that "s" is a known locker name
// % grep -Phor 'lua_(get|lock)_state\("\K[^"]+' src/ | sort -u
static inline void check_locker_name(const char *s) {
	if (CHEAP_COMPARE(s, "attention") ||
	    CHEAP_COMPARE(s, "cfgfs_init") ||
	    CHEAP_COMPARE(s, "cfgfs_read") ||
	    CHEAP_COMPARE(s, "cfgfs_readdir") ||
	    CHEAP_COMPARE(s, "cfgfs_release/sft_message") ||
	    CHEAP_COMPARE(s, "cfgfs_write/sft_console_log") ||
	    CHEAP_COMPARE(s, "cfgfs_write/sft_message") ||
	    CHEAP_COMPARE(s, "cli_input") ||
	    CHEAP_COMPARE(s, "rcon_reader") ||
	    CHEAP_COMPARE(s, "reloader")) {
		return;
	}
	eprintln("error: unknown locker name %s", s);
	abort();
}

// the total time holding the lock may be this long or a warning is printed
// locker: who the lock was held by
static inline double acceptable_call_time_for_locker(const char *me) {
	if (CHEAP_COMPARE(me, "attention") ||
	    CHEAP_COMPARE(me, "cfgfs_init") ||
	    CHEAP_COMPARE(me, "reloader")) {
		return 16.67*2.0;
	}
	return 16.67;
}

// returns true if a warning should be printed if `me` had to wait
//  `block_dur` ms to get the lua lock while it was held by `other`
static inline bool should_warn_for_contention(const char *me,
                                              const char *other,
                                              double block_dur) {
	// cfgfs_read being blocked?
	if (CHEAP_COMPARE(me, "cfgfs_read")) {

		// blocked by attention?
		if (CHEAP_COMPARE(other, "attention")) {
			return block_dur >= 16.67*4.0;
		}

		return block_dur >= 16.67/2.0;
	}

	return block_dur >= 16.67;
}

#undef CHEAP_COMPARE

lua_State *lua_get_state_real(const char *who) {
	if (unlikely(!lua_lock_state_real(who))) {
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

#define LOCK_TIMEOUT_SEC 2

bool lua_lock_state_real(const char *who) {
D	check_locker_name(who);
	double lock_start = mono_ms();
	bool contested = false;
	int err = pthread_mutex_trylock(&lua_mutex);
	if (unlikely(err == EBUSY)) {
		contested = true;
		struct timespec ts = {0};
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += LOCK_TIMEOUT_SEC;
		err = pthread_mutex_timedlock(&lua_mutex, &ts);
	}
	double lock_end = mono_ms();
	double lock_dur = lock_end-lock_start;

	if (unlikely(err != 0)) {
		if (err == ETIMEDOUT) {
			eprintln("warning: %s couldn't get lua access after %ds",
			    who, LOCK_TIMEOUT_SEC);
		} else V {
V			eprintln("lua_lock_state: %s couldn't get lock: %s",
			    who, strerror(err));
		}
		errno = err;
		return false;
	}

	if (unlikely(contested && should_warn_for_contention(who, prev_locked_by, lock_dur))) {
		eprintln("warning: lua call by %s blocked %s for %.2f ms",
		    prev_locked_by, who, lock_dur);
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
	if (locked_for > acceptable_call_time_for_locker(locked_by)) {
		eprintln("warning: lua call by %s took %.2f ms",
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
	// note: L may be the lua_State of a different coroutine here
D	assert(L != NULL);
D	assert(g_L != NULL);
	// note: may be null if it's locked using the unchecked functions
	return locked_by;
}
