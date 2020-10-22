/*

things missing from luaposix
% make -B libvcr.so

*/

#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// types text to a terminal
static int l_ttype(lua_State *L) {
	int fd = lua_tointeger(L, 1);
	const char *str = lua_tostring(L, 2);
	if (unlikely(str == NULL)) goto badstring;

	int rv = 0;
	while (*str) rv |= ioctl(fd, TIOCSTI, str++);
	if (unlikely(rv)) goto error;
	// note: ioctl(TIOCSTI) takes a pointer to one character, not a whole string

	return 0;
badstring:
	return luaL_error(L, "ttype: second argument must be a string");
error:
	return luaL_error(L, "ttype: %s", strerror(errno));
}

// wait until a path is mounted as cfgfs
static int l_wait_cfgfs_mounted(lua_State *L) {
	const char *path = lua_tostring(L, 1);
	if (unlikely(path == NULL)) goto badpath;

	struct stat st;
	for (;;) {
		int rv = stat(path, &st);
		if (unlikely(rv == -1)) goto error;
		if (st.st_ino == 1) break; // root directory should be 1
		pthread_yield();
	}

	return 0;
badpath:
	return luaL_error(L, "wait_mounted: path must be a string");
error:
	return luaL_error(L, "wait_mounted: %s", strerror(errno));
}

int luaopen_libvcr(lua_State *L) {
	lua_settop(L, 0);
	 lua_newtable(L);
	  lua_pushstring(L, "ttype");
	   lua_pushcfunction(L, l_ttype);
	 lua_settable(L, -3);
	  lua_pushstring(L, "wait_cfgfs_mounted");
	   lua_pushcfunction(L, l_wait_cfgfs_mounted);
	 lua_settable(L, -3);
	 assert(lua_gettop(L) == 1);
	 return 1;
}
