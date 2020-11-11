#pragma once

#include "buffer_list.h"

extern struct buffer_list buffers;
extern struct buffer_list init_cfg;

void buffers_init_lua(void *L);
