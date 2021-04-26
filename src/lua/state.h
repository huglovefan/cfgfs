#pragma once

#include <stdbool.h>

typedef struct lua_State lua_State;

// these are left on the stack for fast(?) access
// grep for uses before changing
#define GET_CONTENTS_IDX	1
#define GAME_CONSOLE_OUTPUT_IDX	2
#define MESSAGE_IDX		3
#define stack_is_clean(L) (lua_gettop(L) == MESSAGE_IDX && \
                           lua_type(L, GET_CONTENTS_IDX)        == LUA_TFUNCTION && \
                           lua_type(L, GAME_CONSOLE_OUTPUT_IDX) == LUA_TFUNCTION && \
                           lua_type(L, MESSAGE_IDX)             == LUA_TFUNCTION)
#define stack_is_clean_quick(L) (lua_gettop(L) == MESSAGE_IDX)

lua_State *lua_get_state_real(const char *who);
#define lua_get_state(who) lua_get_state_real("" who "")

lua_State *lua_get_state_already_locked(void);

void lua_release_state(lua_State *);

bool lua_lock_state_real(const char *who);
#define lua_lock_state(who) lua_lock_state_real("" who "")

void lua_unlock_state(void);

void lua_unlock_state_no_click(void);
void lua_unlock_state_and_click(void);

#define lua_release_state_and_click(L) ({ (void)(L); lua_unlock_state_and_click(); })
#define lua_release_state_no_click(L) ({ (void)(L); lua_unlock_state_no_click(); })

int l_get_locker(lua_State *L);
const char *lua_get_locker(lua_State *L);
