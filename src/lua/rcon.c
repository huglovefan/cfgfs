#include "rcon.h"

#include "../rcon/session.h"
#include "../macros.h"

static int l_rcon_new(lua_State *L) {
	const char *host = luaL_checkstring(L, 1);
	lua_Integer port = luaL_checkinteger(L, 2);
	const char *password = lua_tostring(L, 3);
	struct rcon_session *sess = rcon_connect(host, (int)port, password);
	lua_settop(L, 0);
	if (sess) {
		lua_pushlightuserdata(L, sess);
		luaL_newmetatable(L, "cfgfs_rcon");
		lua_setmetatable(L, -2);
		assert(lua_gettop(L) == 1);
		return 1;
	} else {
		luaL_pushfail(L);
		return 1;
	}
}

static int l_rcon_run_cfg(lua_State *L) {
	struct rcon_session *sess = luaL_checkudata(L, 1, "cfgfs_rcon");
	size_t len;
	const char *cfg = luaL_checklstring(L, 2, &len);
	int nowait = lua_toboolean(L, 3);
	if (len > max_cfg_size_rcon) goto toolong;
	lua_pushinteger(L, rcon_run_cfg(sess, cfg, nowait));
	return 1;
toolong:
	return luaL_error(L, "command string too long for rcon");
}

static int l_rcon_delete(lua_State *L) {
	struct rcon_session *sess = luaL_checkudata(L, 1, "cfgfs_rcon");
	rcon_disconnect(sess);
	return 0;
}

const luaL_Reg l_rcon_fns[] = {
	{"_rcon_new", l_rcon_new},
	{"_rcon_run_cfg", l_rcon_run_cfg},
	{"_rcon_delete", l_rcon_delete},
	{NULL, NULL},
};
