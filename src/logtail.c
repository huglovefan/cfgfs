#define _GNU_SOURCE // unlocked_stdio
#include "logtail.h"

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

#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_exit = 1,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

static bool wait_for_event(int fd) {
	bool success = false;

	struct pollfd fds[2] = {
		{.fd = fd,         .events = POLLIN},
		{.fd = msgpipe[0], .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			perror("logtail: poll");
			break;
		}
		if (unlikely((fds[0].revents|fds[1].revents) & POLLNOTGOOD)) {
			break;
		}
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
			check_minus1(in_rv, "logtail: read", goto out);
			success = true;
			goto out;
		}
	}
out:
	return success;
}

// -----------------------------------------------------------------------------

static struct {
	char linebuf[2048];
	size_t len;
} state;

static void got_line(lua_State *L);

static void read_it(lua_State *L, FILE *logfile) {
	LUA_LOCK();
	int c;
	for (;;) {
		c = fgetc_unlocked(logfile);
newchar:
		if (unlikely(c == EOF || c == '\n')) break;
		if (unlikely(state.len == sizeof(state.linebuf))) {
			state.len--; // too long, will ignore this
		}
		state.linebuf[state.len++] = (char)c;
	}
	__asm__(""); // do not put the below stuff inside the loop
	if (unlikely(c == '\n')) {
		got_line(L);
		state.len = 0;
		c = fgetc_unlocked(logfile);
		if (unlikely(c != EOF)) goto newchar;
	}
	clearerr_unlocked(logfile);
	opportunistic_click();
	LUA_UNLOCK();
}
// ^ should this unlock it between lines?

static void got_line(lua_State *L) {
	if (unlikely(state.len == sizeof(state.linebuf))) {
D		eprintln("logtail: ignoring unreasonably long line");
		return;
	}
	if (unlikely(state.len == 0)) return;
	 lua_getglobal(L, "_game_console_output");
	  lua_pushlstring(L, state.linebuf, state.len);
	lua_call(L, 1, 0);
}

// -----------------------------------------------------------------------------

static void ugly_deinit(FILE *logfile, int fd);

__attribute__((cold))
static bool ugly_init(lua_State *L, FILE **logfile_out, int *fd_out) {
	FILE *logfile = NULL;
	int fd = -1, wd = -1;

	char logpath[PATH_MAX] = {0};
	LUA_LOCK();
	 lua_getglobal(L, "gamedir");
	 const char *gamedir = (lua_tostring(L, -1) ?: ".");
	 snprintf(logpath, sizeof(logpath), "%s/console.log", gamedir);
	lua_pop(L, 1);
	LUA_UNLOCK();

	// create the log file it doesn't exist
	logfile = fopen(logpath, "a");
	check_nonnull(logfile, "logtail: fopen", goto err);
	fclose(logfile);

	logfile = fopen(logpath, "r");
	check_nonnull(logfile, "logtail: fopen", goto err);

	fd = inotify_init();
	check_minus1(fd, "logtail: inotify_init", goto err);

	wd = inotify_add_watch(fd, logpath, IN_MODIFY);
	check_minus1(wd, "logtail: inotify_add_watch", goto err);

	fseek(logfile, 0, SEEK_END);

	*logfile_out = logfile;
	*fd_out = fd;

	return true;
err:
	ugly_deinit(logfile, fd);
	return false;
}

__attribute__((cold))
static void ugly_deinit(FILE *logfile, int fd) {
	if (logfile != NULL) fclose(logfile);
	if (fd != -1) close(fd); // closes wd too
}

// -----------------------------------------------------------------------------

static void *logtail_main(void *ud) {
	set_thread_name("logtail");
	lua_State *L = ud;

	FILE *logfile = NULL;
	int fd = -1;
	if (!ugly_init(L, &logfile, &fd)) return NULL;

	while (wait_for_event(fd)) {
		read_it(L, logfile);
	}
	ugly_deinit(logfile, fd);
	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

__attribute__((cold))
void logtail_init(void *L) {
	if (thread != 0) return;
	if (getenv("CFGFS_NO_LOGTAIL")) return;

	check_minus1(
	    pipe(msgpipe),
	    "logtail: pipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, logtail_main, L),
	    "logtail: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	thread = 0;
}

__attribute__((cold))
void logtail_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}
