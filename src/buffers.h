#pragma once

#include <lauxlib.h>

#include "buffer_list.h"

extern struct buffer_list buffers;
extern struct buffer_list init_cfg;

extern const luaL_Reg l_buffers_fns[];
