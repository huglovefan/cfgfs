#include "init.h"

#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <lualib.h>
#include <lauxlib.h>

#include "../buffers.h"
#include "../cli_output.h"
#include "../error.h"
#include "../lua.h"
#include "../macros.h"

#include "builtins.h"
#include "state_priv.h"

// put required global variables and their types here
static struct reqval {
	const char *name;
	int type;
} reqvals[] = {
	// attention.c
	{"_attention", LUA_TFUNCTION},

	// cli_input.c
	{"_cli_input", LUA_TFUNCTION},

	// lua/init.c
	{"_fire_startup", LUA_TFUNCTION},
	{"_fire_unload", LUA_TFUNCTION},

	// main.c
	{"_game_console_output", LUA_TFUNCTION},
	{"_get_contents", LUA_TFUNCTION},
	{"_message", LUA_TFUNCTION},

	// reloader.c
	{"_reload_1", LUA_TFUNCTION},
	{"_reload_2", LUA_TFUNCTION},

	{NULL, 0},
};

#if LUA_VERSION_NUM == 501
 #define LUA_OK 0
#endif

// -----------------------------------------------------------------------------

// panic function

static void lua_print_backtrace(lua_State *L);

static int l_panic(lua_State *L) {
	if (cli_trylock_output_nosave()) {
		cli_save_prompt_locked();
	}
	if (lua_gettop(L) >= 1) {
		fprintf(stderr, "fatal error: %s\n", lua_tostring(L, -1));
	} else {
		fprintf(stderr, "fatal error!\n");
	}
	lua_print_backtrace(L);
	print_c_backtrace_unlocked();
	return 0;
}

static void lua_print_backtrace(lua_State *L) {
	size_t sz;
	const char *s;
#if LUA_VERSION_NUM >= 502
	if (lua_checkstack(L, 1)) {
		luaL_traceback(L, L, NULL, 0);
		s = lua_tolstring(L, -1, &sz);
		if (s != NULL && sz != strlen("stack traceback:")) {
			fprintf(stderr, "%s\n", s);
		}
		lua_pop(L, 1);
	}
#else
	if (lua_checkstack(L, 2)) {
		lua_getglobal(L, "debug");
		lua_getfield(L, -1, "traceback");
		lua_call(L, 0, 1);
		s = lua_tolstring(L, -1, &sz);
		if (s != NULL && sz != strlen("stack traceback:")) {
			fprintf(stderr, "%s\n", s);
		}
		lua_pop(L, 2);
	}
#endif
}

// -----------------------------------------------------------------------------

static void check_required_globals(lua_State *L);

bool lua_init(void) {
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	lua_atpanic(L, l_panic);

	lua_define_builtins(L);

	lua_set_state_unchecked(L);

	// CFGFS_DIR: set by cfgfs_run
	char path[256];
	snprintf(path, sizeof(path), "%s/builtin.lua",
	    getenv("CFGFS_DIR") ?: ".");

	if (LUA_OK != luaL_loadfile(L, path)) {
		eprintln("error: %s", lua_tostring(L, -1));
		eprintln("failed to load builtin.lua!");
		goto err;
	}
	 lua_call(L, 0, 1);
	 if (!lua_toboolean(L, -1)) goto err;
	lua_pop(L, 1);

	check_required_globals(L);

D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_get_contents");
D	 assert(lua_gettop(L) == GET_CONTENTS_IDX);
D	 assert(lua_type(L, GET_CONTENTS_IDX) == LUA_TFUNCTION);
	  lua_getglobal(L, "_game_console_output");
D	  assert(lua_gettop(L) == GAME_CONSOLE_OUTPUT_IDX);
D	  assert(lua_type(L, GAME_CONSOLE_OUTPUT_IDX) == LUA_TFUNCTION);
	   lua_getglobal(L, "_message");
D	   assert(lua_gettop(L) == MESSAGE_IDX);
D	   assert(lua_type(L, MESSAGE_IDX) == LUA_TFUNCTION);

	buffer_list_swap(&buffers, &init_cfg);

	assert(stack_is_clean(L));

	return true;
err:
	lua_set_state_unchecked(NULL);
	lua_close(L);
	return false;

}

static void check_required_globals(lua_State *L) {
	bool ok = true;
	// todo: should use rawget() to suppress the other error for accessing nonexistent variables

	for (const struct reqval *p = reqvals; p->name != NULL; p++) {
		lua_getglobal(L, p->name);
		if (lua_type(L, -1) == p->type) {
			lua_pop(L, 1);
			continue;
		} else {
			if (lua_isnil(L, -1)) {
				eprintln("error: required lua global %s not defined by builtin.lua!",
				    p->name);
			} else {
				eprintln("error: lua global %s has wrong type (expected %s, found %s)",
				    p->name,
				    lua_typename(L, p->type),
				    lua_typename(L, lua_type(L, -1)));
			}
			ok = false;
		}
	}
	if (!ok) {
		abort();
	}
}

void lua_deinit(void) {
	if (lua_lock_state_unchecked()) {
		lua_State *L = lua_get_state_unchecked();
		if (L != NULL) {
			 lua_getglobal(L, "_fire_unload");
			  lua_pushboolean(L, 1);
			lua_call(L, 1, 0);
			lua_close(L);
			lua_set_state_unchecked(NULL);
		}
		lua_unlock_state_unchecked();
	}
}
