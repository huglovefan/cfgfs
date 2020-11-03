#pragma once

// focusedness knower

// returns the title of the currently active window
// free using free()
char *get_attention(void);

void attention_init(void *L);
void attention_deinit(void);

enum game_window_activeness {
	game_window_inactive = 0,
	game_window_active = 1,
	game_window_unknown = 2, // unknown, probably didn't set cfgfs.game_window_title
};
extern _Atomic(enum game_window_activeness) game_window_is_active;

void attention_init_lua(void *L);
