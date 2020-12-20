#include "buffers.h"

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lua.h>
#endif

struct buffer_list buffers;
struct buffer_list init_cfg;

// copies init_cfg to the buffer
static int l_init(lua_State *L) {
	(void)L;
	buffer_list_append_from_that_to_this(&buffers, &init_cfg);
	return 0;
}

static int l_buffer_is_empty(lua_State *L) {
	lua_pushboolean(L, buffer_list_is_empty(&buffers));
	return 1;
}

void buffers_init_lua(void *L) {
	 lua_pushcfunction(L, l_init);
	lua_setglobal(L, "_init");
	 lua_pushcfunction(L, l_buffer_is_empty);
	lua_setglobal(L, "_buffer_is_empty");
}
