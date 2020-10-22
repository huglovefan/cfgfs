#include "reload_thread.h"
#include "lua.h"
#include "buffers.h"
#include "macros.h"

#include <sys/inotify.h>
#include <unistd.h>

// -----------------------------------------------------------------------------

static pthread_t thread = 0;
static int g_fd = -1;
static int g_wd = -1;

__attribute__((cold))
static void *cfgfs_inotify(void *ud) {
	set_thread_name("cf_lua_reloader");
	lua_State *L = (lua_State *)ud;
	char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
	for (;;) {
		g_wd = inotify_add_watch(g_fd, (getenv("CFGFS_SCRIPT") ?: "./script.lua"), IN_MODIFY);
		// todo: apparently this may (or may not) be leaking fds but doing it the right way didn't work when i first wrote it and i didn't change it after that so it's still broken but i wrote that other thing where it worked so i should try to do that here too maybe
		ssize_t rv = read(g_fd, buf, sizeof(buf));
		if (rv <= 0) break;
		if (g_fd == -1) break;
		LUA_LOCK();
		buffer_list_reset(&buffers);
		buffer_list_reset(&init_cfg);
		lua_getglobal(L, "_reload_1");
		 lua_call(L, 0, 1);
		buffer_list_swap(&buffers, &init_cfg);
		  lua_getglobal(L, "_reload_2");
		  lua_rotate(L, -2, 1);
		lua_call(L, 1, 0);
		LUA_UNLOCK();
	}
	pthread_exit(NULL);
	return NULL;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
void reload_thread_start(void *L) {
	g_fd = inotify_init();
	pthread_create(&thread, NULL, cfgfs_inotify, (void *)L);
}

__attribute__((cold))
void reload_thread_stop(void) {
	if (thread != 0) {
		int fd = g_fd, wd = g_wd;
		g_fd = g_wd = -1;
		inotify_rm_watch(fd, wd);
		pthread_join(thread, NULL);
		thread = 0;
	}
}

// -----------------------------------------------------------------------------
