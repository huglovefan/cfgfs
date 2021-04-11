#pragma once

#if defined(REPORTED_CFG_SIZE)
 #define reported_cfg_size ((size_t)(REPORTED_CFG_SIZE))
#else
 #define reported_cfg_size ((size_t)4096)
#endif

void main_quit(void);

int l_notify_list_set(void *L);
