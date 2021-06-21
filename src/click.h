#pragma once

#include <lauxlib.h>

void do_click(void);
void do_click_internal_for_click_thread(void);

void click_init(void);
void click_deinit(void);

extern const luaL_Reg l_click_fns[];
