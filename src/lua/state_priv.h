#pragma once

void lua_set_state_unchecked(void *L);
void *lua_get_state_unchecked(void);

_Bool lua_lock_state_unchecked(void);
void lua_unlock_state_unchecked(void);
