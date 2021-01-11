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

#include <lua.h>

#include "buffers.h"
#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"
#include "pipe_io.h"

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
};

// -----------------------------------------------------------------------------

static bool wait_for_event(const char *path, int fd) {
	bool success = false;

	int wd = inotify_add_watch(fd, path, IN_MODIFY);
	check_minus1(wd, "reloader: inotify_add_watch", goto out);

	switch (rdselect(msgpipe[0], fd)) {
	case 2: {
		char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
		ssize_t rv = read(fd, buf, sizeof(buf));
		check_minus1(rv, "reloader: read", break);
		success = true;
		break;
	}
	case 1:
		switch (readch(msgpipe[0])) {
		case msg_exit:
			goto out;
		}
		break;
	case 0:
		perror("reloader: rdselect");
		break;
	default:
		break;
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

	const char *filename = (getenv("CFGFS_SCRIPT") ?: "script.lua");
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

	writech(msgpipe[1], msg_exit);

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}
