#pragma once

// formerly known as reload_thread
// reloads script.lua when it is edited

void reloader_init(void *L);
void reloader_deinit(void);
