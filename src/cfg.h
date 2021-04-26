#pragma once

#include <lauxlib.h>

#define max_line_length ((size_t)510)
#define max_cfg_size    ((size_t)1048576)
#define max_argc        (63)

extern const luaL_Reg l_cfg_fns[];

void cfg_init_badchars(void);
