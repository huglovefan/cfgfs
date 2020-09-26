#pragma once

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdbool.h>

// create and init a new lua state
lua_State *lua_init(void);

int lua_print_backtrace(lua_State *L);

// lock taken by
// - main thread when calling into lua
// - inotify thread while reinitializing the lua state
// - inotify thread while reinitializing the buffers
// - main thread while touching the buffers outside a lua call
void LUA_LOCK(void);
void LUA_UNLOCK(void);
bool LUA_TRYLOCK(void);

#define HAVE_FILE_IDX		1
#define GET_CONTENTS_IDX	2

#define stack_is_clean(L) (lua_gettop(L) == 2)
// ^ see also *_IDX in main.c (the number here is the last of those)
