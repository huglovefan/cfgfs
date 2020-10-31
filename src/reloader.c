#include "reloader.h"
#include "lua.h"
#include "buffers.h"
#include "macros.h"

#include <sys/poll.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include "cli_output.h"

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
	if (wd == -1) {
		eprintln("reloader: inotify_add_watch: %s", strerror(errno));
		goto out;
	}

	struct pollfd fds[2] = {
		{.fd = fd,         .events = POLLIN},
		{.fd = msgpipe[0], .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			eprintln("reloader: poll: %s", strerror(errno));
			break;
		}
		if (unlikely(fds[0].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_exit:
				goto out;
			default:
				assert(false);
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
			ssize_t in_rv = read(fd, buf, sizeof(buf));
			if (in_rv == -1) {
				eprintln("reloader: read: %s", strerror(errno));
				goto out;
			}
			success = true;
			goto out;
		}
	}
out:
	if (wd != -1) inotify_rm_watch(fd, wd);
	return success;
}

static void do_reload(lua_State *L) {
	LUA_LOCK();

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
D	assert(lua_gettop(L) == CFG_BLACKLIST_IDX);

	LUA_UNLOCK();
}

// -----------------------------------------------------------------------------

static void *reloader_main(void *ud) {
	set_thread_name("reloader");
	lua_State *L = ud;

	int fd = inotify_init();
	if (fd == -1) {
		eprintln("reloader: inotify_init: %s", strerror(errno));
		goto out;
	}

	const char *filename = (getenv("CFGFS_SCRIPT") ?: "./script.lua");
	while (wait_for_event(filename, fd)) {
		do_reload(L);
	}
out:
	if (fd != -1) close(fd);
	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void reloader_init(void *L) {
	if (thread != 0) return;
	if (pipe(msgpipe) == -1) {
		eprintln("reloader: pipe: %s", strerror(errno));
		goto err;
	}
	int err = pthread_create(&thread, NULL, reloader_main, (void *)L);
	if (err != 0) {
		thread = 0;
		eprintln("reloader: pthread_create: %s", strerror(err));
		goto err;
	}
	return;
err:
	if (msgpipe[0] != -1) close(exchange(int, msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(int, msgpipe[1], -1));
}

void reloader_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(exchange(pthread_t, thread, 0), NULL);

	close(exchange(int, msgpipe[0], -1));
	close(exchange(int, msgpipe[1], -1));
}
