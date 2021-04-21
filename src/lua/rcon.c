#include "rcon.h"

#include "../rcon/session.h"
#include "../macros.h"

static int l_rcon_new(lua_State *L) {
	const char *host = luaL_checkstring(L, 1);
	lua_Integer port = luaL_checkinteger(L, 2);
	const char *password = lua_tostring(L, 3);

	struct rcon_session *sess = rcon_connect(host, (int)port, password);
	if (sess) {
		lua_pushlightuserdata(L, sess);
		return 1;
	} else {
		luaL_pushfail(L);
		return 1;
	}
}

static int l_rcon_run_cfg(lua_State *L) {
	struct rcon_session *sess = lua_touserdata(L, 1);

	size_t len;
	const char *cfg = luaL_checklstring(L, 2, &len);
	if (len > max_cfg_size_rcon) goto toolong;

	int32_t id;
	if (0 == rcon_run_cfg(sess, cfg, &id)) {
		lua_pushinteger(L, id);
	} else {
		luaL_pushfail(L);
	}

	return 1;
toolong:
	return luaL_error(L, "command string too long for rcon");
}

static int l_rcon_delete(lua_State *L) {
	struct rcon_session *sess = lua_touserdata(L, 1);
	rcon_disconnect(sess);
	return 0;
}

const luaL_Reg l_rcon_fns[] = {
	{"_rcon_new", l_rcon_new},
	{"_rcon_run_cfg", l_rcon_run_cfg},
	{"_rcon_delete", l_rcon_delete},
	{NULL, NULL},
};
