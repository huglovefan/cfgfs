#include "reloader.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>

#if defined(__linux__)
 #include <sys/prctl.h>
#endif

#include <lauxlib.h>

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

// init/deinit and file adding

// note: these assume they're only called
// 1. from main thread before it has called reloader_init()
// 2. from the reloader thread
// lua must check that it's safe before calling any of these
// (i assume it's not safe to add watches to the inotify fd while we're polling it)

static inline bool safe_to_call_these_from_lua(void *L) {
	const char *locker = lua_get_locker(L);
	if (likely(locker != NULL)) {
		return (0 == strcmp(locker, "reloader"));
	} else {
		// main thread
		// (it doesn't set the locker string)
		return true;
	}
}

static int inotify_fd = -1;

static int get_or_init_inotify_fd(void);
static void deinit_inotify_if_inited(void);
static bool watch_file(const char *path);

static int get_or_init_inotify_fd(void) {
	int fd = inotify_fd;
	if (fd == -1) {
		fd = inotify_init();
		check_minus1(fd, "reloader: inotify_init", goto fail);

		inotify_fd = fd;
		watch_file((getenv("CFGFS_SCRIPT") ?: "script.lua"));
	}
	return fd;
fail:
	if (fd != -1) {
		close(fd);
		inotify_fd = -1;
	}
	return -1;
}

static void deinit_inotify_if_inited(void) {
	int fd = inotify_fd;
	if (unlikely(fd == -1)) return;
	close(fd);
	inotify_fd = -1;
}

static bool watch_file(const char *path) {
	int fd = get_or_init_inotify_fd();
	if (unlikely(fd == -1)) return false;
	if (unlikely(-1 == inotify_add_watch(fd, path, IN_MODIFY))) {
		eprintln("reloader: failed to watch path %s: %s",
		    path, strerror(errno));
		return false;
	}
V	eprintln("reloader: successfully added path %s", path);
	return true;
}

// -----------------------------------------------------------------------------

static char readbuf[sizeof(struct inotify_event) + PATH_MAX + 1];

static bool wait_for_event(void) {
	one_true_entry();
	bool success = false;

	int fd = get_or_init_inotify_fd();
	if (unlikely(fd == -1)) goto out;

	switch (rdselect(msgpipe[0], fd)) {
	case 2: {
V		eprintln("reloader: inotify fd became readable");
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
	case -2:
		// might be good to print something if this ever happens
		eprintln("reloader: inotify fd had an error!");
		assert_unreachable();
		break;
	}
out:
	deinit_inotify_if_inited();
	one_true_exit();
	return success;
}

static void do_reload(void) {
	lua_State *L = lua_get_state("reloader");
	if (L == NULL) {
		perror("reloader: failed to lock lua state");
		return;
	}

V	eprintln("reloader: reloading...");

	// reinit early since we know we're going to poll it later
	get_or_init_inotify_fd();

	buffer_list_reset(&buffers);
	buffer_list_reset(&init_cfg);

	lua_getglobal(L, "_reload_1");
	 lua_call(L, 0, 1);
	  buffer_list_swap(&buffers, &init_cfg);
	  lua_getglobal(L, "_reload_2");
	  lua_rotate(L, -2, 1);
	lua_call(L, 1, 0);

V	eprintln("reloader: reloading done!");

	lua_release_state(L);
}

// -----------------------------------------------------------------------------

// exported to lua as _reloader_add_watch()
int l_reloader_add_watch(void *L) {
	bool ok = false;

	// can't safely add the watch -> just return false
	if (unlikely(!safe_to_call_these_from_lua(L))) {
V		eprintln("l_reloader_watch_file: lua not locked by us, ignoring add of %s",
		    lua_tostring(L, 1));
		goto out;
	}

	const char *path = luaL_checkstring(L, 1);

	if (likely(watch_file(path))) {
		ok = true;
	}
out:
	lua_pushboolean(L, ok);
	return 1;
}

// -----------------------------------------------------------------------------

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
