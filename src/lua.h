#pragma once

#include <stdbool.h>

typedef struct lua_State lua_State;

lua_State *lua_init(void);

int lua_print_backtrace(lua_State *L);
int lua_do_nothing(lua_State *L);

// lock that protects
// - the main lua state
// - buffers in buffers.c (since they can be modified from lua)
void LUA_LOCK(void);
void LUA_UNLOCK(void);
bool LUA_TRYLOCK(void);
bool LUA_TIMEDLOCK(double);

// these are left on the stack (by main.c, reloader.c) for fast(?) access
// grep for uses before changing
#define GET_CONTENTS_IDX	1
#define UNMASK_NEXT_IDX		2
#define GAME_CONSOLE_OUTPUT_IDX	3
#define CFG_BLACKLIST_IDX	4
#define stack_is_clean(L) (lua_gettop(L) == CFG_BLACKLIST_IDX && \
                           lua_type(L, GET_CONTENTS_IDX)        == LUA_TFUNCTION && \
                           lua_type(L, UNMASK_NEXT_IDX)         == LUA_TTABLE && \
                           lua_type(L, GAME_CONSOLE_OUTPUT_IDX) == LUA_TFUNCTION && \
                           lua_type(L, CFG_BLACKLIST_IDX)       == LUA_TTABLE)
