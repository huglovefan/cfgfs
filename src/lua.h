#pragma once

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdbool.h>

lua_State *lua_init(void);

int lua_print_backtrace(lua_State *L);

// lock that protects
// - the main lua state
// - buffers in buffers.c (since they can be modified from lua)
void LUA_LOCK(void);
void LUA_UNLOCK(void);
bool LUA_TRYLOCK(void);

// these are left on the stack (by main.c, reloader.c) for fast(?) access
#define HAVE_FILE_IDX		1
#define GET_CONTENTS_IDX	2
#define UNMASK_NEXT_IDX		3
#define CFG_BLACKLIST_IDX	4

#define stack_is_clean(L) (lua_gettop(L) == CFG_BLACKLIST_IDX)
