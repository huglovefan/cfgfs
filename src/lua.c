#define _GNU_SOURCE // unlocked_stdio
#include "lua.h"
#include "buffers.h"
#include "macros.h"
#include "main.h"
#include "keys.h"
#include "click.h"
#include "attention.h"
#include "cli_output.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

// -----------------------------------------------------------------------------

static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;

void LUA_LOCK(void) { pthread_mutex_lock(&lua_mutex); }
void LUA_UNLOCK(void) { pthread_mutex_unlock(&lua_mutex); }
bool LUA_TRYLOCK(void) { return (0 == pthread_mutex_trylock(&lua_mutex)); }
//                   confusingly this ^ returns 0 (false) if we got the lock

// -----------------------------------------------------------------------------

__attribute__((cold))
int lua_print_backtrace(lua_State *L) {
	if (!lua_checkstack(L, 2)) return 0;
	 lua_getglobal(L, "debug");
	  lua_getfield(L, -1, "traceback");
	  lua_rotate(L, -2, 1);
	 lua_pop(L, 1);
	 lua_call(L, 0, 1);
	 eprintln("%s", lua_tostring(L, -1));
	lua_pop(L, 1);
	return 0;
	// todo: change to luaL_traceback
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

// -----------------------------------------------------------------------------

// buffer-related

static char badchars[256];

__attribute__((constructor))
static void _init_badchars(void) {
	for (size_t c = 0; c < sizeof(badchars); c++) badchars[c] = 1;
#define ok_range(from, to) for (int i = from; i <= to; i++) badchars[i] = 0
	badchars[33] = 0;
	ok_range(35, 38);
	ok_range(42, 46);
	ok_range(48, 57);
	ok_range(60, 122);
	badchars[124] = 0;
	badchars[126] = 0;
	badchars[127] = 0;
#undef ok_range
	// ^ numbers gotten using a script
	// for each character c in 1-255:
	// - cfgf('help %s', c..c)
	// - check if the output is exactly 'help:  no cvar or command named '..c..c
	// this should get all whitespace/separator/invalid characters
}

static bool str_is_ok(const char *restrict s, size_t sz) {
	char bad = 0;
	for (size_t i = 0; i < sz; i++) bad |= badchars[(int)*s++];
	return !bad;
}

static int l_cmd_say(lua_State *L);
static int l_cmd(lua_State *L) {
	char buf[max_line_length+8];
	size_t len = 0;
	int l = lua_gettop(L);

	// note: first arg is the "cfg" table

	if (unlikely(l <= 1)) return 0;
	if (unlikely(l-1 > (ssize_t)max_argc)) goto toolong;

	// say requires special attention
	if (unlikely(l >= 3 &&
	             lua_tostring(L, 2) != NULL &&
	             (strcmp(lua_tostring(L, 2), "say") == 0 ||
	              strcmp(lua_tostring(L, 2), "say_team") == 0))) {
		return l_cmd_say(L);
	}

	for (int i = 2; i <= l; i++) {
		size_t sz;
		const char *s = lua_tolstring(L, i, &sz);
		if (unlikely(s == NULL)) goto typeerr;

		// rough safety check
		if (unlikely(len+sz > max_line_length)) goto toolong;

		if (likely(str_is_ok(s, sz))) {
			if (likely(len != 0 && buf[len-1] != '"')) {
				buf[len++] = ' ';
				// ^ don't need a space after a quote
			}
			memcpy(buf+len, s, sz);
			len += sz;
		} else {
			buf[len++] = '"';
			memcpy(buf+len, s, sz);
			len += sz;
			if (likely(i != l)) buf[len++] = '"';
			// ^ don't need to close the last quote
		}

		// this is now accurate
		if (unlikely(len > max_line_length)) goto toolong;
	}

	buffer_list_write_line(&buffers, buf, len);

	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cmd");
toolong:
	if (len > 40) len = 40;
	buf[len] = '\0';
	eprintln("warning: discarding too-long line: %s ...", buf);
	return lua_print_backtrace(L);
}

/*

version of l_cmd() for say
seems to work

bind('4', function () cmd.say('test                             test') end)
bind('5', function () cmd.say('test say ; // ::::::: {{{{{ test asd') end)
bind('6', function () cmd.say('test say with "embedded" quotes') end)
bind('7', function () cmd.say('test say without quotes') end)
bind('8', function () cmd.say('"test say with left quote') end)
bind('9', function () cmd.say('test say with right quote"') end)
bind('0', function () cmd.say('"test say with both quotes"') end)

*/
__attribute__((cold))
static int l_cmd_say(lua_State *L) {
	char buf[max_line_length+8];
	size_t len = 0;
	int l = lua_gettop(L);

	// note: first arg is the "cfg" table
	// note2: this is only called from l_cmd(). critical security checks are done there

	for (int i = 2; i <= l; i++) {
		size_t sz;
		const char *s = lua_tolstring(L, i, &sz);
		if (unlikely(s == NULL)) goto typeerr;

		// rough safety check
		if (unlikely(len+sz > max_line_length)) goto toolong;

		if (i == 3) {
			buf[len++] = '"';
		} else if (i > 3) {
			buf[len++] = ' ';
		}

		memcpy(buf+len, s, sz);
		len += sz;

		// this is now accurate
		if (unlikely(len > max_line_length)) goto toolong;
	}

	buf[len++] = '"';
	if (unlikely(len > max_line_length)) goto toolong;

	buffer_list_write_line(&buffers, buf, len);

	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cmd");
toolong:
	if (len > 40) len = 40;
	buf[len] = '\0';
	eprintln("warning: discarding too-long line: %s ...", buf);
	return lua_print_backtrace(L);
}

static int l_cfg(lua_State *L) {
	size_t sz;
	const char *s = lua_tolstring(L, 1, &sz);
	if (unlikely(s == NULL)) goto typeerr;
	if (unlikely(sz == 0)) return 0;
	if (unlikely(sz > max_line_length)) goto toolong; // assume it's not multiple lines
	buffer_list_write_line(&buffers, s, sz);
	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cfg()");
toolong:
	eprintln("warning: discarding too-long line: %.40s ...", s);
	return lua_print_backtrace(L);
}

// copies init_cfg to the buffer
static int l_init(lua_State *L) {
	(void)L;
	buffer_list_append_from_that_to_this(&buffers, &init_cfg);
	return 0;
}

// -----------------------------------------------------------------------------

// misc

static double getms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
static int l_ms(lua_State *L) {
	lua_pushnumber(L, getms());
	return 1;
}

static int l_click_after(lua_State *L) {
	click_after(lua_tonumber(L, 1));
	return 0;
}
static int l_click(lua_State *L) {
	(void)L;
	click();
	return 0;
}
static int l_opportunistic_click(lua_State *L) {
	(void)L;
	opportunistic_click();
	return 0;
}

static int l_get_attention(lua_State *L) {
	char *s = get_attention();
	lua_pushstring(L, s);
	free(s);
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

	pthread_mutex_init(&lua_mutex, NULL);

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);

	lua_atpanic(L, l_panic);

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
	lua_pop(L, 1);

	 lua_pushcfunction(L, l_cfg);
	lua_setglobal(L, "_cfg");
	 lua_pushcfunction(L, l_cmd);
	lua_setglobal(L, "_cmd");
	 lua_pushcfunction(L, l_ms);

	lua_setglobal(L, "_ms");
	 lua_pushcfunction(L, l_click);
	lua_setglobal(L, "_click");
	 lua_pushcfunction(L, l_click_after);
	lua_setglobal(L, "_click_after");
	 lua_pushcfunction(L, l_opportunistic_click);
	lua_setglobal(L, "_opportunistic_click");
	 lua_pushcfunction(L, l_get_attention);
	lua_setglobal(L, "_get_attention");

	 lua_pushcfunction(L, l_print);
	lua_setglobal(L, "_print");
	 lua_pushcfunction(L, l_eprint);
	lua_setglobal(L, "_eprint");

	 lua_pushcfunction(L, l_init);
	lua_setglobal(L, "_init");

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		eprintln("lua: getcwd: %s", strerror(errno));
		exit(1);
	}

	lua_pushstring(L, cwd); lua_setglobal(L, "cwd");

	lua_pushinteger(L, (lua_Integer)max_line_length); lua_setglobal(L, "max_line_length");
	lua_pushinteger(L, (lua_Integer)max_cfg_size); lua_setglobal(L, "max_cfg_size");
	lua_pushinteger(L, (lua_Integer)max_argc); lua_setglobal(L, "max_argc");

	lua_pushinteger(L, (lua_Integer)reported_cfg_size); lua_setglobal(L, "reported_cfg_size");

#define ARRAYSIZE(a) (sizeof(a)/sizeof(*(a)))

	lua_createtable(L, ARRAYSIZE(keys), 0); lua_setglobal(L, "num2key");
	lua_createtable(L, 0, ARRAYSIZE(keys));   lua_setglobal(L, "key2num");
	lua_createtable(L, 0, ARRAYSIZE(keys)*4); lua_setglobal(L, "bindfilenames");

	lua_getglobal(L, "num2key");
	for (size_t i = 0; i < ARRAYSIZE(keys); i++) {
		lua_pushinteger(L, (lua_Integer)i+1); // the number doesn't matter
		lua_pushstring(L, keys[i]);
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

	return L;

}
