#pragma once

enum attn {
	attn_unknown = -1, // probably didn't set cfgfs.game_window_title
	attn_inactive = 0,
	attn_active = 1,
};
extern _Atomic(enum attn) game_window_is_active;

void attention_init(void *L);
void attention_deinit(void);

void attention_init_lua(void *L);
