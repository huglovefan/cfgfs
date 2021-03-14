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

// this was previously reusing the same inotify fd (like the man page says) but
//  doing it that way would cause an infinite loop if the file was appended to
//  or edited using nano
// fixed by making it call inotify_init() again each time

static char readbuf[sizeof(struct inotify_event) + PATH_MAX + 1];

static bool wait_for_event(void) {
	one_true_entry();
	bool success = false;

	int fd = inotify_init();
	check_minus1(fd, "reloader: inotify_init", goto out);

	const char *path = (getenv("CFGFS_SCRIPT") ?: "script.lua");

	int wd = inotify_add_watch(fd, path, IN_MODIFY);
	check_minus1(wd, "reloader: inotify_add_watch", goto out);

	switch (rdselect(msgpipe[0], fd)) {
	case 2: {
		ssize_t rv = read(fd, readbuf, sizeof(readbuf));
		check_minus1(rv, "reloader: read", break);
		success = true;
		break;
	}
	case 1:
		switch (readch(msgpipe[0])) {
		case msg_exit:
			goto out;
		}
		assert_unreachable();
	case 0:
		perror("reloader: rdselect");
		break;
	default:
		break;
	}
out:
	if (fd != -1) close(fd); // closes wd too
	one_true_exit();
	return success;
}

static void do_reload(void) {
	lua_State *L = lua_get_state("reloader");
	if (L == NULL) {
		perror("reloader: failed to lock lua state");
		return;
	}

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

	while (wait_for_event()) {
		do_reload();
	}

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

	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	int err = pthread_timedjoin_np(thread, NULL, &ts);
	if (err != 0) {
		return;
	}

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}
