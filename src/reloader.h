#pragma once

void reloader_init(void);
void reloader_deinit(void);

void reloader_reload(void);

int l_reloader_add_watch(void *L);
