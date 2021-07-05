#pragma once

#include <lauxlib.h>

/// reloader_<implementation>.c

void reloader_init(void);
void reloader_deinit(void);

// add a path to the watch list
_Bool reloader_add_watch(const char *path, const char **errmsg);

// manually request a reload
void reloader_reload(void);

/// reloader_common.c

extern const luaL_Reg l_reloader_fns[];

// path: the file that caused this reload (if any)
void reloader_do_reload_internal(const char *path);
