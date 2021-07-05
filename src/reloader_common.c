#include "reloader.h"

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "macros.h"

void reloader_do_reload_internal(const char *path) {
	lua_State *L = lua_get_state("reloader");
	if (L == NULL) {
		perror("reloader: failed to lock lua state!");
		return;
	}

V	eprintln("reloader: reloading...");

	buffer_list_reset(&buffers);
	buffer_list_reset(&init_cfg);

	// .
	 lua_getglobal(L, "_reload_1");  // fn1
	  lua_pushstring(L, path);       // fn1 path
	 lua_call(L, 1, 1);              // rv1
	 buffer_list_swap(&buffers, &init_cfg);
	  lua_getglobal(L, "_reload_2"); // rv1 fn2
	  lua_rotate(L, -2, 1);          // fn2 rv1
	lua_call(L, 1, 0);               // .
	// .

V	eprintln("reloader: reloading done!");

	lua_release_state(L);
}

static int l_reloader_add_watch(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);

	const char *whynot = NULL;
	_Bool ok = reloader_add_watch(path, &whynot);

	if (ok) {
		lua_pushboolean(L, ok);
		return 1;
	} else {
		lua_pushboolean(L, ok);
		lua_pushstring(L, whynot);
		return 2;
	}
}

const luaL_Reg l_reloader_fns[] = {
	{"_reloader_add_watch", l_reloader_add_watch},
	{NULL, NULL},
};
