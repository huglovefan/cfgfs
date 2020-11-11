#pragma once

// focusedness knower

enum game_window_activeness {
	activeness_unknown = -1, // probably didn't set cfgfs.game_window_title
	activeness_inactive = 0,
	activeness_active = 1,
};
extern _Atomic(enum game_window_activeness) game_window_is_active;

void attention_init(void *L);
void attention_deinit(void);

void attention_init_lua(void *L);
