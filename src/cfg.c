#include "cfg.h"

#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "macros.h"

// ~

static char badchars[256];

#define ok_range(from, to) for (int i = from; i <= to; i++) badchars[i] = 0

__attribute__((constructor))
static void _init_badchars(void) {
	memset(badchars, 1, sizeof(badchars));
	badchars[33] = 0;
	ok_range(35, 38);
	ok_range(42, 46);
	ok_range(48, 57);
	ok_range(60, 122);
	badchars[124] = 0;
	badchars[126] = 0;
	badchars[127] = 0;
	// ^ numbers gotten using a script
	// for each character c in 1-255:
	// - cfgf('help %s', c..c)
	// - check if the output is exactly 'help:  no cvar or command named '..c..c
	// this should get all whitespace/separator/invalid characters
}

#undef ok_range

static bool str_is_ok(const char *s, size_t sz) {
	if (unlikely(sz == 0)) return 0;
	char bad = 0;
	do bad |= badchars[(int)*s]; while (*++s);
	return !bad;
}

// -----------------------------------------------------------------------------

static int l_cmd_say(lua_State *L);
static int cmd_toolong(lua_State *L, char *buf, size_t len);

static int l_cmd(lua_State *L) {
	char buf[max_line_length+8];
	size_t len = 0;
	int top = lua_gettop(L);

	// note: first arg is the "cfg" table

	if (unlikely(top <= 1)) return 0;
	if (unlikely(top-1 > (ssize_t)max_argc)) goto toolong;

	if (unlikely(top >= 3 &&
	             lua_tostring(L, 2) != NULL &&
	             (strcmp(lua_tostring(L, 2), "say") == 0 ||
	              strcmp(lua_tostring(L, 2), "say_team") == 0))) {
		return l_cmd_say(L);
	}

	for (int i = 2; i <= top; i++) {
		size_t sz;
		const char *s = lua_tolstring(L, i, &sz);
		if (unlikely(s == NULL)) goto typeerr;

		// rough safety check
		if (unlikely(len+sz > max_line_length)) goto toolong;

		if (likely(str_is_ok(s, sz))) {
			if (len != 0) buf[len++] = ' ';
			memcpy(buf+len, s, sz);
			len += sz;
		} else {
			buf[len++] = '"';
			memcpy(buf+len, s, sz);
			len += sz;
			buf[len++] = '"';
		}

		// this is now accurate
		if (unlikely(len > max_line_length)) goto toolong;
	}

	buffer_list_write_line(&buffers, buf, len);

	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cmd");
toolong:
	return cmd_toolong(L, buf, len);
}

/*

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
	int top = lua_gettop(L);

	// note: first arg is the "cfg" table
	// note2: this is only called from l_cmd(). critical security checks are done there

	for (int i = 2; i <= top; i++) {
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
	return cmd_toolong(L, buf, len);
}

__attribute__((cold))
static int cmd_toolong(lua_State *L, char *buf, size_t len) {
	if (len > 40) len = 40;
	buf[len] = '\0';
	eprintln("warning: discarding too-long line: %s ...", buf);
	return lua_print_backtrace(L);
}

// -----------------------------------------------------------------------------

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
	assert(max_line_length >= 40);
	eprintln("warning: discarding too-long line: %.40s ...", s);
	return lua_print_backtrace(L);
}

// -----------------------------------------------------------------------------

void cfg_init_lua(void *L) {
	 lua_pushcfunction(L, l_cfg);
	lua_setglobal(L, "_cfg");
	 lua_pushcfunction(L, l_cmd);
	lua_setglobal(L, "_cmd");
}
