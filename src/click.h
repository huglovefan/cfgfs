#pragma once

// click to make source exec a config at will

void click(void);

void click_after(double ms);

// click if there is stuff in the buffer
// !!!NOTE!!! this is only safe to call while LUA_LOCK is held
void opportunistic_click(void);
