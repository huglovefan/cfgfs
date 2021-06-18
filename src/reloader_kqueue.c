#include "reloader.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/event.h>
#include <sys/stat.h>

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "pipe_io.h"

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

static void do_reload(void);

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
		memset(&ev, 0, sizeof(struct kevent));
		int evcnt = kevent(kq, NULL, 0, &ev, 1, NULL);
		check_minus1(evcnt, "reloader: kevent", goto out);

#if defined(__linux__)
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
				do_reload();
				goto poll;
			}
			assert_unreachable();
		}

		// give them some time to actually write out the file
		usleep(10*1000);

		pthread_mutex_lock(&lock);

		bool should_reload = false;
		/*if (evcnt != 0) {*/
			// find the file the event is for, then check if it's modified
			bool found = false;
			bool unmodified = false;
			for (unsigned int i = 0; i < watches_cnt; i++) {
				if (ev.ident != (uintptr_t)watches[i].fd) {
					continue;
				}
				found = true;

				struct stat sb;
				if (-1 != stat(watches[i].path, &sb)) {
					sb.st_atime = 0;
					if (0 == memcmp(&sb, &watches[i].sb, sizeof(struct stat))) {
						unmodified = true;
					}
				}
				break;
			}
			assert(found);
			should_reload = (found) ? !unmodified : true;
		/*} else {
			// event has no target
			// check all files and find out if any of them are modified
			bool modified = false;
			for (unsigned int i = 0; i < watches_cnt; i++) {
				struct stat sb;
				if (0 == stat(watches[i].path, &sb)) {
					sb.st_atime = 0;
					if (0 == memcmp(&sb, &watches[i].sb, sizeof(struct stat))) {
						continue;
					}
				}
				modified = true;
				break;
			}
			should_reload = (modified);
		}*/

		if (should_reload) remove_watches();
		pthread_mutex_unlock(&lock);
		if (should_reload) do_reload();
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


static void do_reload(void) {
	lua_State *L = lua_get_state("reloader");
	if (L == NULL) {
		perror("reloader: failed to lock lua state!");
		return;
	}

V	eprintln("reloader: reloading...");

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

void reloader_reload(void) {
	if (msgpipe[1] != -1) writech(msgpipe[1], msg_reload);
}

int l_reloader_add_watch(void *L) {
	bool locked = false;
	const char *path = luaL_checkstring(L, 1);
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
	};
	check_minus1(
	    kevent(kq, &ev, 1, NULL, 0, NULL),
	    "reloader_add_watch: kevent",
	    goto err);

	watches_cnt++;

	locked = false;
	pthread_mutex_unlock(&lock);

	lua_pushboolean(L, true);
	return 1;
err:
	if (locked) pthread_mutex_unlock(&lock);
	if (fd != -1) close(fd);
	if (path2 != NULL) free(path2);
	lua_pushboolean(L, false);
	return 1;
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

#if !defined(__FreeBSD__)
	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	int err = pthread_timedjoin_np(thread, NULL, &ts);
	if (err != 0) {
		return;
	}
#else
	pthread_join(thread, NULL);
#endif

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}
