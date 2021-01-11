#ifndef _GNU_SOURCE
 #define _GNU_SOURCE 1 // unlocked_stdio
#endif
#include "lua.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#include <lualib.h>
#include <lauxlib.h>

#include "attention.h"
#include "buffers.h"
#include "cfg.h"
#include "cli_input.h"
#include "cli_output.h"
#include "click.h"
#include "keys.h"
#include "macros.h"
#include "main.h"

static lua_State *g_L;
static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;

lua_State *lua_get_state(void) {
	lua_lock_state();
D	assert(g_L != NULL);
	return g_L;
}

void lua_release_state(lua_State *L) {
D	assert(L != NULL);
	lua_unlock_state();
}

void lua_lock_state(void) {
	pthread_mutex_lock(&lua_mutex);
D	assert(stack_is_clean(g_L));
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

void lua_deinit(void) {
	pthread_mutex_lock(&lua_mutex);
D	assert(stack_is_clean(g_L));
	if (g_L != NULL) {
		 lua_getglobal(g_L, "_fire_unload");
		  lua_pushboolean(g_L, 1);
		lua_call(g_L, 1, 0);
		lua_close(exchange(g_L, NULL));
	}
	pthread_mutex_unlock(&lua_mutex);
}

// -----------------------------------------------------------------------------

static void lua_print_backtrace(lua_State *L) {
	size_t sz;
	const char *s;
	if (lua_checkstack(L, 1)) {
		luaL_traceback(L, L, NULL, 0);
		s = lua_tolstring(L, -1, &sz);
		if (s != NULL && sz != strlen("stack traceback:")) {
			fprintf(stderr, "%s\n", s);
		}
		lua_pop(L, 1);
	}
}
static void c_print_backtrace(void) {
	typedef void (*print_backtrace_t)(void);
	print_backtrace_t fn = (print_backtrace_t)dlsym(RTLD_DEFAULT, "__sanitizer_print_stack_trace");
	if (fn != NULL) fn();
}

__attribute__((cold))
static int l_panic(lua_State *L) {
	if (cli_trylock_output_nosave()) cli_save_prompt_locked();
	fprintf(stderr, "fatal error: %s\n", lua_tostring(L, -1));
	lua_print_backtrace(L);
	c_print_backtrace();
	return 0;
}
__attribute__((cold))
static int l_fatal(lua_State *L) {
	if (cli_trylock_output_nosave()) cli_save_prompt_locked();
	const char *s = lua_tostring(L, 1);
	if (s) fprintf(stderr, "fatal error: %s\n", s);
	lua_print_backtrace(L);
	c_print_backtrace();
	abort();
}

int lua_do_nothing(lua_State *L) {
	(void)L;
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

// string.after('/cfgfs/buffer.cfg', '/cfgfs/') -> 'buffer.cfg'
static int l_string_after(lua_State *L) {
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	if (likely(l1 >= l2 && memcmp(s1, s2, l2) == 0)) {
		lua_pushlstring(L, s1+l2, l1-l2);
		return 1;
	}
	return 0;
error:
	return luaL_error(L, "string.after: invalid argument");
}

// string.before('buffer.cfg', '.cfg') -> 'buffer'
static int l_string_before(lua_State *L) {
	size_t l1, l2;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	if (unlikely(!s1 || !s2)) goto error;
	if (likely(l1 >= l2 && memcmp(s1+l1-l2, s2, l2) == 0)) {
		lua_pushlstring(L, s1, l1-l2);
		return 1;
	}
	return 0;
error:
	return luaL_error(L, "string.before: invalid argument");
}

// string.between('/cfgfs/buffer.cfg', '/cfgfs/', '.cfg') -> 'buffer'
static int l_string_between(lua_State *L) {
	size_t l1, l2, l3;
	const char *s1 = lua_tolstring(L, 1, &l1);
	const char *s2 = lua_tolstring(L, 2, &l2);
	const char *s3 = lua_tolstring(L, 3, &l3);
	if (unlikely(!s1 || !s2 || !s3)) goto error;
	if (likely(l1 >= l2+l3 && memcmp(s1, s2, l2) == 0 && memcmp(s1+l1-l3, s3, l3) == 0)) {
		lua_pushlstring(L, s1+l2, l1-l2-l3);
		return 1;
	}
	return 0;
error:
	return luaL_error(L, "string.between: invalid argument");
}

// removes whitespace from the start and end
static int l_string_trim(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	const char *first, *last;
	if (unlikely(!s)) goto error;

	first = NULL;
	last = s;
	while (*s != '\0') {
		if (*s > 0x20) {
			first = first ?: s;
			last = s;
		}
		s++;
	}
	if (likely(first)) {
		lua_pushlstring(L, first, (size_t)(last-first)+1);
	} else {
		lua_pushlstring(L, "", 0);
	}

	return 1;
error:
	return luaL_error(L, "string.trim: invalid argument");
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

static int l_get_thread_name(lua_State *L) {
	char s[17] = {0};
	char *p = s;
	if (unlikely(-1 == prctl(PR_GET_NAME, s, 0, 0, 0))) {
		perror("get_thread_name");
		p = NULL;
	}
	lua_pushstring(L, s);
	return 1;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
bool lua_init(void) {

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	lua_atpanic(L, l_panic);

	 lua_pushcfunction(L, l_fatal);
	lua_setglobal(L, "_fatal");

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

	 lua_pushcfunction(L, l_get_thread_name);
	lua_setglobal(L, "_get_thread_name");

	lua_newtable(L); lua_setglobal(L, "num2key");
	lua_newtable(L); lua_setglobal(L, "key2num");
	lua_newtable(L); lua_setglobal(L, "bindfilenames");

	 lua_getglobal(L, "num2key");
	 for (int i = 0; keys[i].name != NULL; i++) {
	  lua_pushinteger(L, (lua_Integer)i+1);
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
	") != LUA_OK) lua_error(L);

	buffers_init_lua(L);
	cfg_init_lua(L);
	cli_input_init_lua(L);
	click_init_lua(L);

	_Static_assert(sizeof(AGPL_SOURCE_URL) > 1, "AGPL_SOURCE_URL not set to a valid value");
	lua_pushstring(L, AGPL_SOURCE_URL); lua_setglobal(L, "agpl_source_url");

	if (luaL_loadfile(L, "./builtin.lua") != LUA_OK) {
		eprintln("error: %s", lua_tostring(L, -1));
		eprintln("failed to load builtin.lua!");
		goto err;
	}
	 lua_call(L, 0, 1);
	 if (!lua_toboolean(L, -1)) goto err;
	lua_pop(L, 1);

	 lua_getglobal(L, "_get_contents");
D	 assert(lua_gettop(L) == GET_CONTENTS_IDX);
D	 assert(lua_type(L, GET_CONTENTS_IDX) == LUA_TFUNCTION);
	  lua_getglobal(L, "_game_console_output");
D	  assert(lua_gettop(L) == GAME_CONSOLE_OUTPUT_IDX);
D	  assert(lua_type(L, GAME_CONSOLE_OUTPUT_IDX) == LUA_TFUNCTION);

	buffer_list_swap(&buffers, &init_cfg);

	 lua_getglobal(L, "_fire_startup");
	lua_call(L, 0, 0);

	assert(stack_is_clean(L));

	g_L = L;

	return true;
err:
	lua_close(L);
	return false;

}
