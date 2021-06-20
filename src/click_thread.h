#pragma once

#include <stdint.h>

void click_thread_init(void);
void click_thread_deinit(void);

_Bool click_thread_submit_click(unsigned int ms, uintptr_t *id_out);
_Bool click_thread_cancel_click(uintptr_t id);
