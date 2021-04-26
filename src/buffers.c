#include "buffers.h"

#include <lua.h>

struct buffer_list buffers;
struct buffer_list init_cfg;

// copies init_cfg to the buffer
static int l_init(lua_State *L) {
	(void)L;
	buffer_list_append_from_that_to_this(&buffers, &init_cfg);
	return 0;
}

__attribute__((minsize))
static int l_buffer_is_empty(lua_State *L) {
	lua_pushboolean(L, buffer_list_is_empty(&buffers));
	return 1;
}

const luaL_Reg l_buffers_fns[] = {
	{"_init", l_init},
	{"_buffer_is_empty", l_buffer_is_empty},
	{NULL, NULL},
};
