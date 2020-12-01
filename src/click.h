#pragma once

// click to make source exec a config at will

// i didn't like this feature at first because it seemed so cheaty compared to
//  everything else in cfgfs but
// clicking one (1) key is nothing compared to what many windows gamers are
//  already doing with keyboard macro software
// autohotkey doesn't have a reputation for getting you banned so i think this
//  should be okay

// unlock the lua lock and click if there is stuff in the buffer
void opportunistic_click_and_unlock(void);

void click_init(void);
void click_deinit(void);

void click_init_lua(void *L);
