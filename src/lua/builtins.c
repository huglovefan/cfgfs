#include "builtins.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <lualib.h>
#include <lauxlib.h>

#include "../attention.h"
#include "../buffers.h"
#include "../cfg.h"
#include "../cli_input.h"
#include "../cli_output.h"
#include "../cli_scrollback.h"
#include "../click.h"
#include "../keys.h"
#include "../lua.h"
#include "../macros.h"
#include "../main.h"
#include "../misc/string.h"
#include "../reloader.h"

#include "rcon.h"

// -----------------------------------------------------------------------------

// string functions

static int l_string_starts_with(lua_State *L) {
	size_t l1, l2;
	const char *s1 = luaL_checklstring(L, 1, &l1);
	const char *s2 = luaL_checklstring(L, 2, &l2);
	lua_pushboolean(L, (l1 >= l2 && memcmp(s1, s2, l2) == 0));
	return 1;
}

static int l_string_ends_with(lua_State *L) {
	size_t l1, l2;
	const char *s1 = luaL_checklstring(L, 1, &l1);
	const char *s2 = luaL_checklstring(L, 2, &l2);
	lua_pushboolean(L, (l1 >= l2 && memcmp(s1+l1-l2, s2, l2) == 0));
	return 1;
}

// string.after('/cfgfs/buffer.cfg', '/cfgfs/') -> 'buffer.cfg'
static int l_string_after(lua_State *L) {
	size_t l1, l2;
	const char *s1 = luaL_checklstring(L, 1, &l1);
	const char *s2 = luaL_checklstring(L, 2, &l2);
	if (likely(l1 >= l2 && memcmp(s1, s2, l2) == 0)) {
		lua_pushlstring(L, s1+l2, l1-l2);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// string.before('buffer.cfg', '.cfg') -> 'buffer'
static int l_string_before(lua_State *L) {
	size_t l1, l2;
	const char *s1 = luaL_checklstring(L, 1, &l1);
	const char *s2 = luaL_checklstring(L, 2, &l2);
	if (likely(l1 >= l2 && memcmp(s1+l1-l2, s2, l2) == 0)) {
		lua_pushlstring(L, s1, l1-l2);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// string.between('/cfgfs/buffer.cfg', '/cfgfs/', '.cfg') -> 'buffer'
static int l_string_between(lua_State *L) {
	size_t l1, l2, l3;
	const char *s1 = luaL_checklstring(L, 1, &l1);
	const char *s2 = luaL_checklstring(L, 2, &l2);
	const char *s3 = luaL_checklstring(L, 3, &l3);
	if (likely(l1 >= l2+l3 && memcmp(s1, s2, l2) == 0 && memcmp(s1+l1-l3, s3, l3) == 0)) {
		lua_pushlstring(L, s1+l2, l1-l2-l3);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

// removes whitespace from the start and end
static int l_string_trim(lua_State *L) {
	const char *s = luaL_checkstring(L, 1);
	const char *first, *last;

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
}

// https://linux.die.net/man/3/memfrob
static int l_string_frobnicate(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	char *newstr = malloc(len+1);
	memcpy(newstr, s, len+1);
#if defined(__linux__)
	memfrob(newstr, len);
#else
	for (unsigned int i = 0; i < len; i++) newstr[i] = newstr[i]^42;
#endif
	lua_pushstring(L, newstr);
	free(newstr);
	return 1;
}

// https://perldoc.perl.org/functions/chomp
// (this does just the newline thing)
static int l_string_chomp(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	if (likely(len != 0 && s[len-1] == '\n')) {
		lua_pushlstring(L, s, len-1);
	} else {
		lua_settop(L, 1); // ignore excess arguments
	}
	return 1;
}

// -----------------------------------------------------------------------------

static int l_ms(lua_State *L) {
	lua_pushnumber(L, mono_ms());
	return 1;
}

// -----------------------------------------------------------------------------

#define cfgfs_memdup(s, sz) \
	({ \
		const char *_memdup_s = (s); \
		size_t _memdup_sz = (sz); \
		char *_memdup_p = malloc(_memdup_sz); \
		memcpy(_memdup_p, _memdup_s, _memdup_sz); \
		_memdup_p; \
	})

// printing

static int l_print(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	cli_lock_output();
	 fwrite_unlocked(s, 1, len, stdout);
	 fputc_unlocked('\n', stdout);
	 cli_scrollback_add_output(cfgfs_memdup(s, len+1));
	cli_unlock_output();
	return 0;
}
static int l_eprint(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	cli_lock_output();
	 fwrite_unlocked(s, 1, len, stderr);
	 fputc_unlocked('\n', stderr);
	 cli_scrollback_add_output(cfgfs_memdup(s, len+1));
	cli_unlock_output();
	return 0;
}

// multiple argument versions
// only supports strings because converting other types may throw
// (can't allow that while holding the cli_output lock)

static int l_printv(lua_State *L) {
	cli_lock_output();
	int i, top = lua_gettop(L);
	for (i = 1; i <= top; i++) {
		if (unlikely(lua_type(L, i) != LUA_TSTRING)) goto typeerr;
		size_t len;
		const char *s = lua_tolstring(L, i, &len);
		fwrite_unlocked(s, 1, len, stdout);
	}
	fputc_unlocked('\n', stdout);
	cli_unlock_output();
	return 0;
typeerr:
	fputc_unlocked('\n', stdout);
	cli_unlock_output();
	return luaL_error(L, "printv: argument %d is not a string", i);
}
static int l_eprintv(lua_State *L) {
	cli_lock_output();
	int i, top = lua_gettop(L);
	for (i = 1; i <= top; i++) {
		if (unlikely(lua_type(L, i) != LUA_TSTRING)) goto typeerr;
		size_t len;
		const char *s = lua_tolstring(L, i, &len);
		fwrite_unlocked(s, 1, len, stderr);
	}
	fputc_unlocked('\n', stderr);
	cli_unlock_output();
	return 0;
typeerr:
	fputc_unlocked('\n', stderr);
	cli_unlock_output();
	return luaL_error(L, "eprintv: argument %d is not a string", i);
}

// -----------------------------------------------------------------------------

// takes a config name like "scout.cfg" and makes sure there's one like that
//  somewhere in the game's search path
// why: cfgfs.notify_cfgfs in builtin.lua doesn't work reliably if the config
//  doesn't exist, so it uses this to create them
// (reading a nonexistent config generates fewer filesystem events so the
//  unmask_next mechanism doesn't work properly for them)
static int l_ensure_cfg_exists(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	bool rv = false;

	if (!getenv("GAMEDIR")) return 0;

	// unsupported, but likely to already exist if it has such a fancy path
	if (strchr(name, '/') || strchr(name, '\\')) return 0;

	char stkdata[128];
	struct string s = string_new_empty_from_stkbuf(stkdata, sizeof(stkdata));
	s.autogrow = 1;

	string_set_contents_from_fmt(&s, "%s/cfg/%s", getenv("GAMEDIR"), name);
	if (0 == access(s.data, F_OK)) {
		rv = true;
		goto out;
	}

	string_set_contents_from_fmt(&s, "%s/custom", getenv("GAMEDIR"));

	struct dirent **namelist = NULL;
	int cnt = scandir(s.data, &namelist, NULL, alphasort);
	for (int i = 0; i < cnt; i++) {
		const char *entname = namelist[i]->d_name;
		// ignore dotdot/hidden and cfgfs
		if (!rv && entname[0] != '.' && 0 != strcmp(entname, "!cfgfs")) {
			string_set_contents_from_fmt(&s, "%s/custom/%s/cfg/%s",
			    getenv("GAMEDIR"), entname, name);
			rv = (0 == access(s.data, F_OK));
		}
		free(namelist[i]);
	}
	free(namelist);

out:
	if (!rv) {
		string_set_contents_from_fmt(&s, "%s/cfg/%s", getenv("GAMEDIR"), name);
		FILE *f = fopen(s.data, "a");
		if (f != NULL) {
			fprintf(f, "// empty file created by cfgfs (notify_cfgs)\n");
			fclose(f);
		} else {
V			eprintln("ensure_cfg_exists: fopen %s: %s", name, strerror(errno));
		}
	}
	string_free(&s);
	return 0;
}

// -----------------------------------------------------------------------------

static int l_cfgfs_unmount(lua_State *L) {
	(void)L;
	main_quit();
	return 0;
}

// -----------------------------------------------------------------------------

static const luaL_Reg fns_g[] = {
	{"_fatal", (lua_CFunction)l_panic},
	{"_print", l_print},
	{"_eprint", l_eprint},
	{"printv", l_printv},
	{"eprintv", l_eprintv},
	{"_ms", l_ms},
	{"_get_locker", l_get_locker},
	{"_ensure_cfg_exists", l_ensure_cfg_exists},
	{"_cfgfs_unmount", l_cfgfs_unmount},
	// main.c
	{"_notify_list_set", (lua_CFunction)l_notify_list_set},
	// reloader.c
	{"_reloader_add_watch", (lua_CFunction)l_reloader_add_watch},
	{NULL, NULL},
};
static const luaL_Reg fns_s[] = {
	{"starts_with", l_string_starts_with},
	{"ends_with", l_string_ends_with},
	{"after", l_string_after},
	{"before", l_string_before},
	{"between", l_string_between},
	{"trim", l_string_trim},
	{"frobnicate", l_string_frobnicate},
	{"chomp", l_string_chomp},
	{NULL, NULL},
};

void lua_define_builtins(void *L) {

	assert(0 == lua_gettop(L));

	 lua_getglobal(L, "_G");
	 luaL_setfuncs(L, fns_g, 0);
	 luaL_setfuncs(L, l_buffers_fns, 0);
	 luaL_setfuncs(L, l_cfg_fns, 0);
	 luaL_setfuncs(L, l_cli_input_fns, 0);
	 luaL_setfuncs(L, l_click_fns, 0);
	 luaL_setfuncs(L, l_rcon_fns, 0);
	lua_pop(L, 1);

#if defined(__linux__) || defined(__FreeBSD__)
	lua_pushboolean(L, 1); lua_setglobal(L, "__linux__");
#else
	lua_pushboolean(L, 0); lua_setglobal(L, "__linux__");
#endif

#if defined(__CYGWIN__)
	lua_pushboolean(L, 1); lua_setglobal(L, "__CYGWIN__");
#else
	lua_pushboolean(L, 0); lua_setglobal(L, "__CYGWIN__");
#endif

	 lua_getglobal(L, "string");
	 luaL_setfuncs(L, fns_s, 0);
	lua_pop(L, 1);

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

	if (LUA_OK != luaL_dostring(L, "\
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
	")) lua_error(L);

	_Static_assert(sizeof(AGPL_SOURCE_URL) > 1, "AGPL_SOURCE_URL not set to a valid value");
	lua_pushstring(L, AGPL_SOURCE_URL); lua_setglobal(L, "agpl_source_url");

	assert(0 == lua_gettop(L));

}
