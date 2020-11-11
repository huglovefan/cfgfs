#pragma once

// click to make source exec a config at will

// click if there is stuff in the buffer
// !!!NOTE!!! this is only safe to call while LUA_LOCK is held
void opportunistic_click(void);

void click_init(void);
void click_deinit(void);

void click_init_lua(void *L);
