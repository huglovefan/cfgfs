#include "logtail.h"
#include <limits.h>
#include <errno.h>
#include <unistd.h>

#include <sys/inotify.h>

#include "cli.h"
#include "vcr.h"

// -----------------------------------------------------------------------------

static pthread_t thread;
static char logpath[PATH_MAX];

static int _Atomic in_fd = -1;
static int _Atomic in_wd = -1;

static bool logtail_wait(void) {
	char buf[sizeof(struct inotify_event) + PATH_MAX + 1];
	ssize_t rv = read(in_fd, buf, sizeof(buf));
	return (rv > 0);
	// todo: could handle deletion of the file
}

static FILE *logfile;
static char linebuf[0xffff];
static char *p;

static void logtail_barf(void) {
	for (;;) {
		int c = fgetc(logfile);
		if (unlikely(c == EOF)) {
			clearerr(logfile); // forget eof
			break;
		}
		if (unlikely(c == '\n')) {
			*p = '\0';
			{
			double tm = vcr_get_timestamp();
			vcr_event {
				vcr_add_string("what", "game_console_output");
				vcr_add_string("text", linebuf);
				vcr_add_double("timestamp", tm);
			}
			}
			cli_println("%s", linebuf);
			p = linebuf;
			continue;
		}
		*p++ = (char)c;
	}
	// todo: could avoid overflowing the buffer
}

static void *logtail_main(void *ud) {
	(void)ud;
	set_thread_name("cf_logtail");
	p = linebuf;
	while (logtail_wait()) {
		logtail_barf();
	}
	pthread_exit(NULL);
	return NULL;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
void logtail_start(const char *gamedir) {
	if (gamedir == NULL) gamedir = ".";
	if (thread != 0) return;

	snprintf(logpath, sizeof(logpath), "%s/console.log", gamedir);

	// create if it doesn't exist
	logfile = fopen(logpath, "a");
	if (logfile != NULL) fclose(logfile);

	logfile = fopen(logpath, "r");
	if (logfile == NULL) {
		Dbg("fopen: %s", strerror(errno));
		goto err;
	}
	fseek(logfile, 0, SEEK_END);

	in_fd = inotify_init();
	if (in_fd == -1) {
		Dbg("inotify_init: %s", strerror(errno));
		goto err;
	}

	in_wd = inotify_add_watch(in_fd, logpath, IN_MODIFY);
	if (in_wd == -1) {
		Dbg("inotify_add_watch: %s", strerror(errno));
		goto err;
	}

	int err = pthread_create(&thread, NULL, logtail_main, NULL);
	if (err != 0) {
		Dbg("pthread_create: %s", strerror(err));
		thread = 0;
		goto err;
	}
	return;
err:
	if (logfile != NULL) {
		fclose(exchange(FILE *, logfile, NULL));
	}
	if (in_fd != -1) {
		if (in_wd != -1) {
			inotify_rm_watch(in_fd, in_wd);
			close(exchange(int, in_wd, -1));
		}
		close(exchange(int, in_fd, -1));
	}
}

__attribute__((cold))
void logtail_stop(void) {
	if (thread == 0) return;

	int fd = exchange(int, in_fd, -1);
	int wd = exchange(int, in_wd, -1);
	// is this safe??

	inotify_rm_watch(fd, wd);
	close(wd);

	pthread_join(thread, NULL);

	close(fd);

	fclose(logfile);
	logfile = NULL;

	thread = 0;
}
