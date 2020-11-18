#define _GNU_SOURCE // unlocked_stdio
#include "lua.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <lualib.h>
#include <lauxlib.h>

#include "attention.h"
#include "buffers.h"
#include "cfg.h"
#include "cli_output.h"
#include "click.h"
#include "keys.h"
#include "macros.h"
#include "main.h"

// -----------------------------------------------------------------------------

static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;

void LUA_LOCK(void) { pthread_mutex_lock(&lua_mutex); }
void LUA_UNLOCK(void) { pthread_mutex_unlock(&lua_mutex); }
bool LUA_TRYLOCK(void) { return (0 == pthread_mutex_trylock(&lua_mutex)); }

__attribute__((noinline))
bool LUA_TIMEDLOCK(double sec) {
	struct timespec ts;
	int rv = clock_gettime(CLOCK_REALTIME, &ts); // timedlock wants realtime
D	if (unlikely(rv == -1)) {
		perror("LUA_TIMEDLOCK: clock_gettime");
		return false;
	}
	ms2ts(ts, ts2ms(ts)+sec*1000);
	return (0 == pthread_mutex_timedlock(&lua_mutex, &ts));
}

// -----------------------------------------------------------------------------

__attribute__((cold))
int lua_print_backtrace(lua_State *L) {
	if (!lua_checkstack(L, 1)) return 0;
	 luaL_traceback(L, L, NULL, 0);
	 const char *s = lua_tostring(L, -1);
	 if (s) eprintln("%s", s);
	lua_pop(L, 1);
	return 0;
}

int lua_do_nothing(lua_State *L) {
	(void)L;
	return 0;
}

__attribute__((cold))
static int l_panic(lua_State *L) {
	 eprintln("fatal error: %s", lua_tostring(L, -1));
	 lua_print_backtrace(L);
	 __sanitizer_print_stack_trace();
	 return 0;
}

// -----------------------------------------------------------------------------

// string functions

static int l_string_starts_with(lua_State *L) {
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	lua_pushboolean(L, (l1 >= l2 && memcmp(s1, s2, l2) == 0));
	return 1;
error:
	return luaL_error(L, "invalid argument");
}

static int l_string_ends_with(lua_State *L) {
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	lua_pushboolean(L, (l1 >= l2 && memcmp(s1+l1-l2, s2, l2) == 0));
	return 1;
error:
	return luaL_error(L, "invalid argument");
}

static int l_string_after(lua_State *L) {
	const char *rv = NULL;
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	if (likely(l1 >= l2 && memcmp(s1, s2, l2) == 0)) {
		rv = s1+l2;
	}
	lua_pushstring(L, rv);
	return 1;
error:
	return luaL_error(L, "invalid argument");
}

static int l_string_before(lua_State *L) {
	char *rv = NULL;
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	if (likely(l1 >= l2 && memcmp(s1+l1-l2, s2, l2) == 0)) {
		rv = alloca(l1-l2+1);
		memcpy(rv, s1, l1-l2);
		rv[l1-l2] = '\0';
	}
	lua_pushstring(L, rv);
	return 1;
error:
	return luaL_error(L, "invalid argument");
}

static int l_string_between(lua_State *L) {
	char *rv = NULL;
	size_t l1, l2, l3;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	const char *s3 = lua_tolstring(L, 3, &l3);
	if (unlikely(!s1 || !s2 || !s3)) goto error;
	if (likely(l1 >= l2+l3 && memcmp(s1, s2, l2) == 0 && memcmp(s1+l1-l3, s3, l3) == 0)) {
		rv = alloca(l1-(l2+l3)+1);
		memcpy(rv, s1+l2, l1-(l2+l3));
		rv[l1-(l2+l3)] = '\0';
	}
	lua_pushstring(L, rv);
	return 1;
error:
	return luaL_error(L, "invalid argument");
}

static int l_string_trim(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	if (unlikely(!s)) goto error;

	const char *first = NULL, *last = s;
	while (*s != '\0') {
		if (*s > 0x20) {
			first = first ?: s;
			last = s;
		}
		s++;
	}
	if (unlikely(!first)) first = s;
	// may write past the end if the string was empty but addresssanitizer doesn't complain
	char tmp = exchange(*((char *)(uintptr_t)last+1), '\0');
	lua_pushstring(L, first);
	*((char *)(uintptr_t)last+1) = tmp;

	return 1;
error:
	return luaL_error(L, "invalid argument");
}

// -----------------------------------------------------------------------------

static int l_ms(lua_State *L) {
	lua_pushnumber(L, mono_ms());
	return 1;
}

// -----------------------------------------------------------------------------

// printing

static int l_print(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	if (unlikely(s == NULL)) return 0;
	cli_lock_output();
	fputs_unlocked(s, stdout);
	fputc_unlocked('\n', stdout);
	cli_unlock_output();
	return 0;
}
static int l_eprint(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	if (unlikely(s == NULL)) return 0;
	cli_lock_output();
	fputs_unlocked(s, stderr);
	fputc_unlocked('\n', stderr);
	cli_unlock_output();
	return 0;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
lua_State *lua_init(void) {

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	lua_atpanic(L, l_panic);

	 lua_pushcfunction(L, l_print);
	lua_setglobal(L, "_print");
	 lua_pushcfunction(L, l_eprint);
	lua_setglobal(L, "_eprint");

	 lua_pushcfunction(L, l_ms);
	lua_setglobal(L, "_ms");

	 lua_getglobal(L, "string");
	  lua_pushcfunction(L, l_string_starts_with);
	 lua_setfield(L, -2, "starts_with");
	  lua_pushcfunction(L, l_string_ends_with);
	 lua_setfield(L, -2, "ends_with");
	  lua_pushcfunction(L, l_string_after);
	 lua_setfield(L, -2, "after");
	  lua_pushcfunction(L, l_string_before);
	 lua_setfield(L, -2, "before");
	  lua_pushcfunction(L, l_string_between);
	 lua_setfield(L, -2, "between");
	  lua_pushcfunction(L, l_string_trim);
	 lua_setfield(L, -2, "trim");
	lua_pop(L, 1);

	lua_newtable(L); lua_setglobal(L, "num2key");
	lua_newtable(L); lua_setglobal(L, "key2num");
	lua_newtable(L); lua_setglobal(L, "bindfilenames");

	lua_getglobal(L, "num2key");
	for (int i = 0; keys[i].name != NULL; i++) {
		lua_pushinteger(L, (lua_Integer)i+1); // the number doesn't matter
		lua_pushstring(L, keys[i].name);
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (luaL_dostring(L, "\
	for n, key in ipairs(num2key) do\
		local down   = string.format('/cfgfs/keys/+%d.cfg', n)\
		local up     = string.format('/cfgfs/keys/-%d.cfg', n)\
		local toggle = string.format('/cfgfs/keys/^%d.cfg', n)\
		local once   = string.format('/cfgfs/keys/@%d.cfg', n)\
		bindfilenames[down]   = {name = key, type = 'down'}\
		bindfilenames[up]     = {name = key, type = 'up'}\
		bindfilenames[toggle] = {name = key, type = 'toggle'}\
		bindfilenames[once]   = {name = key, type = 'once'}\
		key2num[key] = n\
	end\
	collectgarbage()\
	") != LUA_OK) lua_error(L);

	attention_init_lua(L);
	buffers_init_lua(L);
	cfg_init_lua(L);
	click_init_lua(L);

	assert(lua_gettop(L) == 0);

	return L;

}
