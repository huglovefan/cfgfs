#pragma once

#include <stdbool.h>

typedef struct lua_State lua_State;

bool lua_init(void);

int lua_do_nothing(lua_State *L);

// these are left on the stack for fast(?) access
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

lua_State *lua_get_state(void);
void lua_release_state(lua_State *);
void lua_lock_state(void);
void lua_unlock_state(void);
lua_State *lua_get_state_already_locked(void);
void lua_unlock_state_no_click(void);
void lua_unlock_state_and_click(void);
void lua_deinit(void);

#define lua_release_state_and_click(L) ({ (void)(L); lua_unlock_state_and_click(); })
#define lua_release_state_no_click(L) ({ (void)(L); lua_unlock_state_no_click(); })
