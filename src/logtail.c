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
			eprintln("logtail: poll: %s", strerror(errno));
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
			check_minus1(in_rv, "logtail: read", goto out);
			success = true;
			goto out;
		}
	}
out:
	return success;
}

// -----------------------------------------------------------------------------

static void got_line(lua_State *L, const char *line) {
	if (unlikely(*line == '\0')) return;

	LUA_LOCK();
	 lua_getglobal(L, "_game_console_output");
	  lua_pushstring(L, line);
	lua_call(L, 1, 0);
	opportunistic_click();
	LUA_UNLOCK();
}

static char *linebuf;
static size_t linebufsz;

static void barf(lua_State *L, FILE *logfile) {
	static char *p;
	for (;;) {
		int c = fgetc(logfile);
		if (unlikely(c == EOF)) {
			clearerr(logfile); // forget eof
			break;
		}
		if (unlikely(p == NULL || (size_t)(p-linebuf) >= linebufsz)) {
			size_t used = (p != NULL) ? (size_t)(p-linebuf) : 0;
			linebufsz = (linebufsz) ? linebufsz*2 : 512;
			linebuf = realloc(linebuf, linebufsz);
			p = linebuf+used;
		}
		if (unlikely(c == '\n')) {
			*p = '\0';
			got_line(L, linebuf);
			p = linebuf;
			continue;
		}
		*p++ = (char)c;
	}
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

	// create log file it doesn't exist
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
		barf(L, logfile);
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

	free(exchange(linebuf, NULL));
	linebufsz = 0;

	thread = 0;
}
