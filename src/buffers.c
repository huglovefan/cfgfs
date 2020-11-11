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

void buffers_init_lua(void *L) {
	 lua_pushcfunction(L, l_init);
	lua_setglobal(L, "_init");
}
