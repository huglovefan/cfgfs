#pragma once

// click to make source exec a config at will

// i didn't like this feature at first because it seemed so cheaty compared to
//  everything else in cfgfs but
// clicking one (1) key is nothing compared to what many windows gamers are
//  already doing with keyboard macro software
// autohotkey doesn't have a reputation for getting you banned so i think this
//  should be okay

void do_click(void);

void click_init(void);
void click_deinit(void);

void click_init_lua(void *L);

void click_init_threadattr(void);
