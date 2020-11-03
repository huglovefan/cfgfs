#pragma once

#include <stdbool.h>

typedef struct lua_State lua_State;

lua_State *lua_init(void);

// lock that protects
// - the main lua state
// - buffers in buffers.c (since they can be modified from lua)
void LUA_LOCK(void);
void LUA_UNLOCK(void);
bool LUA_TRYLOCK(void);

// these are left on the stack (by main.c, reloader.c) for fast(?) access
// grep for uses before changing
#define GET_CONTENTS_IDX	1
#define UNMASK_NEXT_IDX		2
#define CFG_BLACKLIST_IDX	3
#define stack_is_clean(L) (lua_gettop(L) == CFG_BLACKLIST_IDX)
