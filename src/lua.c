#include "lua.h"
#include "buffers.h"
#include "macros.h"
#include "main.h"
#include "keys.h"
#include "click.h"

#include <unistd.h>

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
	 fprintf(stderr, "%s\n", lua_tostring(L, -1));
	lua_pop(L, 1);
	return 0;
	// was there a C version of the backtrace function?
}

__attribute__((cold))
static int l_panic(lua_State *L) {
	 fprintf(stderr, "fatal error: %s\n", lua_tostring(L, -1));
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
	badchars['+'] = 0;
	badchars['-'] = 0;
	badchars['.'] = 0;
	badchars['/'] = 0;
	badchars['_'] = 0;
	for (int c = '0'; c <= '9'; c++) badchars[c] = 0;
	for (int c = 'A'; c <= 'Z'; c++) badchars[c] = 0;
	for (int c = 'a'; c <= 'z'; c++) badchars[c] = 0;
}

// https://code.woboq.org/userspace/glibc/string/strspn.c.html
// i wonder if this is any better than just doing it normally
// i wish there was a way to let the compiler choose the implementation

static bool str_is_ok(const char *restrict s, size_t sz) {
	char b1 = 0, b2 = 0, b3 = 0, b4 = 0;
	while (sz >= 4) {
		b1 = badchars[(int)s[0]];
		b2 = badchars[(int)s[1]];
		b3 = badchars[(int)s[2]];
		b4 = badchars[(int)s[3]];
		if (unlikely(b1|b2|b3|b4)) return 0;
		sz -= 4;
		s += 4;
	}
	switch (sz) {
	case 3: b3 = badchars[(int)s[2]]; __attribute__((fallthrough));
	case 2: b2 = badchars[(int)s[1]]; __attribute__((fallthrough));
	case 1: b1 = badchars[(int)s[0]]; __attribute__((fallthrough));
	case 0: return !(b1|b2|b3);
	default: __builtin_unreachable();
	}
}

static int l_cmd(lua_State *L) {
	char buf[max_line_length+8];
	size_t len = 0;
	int l = lua_gettop(L);

	// nasty hack for SAY
	// if the say command's "command line" starts with a quote, it snips
	//  it off and also removes the last character (even if it's not a quote)
	// fix by always putting a quote before the first arg, and also adding
	//  one to the end
	// this way saying things with and without quotes works as expected
	// (should say have its own version of this function?)
	bool say_hazard = false;
	if (unlikely(l >= 3 &&
	             lua_tostring(L, 2) != NULL &&
	             (strcmp(lua_tostring(L, 2), "say") == 0 ||
	              strcmp(lua_tostring(L, 2), "say_team") == 0))) {
		say_hazard = true;
	}

	if (unlikely(l-1 > (ssize_t)max_argc)) goto toolong;

	// note: first arg is the "cfg" table

	for (int i = 2; i <= l; i++) {
		size_t sz;
		const char *s = lua_tolstring(L, i, &sz);
		if (unlikely(s == NULL)) goto typeerr;

		// rough safety check
		if (unlikely(len+sz > max_line_length)) goto toolong;

		if (likely(say_hazard || str_is_ok(s, sz))) {
			if (unlikely(say_hazard && i == 3)) {
				buf[len++] = '"';
			} else if (likely(len != 0 && buf[len-1] != '"')) {
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

	if (unlikely(say_hazard)) {
		buf[len++] = '"';
		if (unlikely(len > max_line_length)) goto toolong;
	}

	if (likely(len != 0)) buffer_list_write_line(&buffers, buf, len);

	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cmd");
toolong:
	if (len > 40) len = 40;
	buf[len] = '\0';
	cli_println("warning: discarding too-long line: %s ...", buf);
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
	cli_println("warning: discarding too-long line: %.40s ...", s);
	return lua_print_backtrace(L);
}

// copies init_cfg to the buffer
static int l_init(lua_State *L) {
	(void)L;
	buffer_list_append_from_that_to_this(&buffers, &init_cfg);
	return 0;
}

static double tsms(const struct timespec *ts) {
	return (double)ts->tv_sec * 1000.0 + (double)ts->tv_nsec / 1000000.0;
}

static int l_ms(lua_State *L) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	lua_pushnumber(L, tsms(&ts));
	return 1;
}

static int l_click(lua_State *L) {
	click(lua_tonumber(L, 1));
	return 0;
}

// -----------------------------------------------------------------------------

static int l_print(lua_State *L) {
	const char *s = lua_tostring(L, 1);
	if (unlikely(s == NULL)) return 0;
	cli_println("%s", s);
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

	 lua_pushcfunction(L, l_print);
	lua_setglobal(L, "_print");
	 lua_pushcfunction(L, l_print);
	lua_setglobal(L, "_eprint");

	 lua_pushcfunction(L, l_init);
	lua_setglobal(L, "_init");

	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		perror("getcwd");
		exit(1);
	}

	lua_pushstring(L, cwd); lua_setglobal(L, "cwd");

	lua_pushinteger(L, (lua_Integer)max_line_length); lua_setglobal(L, "max_line_length");
	lua_pushinteger(L, (lua_Integer)max_cfg_size); lua_setglobal(L, "max_cfg_size");
	lua_pushinteger(L, (lua_Integer)max_argc); lua_setglobal(L, "max_argc");

	lua_pushinteger(L, (lua_Integer)reported_cfg_size); lua_setglobal(L, "reported_cfg_size");

#define ARRAYSIZE(a) (sizeof(a)/sizeof(*(a)))

	lua_createtable(L, ARRAYSIZE(keys)-1, 1); lua_setglobal(L, "num2key");
	lua_createtable(L, 0, ARRAYSIZE(keys));   lua_setglobal(L, "key2num");
	lua_createtable(L, 0, ARRAYSIZE(keys)*2); lua_setglobal(L, "bindfilenames");

	lua_getglobal(L, "num2key");
	for (size_t i = 0; i < ARRAYSIZE(keys); i++) {
		lua_pushinteger(L, (lua_Integer)i);
		lua_pushstring(L, keys[i]);
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (luaL_dostring(L, "\
	for n = 0, #num2key do\
		local key = num2key[n]\
		local down = string.format('/cfgfs/keys/+%d.cfg', n)\
		local up = string.format('/cfgfs/keys/-%d.cfg', n)\
		bindfilenames[down] = {name = key, pressed = true}\
		bindfilenames[up] = {name = key, pressed = false}\
		key2num[key] = n\
	end\
	") != LUA_OK) lua_error(L);

	return L;

}

// -----------------------------------------------------------------------------
