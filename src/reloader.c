#include "reloader.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <unistd.h>

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lua.h>
#endif

#include "buffers.h"
#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"

// bug: editing the script with nano makes this get in an infinite loop. does it
// modify the file in place instead of replacing it? why does that break this?

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_exit = 1,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

static bool wait_for_event(const char *path, int fd) {
	bool success = false;

	// just re-add the watch each time
	// some editors replace the file instead of modifying the existing one
	int wd = inotify_add_watch(fd, path, IN_MODIFY);
	check_minus1(wd, "reloader: inotify_add_watch", goto out);

	struct pollfd fds[2] = {
		{.fd = fd,         .events = POLLIN},
		{.fd = msgpipe[0], .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			perror("reloader: poll");
			break;
		}
		if (unlikely((fds[0].revents|fds[1].revents) & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_exit:
				goto out;
			default:
D				assert(0);
				__builtin_unreachable();
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
			ssize_t in_rv = read(fd, buf, sizeof(buf));
			check_minus1(in_rv, "reloader: read", goto out);
			success = true;
			goto out;
		}
	}
out:
	if (wd != -1) inotify_rm_watch(fd, wd);
	return success;
}

static void do_reload(void) {
	lua_State *L = lua_get_state();

	buffer_list_reset(&buffers);
	buffer_list_reset(&init_cfg);

	lua_getglobal(L, "_reload_1");
	 lua_call(L, 0, 1);
	  buffer_list_swap(&buffers, &init_cfg);
	  lua_getglobal(L, "_reload_2");
	  lua_rotate(L, -2, 1);
	lua_call(L, 1, 0);

	// this comes from the settings table so it has to be reloaded
	assert(lua_gettop(L) == CFG_BLACKLIST_IDX);
	lua_pop(L, 1);
	 lua_getglobal(L, "cfgfs");
	  lua_getfield(L, -1, "intercept_blacklist");
	  lua_rotate(L, -2, 1);
	 lua_pop(L, 1);

	lua_release_state(L);
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void *reloader_main(void *ud) {
	(void)ud;
	set_thread_name("reloader");

	int fd = inotify_init();
	check_minus1(
	    fd,
	    "reloader: inotify_init",
	    goto out);

	const char *filename = (getenv("CFGFS_SCRIPT") ?: "./script.lua");
	while (wait_for_event(filename, fd)) {
		do_reload();
	}
out:
	if (fd != -1) close(fd);
	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void reloader_init(void) {
	if (thread != 0) return;

	check_minus1(
	    pipe(msgpipe),
	    "reloader: pipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, reloader_main, NULL),
	    "reloader: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	thread = 0;
}

void reloader_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}
