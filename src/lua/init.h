#pragma once

_Bool lua_init(void);
void lua_deinit(void);

__attribute__((noreturn))
int l_panic(void *L);
