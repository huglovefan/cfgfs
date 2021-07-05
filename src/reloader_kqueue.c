#include "reloader.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#if defined(__FreeBSD__)
 #include <pthread_np.h>
#endif

#include <sys/event.h>
#include <sys/stat.h>

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "pipe_io.h"

#if !defined(__FreeBSD__)
 #define USING_LIBKQUEUE
#endif

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
	msg_reload = 2,
};

// -----------------------------------------------------------------------------

static int kq = -1;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct watch {
	int fd;
	char *path;
	struct stat sb;
} *watches;
static unsigned int watches_cnt;

static void remove_watches(void) {
	for (unsigned int i = 0; i < watches_cnt; i++) {
		struct kevent ev = {
			.ident = (uintptr_t)watches[i].fd,
			.filter = EVFILT_VNODE,
			.flags = EV_DELETE,
		};
		if (-1 == kevent(kq, &ev, 1, NULL, 0, NULL)) {
			if (errno != ENOENT) {
				perror("reloader: kevent");
			}
		}
		close(watches[i].fd);
		free(watches[i].path);
	}
	watches_cnt = 0;
}

static bool file_modified(const char *path, const struct stat *oldstat);

static void *reloader_main(void *ud) {
	(void)ud;
	set_thread_name("reloader");

	pthread_mutex_lock(&lock);
	if (kq == -1) {
		kq = kqueue();
		check_minus1(kq, "reloader: kqueue", abort());
	}
	pthread_mutex_unlock(&lock);

	struct kevent ev = {
		.ident = (uintptr_t)msgpipe[0],
		.filter = EVFILT_READ,
		.flags = EV_ADD,
	};
	check_minus1(
	    kevent(kq, &ev, 1, NULL, 0, NULL),
	    "reloader: kevent",
	    goto out);

	for (;;) {
poll:
		ev = (struct kevent){0};
		int evcnt = kevent(kq, NULL, 0, &ev, 1, NULL);
		check_minus1(evcnt, "reloader: kevent", goto out);

#if defined(USING_LIBKQUEUE)
		if (evcnt == 0) goto poll; // ???
#endif

		if ((int)ev.ident == msgpipe[0]) {
			switch (readch(msgpipe[0])) {
			case msg_exit:
				goto out;
			case msg_reload:
				pthread_mutex_lock(&lock);
				 remove_watches();
				pthread_mutex_unlock(&lock);
				reloader_do_reload_internal(NULL);
				goto poll;
			}
			assert_unreachable();
		}

		pthread_mutex_lock(&lock);

		unsigned int idx = (unsigned int)(uintptr_t)ev.udata;
		if (!(idx < watches_cnt)) {
			eprintln("reloader: idx %u >= watches_cnt %u",
			    idx, watches_cnt);
D			assert(false);
			pthread_mutex_unlock(&lock);
			continue;
		}

		if (file_modified(watches[idx].path, &watches[idx].sb)) {
			char *path = strdup(watches[idx].path);
			remove_watches();
			pthread_mutex_unlock(&lock);
			reloader_do_reload_internal(path);
			free(path);
		} else {
			eprintln("reloader: woken up for unmodified path %s",
			    watches[idx].path);
			pthread_mutex_unlock(&lock);
		}
	}
out:
	pthread_mutex_lock(&lock);
	if (kq != -1) {
		remove_watches();
		free(exchange(watches, NULL));
		watches_cnt = 0;
		close(exchange(kq, -1));
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

static bool file_modified(const char *path, const struct stat *oldstat) {
D	assert(oldstat->st_atime == 0); // should've cleared this for the comparison to work
	int tries = 4;
	while (tries --> 0) {
		struct stat sb;
		if (-1 != stat(path, &sb)) {
			sb.st_atime = 0;
			if (0 != memcmp(&sb, oldstat, sizeof(struct stat))) {
				return true;
			}
		}
		// give them time to actually write out the file
		usleep(5*1000);
	}
	return false;
}

// -----------------------------------------------------------------------------

void reloader_reload(void) {
	if (msgpipe[1] != -1) writech(msgpipe[1], msg_reload);
}

_Bool reloader_add_watch(const char *path, const char **whynot) {
	bool locked = false;
	int fd;
	char *path2 = NULL;
	struct watch *newwatches;

	fd = open(path, O_RDONLY|O_CLOEXEC);
	check_minus1(fd, "reloader_add_watch: open", goto err);

	path2 = strdup(path);
	check_nonnull(path2, "reloader_add_watch: strdup", goto err);

	pthread_mutex_lock(&lock);
	locked = true;

	newwatches = reallocarray(watches, watches_cnt+1, sizeof(struct watch));
	check_nonnull(newwatches, "reloader_add_watch: reallocarray", goto err);
	newwatches[watches_cnt].path = path2;
	newwatches[watches_cnt].fd = fd;
	check_minus1(
	    fstat(fd, &newwatches[watches_cnt].sb),
	    "reloader_add_watch: stat",
	    goto err);
	newwatches[watches_cnt].sb.st_atime = 0;
	watches = newwatches;

	if (kq == -1) {
		kq = kqueue();
		check_minus1(kq, "reloader_add_watch: kqueue", goto err);
	}

	struct kevent ev = {
		.ident = (uintptr_t)fd,
		.filter = EVFILT_VNODE,
		.flags = EV_ADD,
		// attrib: edit using geany, touch(1)
		// write: edit using nano
		.fflags = NOTE_ATTRIB|NOTE_WRITE,
		.udata = (void *)(uintptr_t)watches_cnt,
	};
	check_minus1(
	    kevent(kq, &ev, 1, NULL, 0, NULL),
	    "reloader_add_watch: kevent",
	    goto err);

	watches_cnt++;

	locked = false;
	pthread_mutex_unlock(&lock);

	return true;
err:
	if (locked) pthread_mutex_unlock(&lock);
	if (fd != -1) close(fd);
	if (path2 != NULL) free(path2);
	if (whynot != NULL) *whynot = "failed to add watch";
	return false;
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
