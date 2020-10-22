#undef WITH_VCR
#define WITH_VCR
#include "vcr.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "macros.h"

double vcr_get_timestamp(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static pthread_mutex_t vcr_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *vcr_outfile;
static lua_State *L;

void _vcr_begin(void) {
	pthread_mutex_lock(&vcr_lock);
	if (unlikely(L == NULL)) {
		L = luaL_newstate();
		luaL_openlibs(L);
		if (luaL_dostring(L, "\
		_init=function()t={}end\
		_add=function(k,v)t[#t+1]=string.format('%s=%q',k,v)end\
		_finish=function()return'event { '..table.concat(t,', ')..' };\\n'end\
		") != LUA_OK) lua_error(L);
		vcr_outfile = fopen("vcr.log", "w");
		static char buf[1024*1024*20];
		setvbuf(vcr_outfile, buf, _IOFBF, sizeof(buf));
	}
D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_init");
	lua_call(L, 0, 0);
D	assert(lua_gettop(L) == 0);
}

void vcr_add_string(const char *k, const char *v) {
D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_add");
	  lua_pushstring(L, k);
	   lua_pushstring(L, v);
	lua_call(L, 2, 0);
D	assert(lua_gettop(L) == 0);
}
void vcr_add_double(const char *k, double v) {
D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_add");
	  lua_pushstring(L, k);
	   lua_pushnumber(L, v);
	lua_call(L, 2, 0);
D	assert(lua_gettop(L) == 0);
}
void vcr_add_integer(const char *k, long long v) {
D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_add");
	  lua_pushstring(L, k);
	   lua_pushinteger(L, v);
	lua_call(L, 2, 0);
D	assert(lua_gettop(L) == 0);
}

void _vcr_end(void) {
D	assert(lua_gettop(L) == 0);
	 lua_getglobal(L, "_finish");
	 lua_call(L, 0, 1);
	 size_t sz;
	 const char *s = lua_tolstring(L, -1, &sz);
	 fwrite(s, 1, sz, vcr_outfile);
	lua_pop(L, 1);
D	assert(lua_gettop(L) == 0);
	pthread_mutex_unlock(&vcr_lock);
}

__attribute__((cold))
void vcr_save(void) {
	pthread_mutex_lock(&vcr_lock);
	if (vcr_outfile != NULL) fflush(vcr_outfile);
	pthread_mutex_unlock(&vcr_lock);
}
