#pragma once

// focusedness knower

void attention_init(void *L);

// returns the title of the currently active window
// free using free()
char *get_attention(void);
