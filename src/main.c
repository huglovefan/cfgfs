#include "main.h"

#include <errno.h>
#include <float.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#pragma GCC diagnostic push
 #if defined(__clang__)
  #pragma GCC diagnostic ignored "-Wdocumentation"
 #endif
 #pragma GCC diagnostic ignored "-Wpadded"
  #include <fuse.h>
  #include <fuse_lowlevel.h>
#pragma GCC diagnostic pop

#include <lauxlib.h>

#include "attention.h"
#include "buffer_list.h"
#include "buffers.h"
#include "cli_input.h"
#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"
#include "reloader.h"

// -----------------------------------------------------------------------------

static pthread_t g_main_thread;

// true if we're exiting because main_quit() was called
static bool main_quit_called;

__attribute__((cold))
void main_quit(void) {
	pthread_t main_thread = g_main_thread;
	if (main_thread) {
		main_quit_called = true;
		pthread_kill(main_thread, SIGINT);
	}
}

// -----------------------------------------------------------------------------

enum special_file_type {
	sft_none = 0,
	sft_console_log = 1,
	sft_control = 2,
};

static int l_lookup_path(const char *restrict path);

#define starts_with(self, other) \
	(0 == memcmp(self, other, strlen(other)))

#define ends_with(self, other) \
	(0 == memcmp(self+length-strlen(other), other, strlen(other)))

#define equals(self, other) \
	(length == strlen(other) && \
	 0 == memcmp(self, other, length))

__attribute__((always_inline))
static int lookup_path(const char *restrict path, enum special_file_type *type) {
	const char *slashdot = path;
D	assert(*slashdot == '/');
	const char *p = path;
	for (; *p != '\0'; p++) {
		switch (*p) {
		case '/':
		case '.':
			slashdot = p;
			break;
		}
	}
	size_t length = (size_t)(p-path);

	if (unlikely(*slashdot == '/')) {
		return 0xd;
	}
//	if (unlikely(length < strlen("/.cfg"))) {
//		return -ENOENT;
//	}
	if (unlikely(!ends_with(path, ".cfg"))) {
		if (unlikely(equals(path, "/console.log"))) {
			if (type) *type = sft_console_log;
			return 0xf;
		}
		if (unlikely(equals(path, "/.control"))) {
			if (type) *type = sft_control;
			return 0xf;
		}
		return -ENOENT;
	}

	if (likely(starts_with(path, "/cfgfs/"))) {
		return 0xf;
	}

	return l_lookup_path(path);
}

#undef starts_with
#undef ends_with
#undef equals

__attribute__((noinline))
static int l_lookup_path(const char *restrict path) {
	lua_State *L = lua_get_state();
	if (unlikely(L == NULL)) return -errno;
	 lua_getglobal(L, "_lookup_path");
	  lua_pushstring(L, path+1); // skip "/"
	 lua_call(L, 1, 1);
	 int rv = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
D	assert(rv == 0 || rv == 0xd || rv == 0xf);
	if (rv == 0) rv = -ENOENT;
	lua_release_state_no_click(L);
	return rv;
}

// -----------------------------------------------------------------------------

__attribute__((hot))
static int cfgfs_getattr(const char *restrict path,
                         struct stat *restrict stbuf,
                         struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_getattr: %s", path);

	int rv = lookup_path(path, NULL);

	if (rv == 0xf) {
		stbuf->st_mode = 0444|S_IFREG;
		stbuf->st_size = reported_cfg_size;
		return 0;
	} else if (rv == 0xd) {
		stbuf->st_mode = 0555|S_IFDIR;
		return 0;
	} else {
D		assert(rv < 0);
		return rv;
	}
}

// ~

__attribute__((hot))
static int cfgfs_open(const char *restrict path,
                      struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_open: %s", path);

	int rv = lookup_path(path, (enum special_file_type *)&fi->fh);

	if (rv == 0xf) {
		return 0;
	} else if (rv == 0xd) {
		return -EISDIR;
	} else {
D		assert(rv < 0);
		return rv;
	}
}

// ~

#define starts_with(this, that) (0 == strncmp(this, that, strlen(that)))

__attribute__((hot))
static int cfgfs_read(const char *restrict path,
                      char *restrict buf,
                      size_t size,
                      off_t offset,
                      struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_read: %s (size=%lu, offset=%lu)", path, size, offset);
	int rv = 0;

	if (unlikely(offset != 0)) return 0;

	if (unlikely(size < reported_cfg_size)) {
		eprintln("\acfgfs_read: read size too small! expected at least %zu, got only %zu",
		    reported_cfg_size, size);
D		abort();
		return -EOVERFLOW;
	}

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = starts_with(path, "/cfgfs/unmask_next/");

	lua_State *L = lua_get_state();
	if (unlikely(L == NULL)) {
		return -errno;
	}

	struct buffer fakebuf;
	if (likely(!silent)) {
		buffer_list_maybe_unshift_fake_buf(&buffers, &fakebuf, buf);
	}

	 lua_pushvalue(L, GET_CONTENTS_IDX);
	  lua_pushstring(L, path);
	lua_call(L, 1, 0);

	if (likely(!silent)) {
		struct buffer *ent = buffer_list_grab_first(&buffers);
D		assert(ent != NULL); // nothing should've removed it
		size_t outsize = buffer_get_size(ent);
		rv = (int)outsize;
		if (unlikely(ent != &fakebuf)) {
			buffer_memcpy_to(ent, buf, outsize);
			buffer_free(ent);
		}
	}
	lua_release_state_no_click(L);
	VV {
		if (rv >= 0) {
			eprintln("data=[[%.*s]] rv=%d", rv, buf, rv);
		} else {
			eprintln("data=(null) rv=%d", rv);
		}
	}
	return rv;
}

#undef starts_with

// ~

static int cfgfs_write_control(const char *data, size_t size);

#define log_catcher_got_line(p, sz) \
	({ \
		if (likely(!lua_locked)) { \
			if (unlikely(!lua_lock_state())) { \
				rv = -errno; \
				goto out; \
			} \
			lua_locked = true; \
		} \
		lua_State *L = lua_get_state_already_locked(); \
		 lua_pushvalue(L, GAME_CONSOLE_OUTPUT_IDX); \
		  lua_pushlstring(L, (p), (sz)); \
		lua_call(L, 1, 0); \
	})

__attribute__((hot))
static int cfgfs_write(const char *restrict path,
                       const char *restrict data,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_write: %s (size=%lu, offset=%lu)", path, size, offset);

	enum special_file_type sft;
	memcpy(&sft, &fi->fh, sizeof(enum special_file_type));
	switch (sft) {
	case sft_console_log: {
		static pthread_mutex_t logcatcher_lock = PTHREAD_MUTEX_INITIALIZER;
		static char logbuffer[8192];
		static char *p = logbuffer;
		bool lua_locked = false;
		size_t insize = size;
		int rv = (int)insize;

		// tf2 only writes one thing at a time so this doesn't really need a lock
D		assert(0 == pthread_mutex_trylock(&logcatcher_lock));

		one_true_entry();
		if (likely(size >= 2 && data[size-1] == '\n')) {
			// writing a full line in one go
			// may contain embedded newlines
			log_catcher_got_line(data, size-1);
		} else do {
			// idiot writing a line in multiple parts

			// size check
			size_t buffer_used = (size_t)(p-logbuffer);
			size_t buffer_left = sizeof(logbuffer)-buffer_used;
			if (unlikely(size > buffer_left)) {
				rv = -E2BIG;
				goto out;
			}

			char *nl = memchr(data, '\n', size);
			if (likely(nl == NULL)) {
				// incoming data doesn't complete a line. add it to the buffer and get out
				memcpy(p, data, size);
				p += size;
				size = 0;
			} else {
				// incoming data completes a line
				size_t partlen = (size_t)(nl-data);
				memcpy(p, data, partlen);
				log_catcher_got_line(logbuffer, (size_t)(p-logbuffer)+partlen);
				p = logbuffer;
				data += partlen+1;
				size -= partlen+1;
			}
		} while (unlikely(size != 0));
out:
		one_true_exit();
		if (lua_locked) lua_unlock_state();
D		pthread_mutex_unlock(&logcatcher_lock);
		return rv;
	}
	case sft_control:
		return cfgfs_write_control(data, size);
	case sft_none:
		return -ENOTSUP;
	}
	unreachable_weak();
}

#undef log_catcher_got_line

// ~

static void control_do_line(const char *line, size_t size);

__attribute__((cold))
__attribute__((noinline))
static int cfgfs_write_control(const char *data,
                               size_t size) {
	if (size == 8192) return -EMSGSIZE; // might be truncated
	if (!lua_lock_state()) return -errno;

	const char *p = data;
	for (;;) {
		const char *c = strchr(p, '\n');
		if (c == NULL) break;
		control_do_line(p, (size_t)(c-p));
		p = c+1;
	}

	lua_unlock_state();
	return (int)size;
}

static void control_do_line(const char *line, size_t size) {
V	eprintln("control_do_line: line=[%.*s]", (int)size, line);
	lua_State *L = lua_get_state_already_locked();
	 lua_getglobal(L, "_control");
	  lua_pushlstring(L, line, size);
	lua_call(L, 1, 0);
}

// ~

__attribute__((cold))
static void *cfgfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	// https://github.com/libfuse/libfuse/blob/0105e06/include/fuse_common.h#L421
	(void)conn;

	// https://github.com/libfuse/libfuse/blob/f54eb86/include/fuse.h#L93
	cfg->set_gid = true;
	cfg->gid = getegid();
	cfg->set_uid = true;
	cfg->uid = geteuid();

	cfg->entry_timeout = DBL_MAX;
	cfg->negative_timeout = 0;
	cfg->attr_timeout = DBL_MAX;

	cfg->direct_io = true;

	return NULL;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void cfgfs_log(enum fuse_log_level level, const char *fmt, va_list args) {
	(void)level;
	cli_lock_output();
	vfprintf(stderr, fmt, args);
	cli_unlock_output();
}

// -----------------------------------------------------------------------------

static const struct fuse_operations cfgfs_oper = {
	.getattr = cfgfs_getattr,
	.open = cfgfs_open,
	.read = cfgfs_read,
	.write = cfgfs_write,
	.init = cfgfs_init,
};

// https://github.com/libfuse/libfuse/blob/f54eb86/include/fuse.h#L852
enum fuse_main_rv {
	rv_ok                 = 0,
	rv_invalid_argument   = 1,
	rv_missing_mountpoint = 2,
	rv_fuse_setup_failed  = 3,
	rv_mount_failed       = 4,
	rv_daemonize_failed   = 5,
	rv_signal_failed      = 6,
	rv_fs_error           = 7,
	// cfgfs-specific
	rv_cfgfs_lua_failed    = 10,
	rv_cfgfs_reexec_failed = 11,
};

static void check_env(void) {
	static const struct var {
		const char *name, *what;
	} vars[] = {
		{"CFGFS_SCRIPT", "path to the script to load"},
		{"GAMEDIR", "path to mod directory containing gameinfo.txt"},
		{"GAMEROOT", "path to parent directory of GAMEDIR"},
		{"GAMENAME", "game title from gameinfo.txt"},
		{NULL, NULL},
	};
	bool warned = false;

	for (const struct var *p = vars; p->name != NULL; p++) {
		const char *v = getenv(p->name);
		if (v != NULL && *v != '\0') {
			continue;
		}
		if (!warned) {
			eprintln("warning: the following environment variables are unset:");
			warned = true;
		}
		if (p->what != NULL) {
			eprintln("- %s (%s)", p->name, p->what);
		} else {
			eprintln("- %s", p->name);
		}
	}
	if (warned) {
		eprintln("features that depend on them will be unavailable");
	}
}

int main(int argc, char **argv) {

	one_true_entry();

	// https://lwn.net/Articles/837019/
	mallopt(M_MMAP_MAX, 0);
	mallopt(M_TOP_PAD, 10*1024*1000);
	mallopt(M_TRIM_THRESHOLD, 20*1024*1000);
	mlockall(MCL_CURRENT|MCL_FUTURE);

#if defined(SANITIZER)
	eprintln("NOTE: cfgfs was built with %s", SANITIZER);
#endif

#if defined(PGO) && PGO == 1
	eprintln("NOTE: this is a PGO profiling build, rebuild with PGO=2 when finished");
#endif

D	eprintln("NOTE: debug checks are enabled");
V	eprintln("NOTE: verbose messages are enabled");
VV	eprintln("NOTE: very verbose messages are enabled");

	check_env();

	enum fuse_main_rv rv;

	// ~ init fuse ~

	fuse_set_log_func(cfgfs_log);

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_cmdline_opts opts;
	if (0 != fuse_parse_cmdline(&args, &opts)) {
		rv = rv_invalid_argument;
		goto out_no_fuse;
	}
	if (opts.show_version) {
		fuse_lowlevel_version();
		rv = rv_ok;
		goto out_no_fuse;
	}
	if (opts.show_help) {
		if (args.argv[0][0] != '\0') {
			println("usage: %s [options] <mountpoint>", args.argv[0]);
		}
		println("FUSE options:");
		fuse_cmdline_help();
		fuse_lib_help(&args);
		rv = rv_ok;
		goto out_no_fuse;
	}
	if (!opts.mountpoint) {
		eprintln("error: no mountpoint specified");
		rv = rv_missing_mountpoint;
		goto out_no_fuse;
	}

	opts.foreground = true;

	struct fuse *fuse = fuse_new(&args, &cfgfs_oper, sizeof(cfgfs_oper), NULL);
	if (fuse == NULL) {
		rv = rv_fuse_setup_failed;
		goto out_no_fuse;
	}
	if (0 != fuse_mount(fuse, opts.mountpoint)) {
		rv = rv_mount_failed;
		goto out_fuse_newed;
	}
	struct fuse_session *se = fuse_get_session(fuse);
	if (0 != fuse_set_signal_handlers(se)) {
		rv = rv_signal_failed;
		goto out_fuse_newed_and_mounted;
	}
	g_main_thread = pthread_self();

	// ~ init other things ~

	click_init();
	if (!lua_init()) {
		rv = rv_cfgfs_lua_failed;
		goto out_fuse_newed_and_mounted_and_signals_handled;
	}
	attention_init();
	cli_input_init();
	reloader_init();

	// ~ boot up fuse ~

	int loop_rv = fuse_loop_mt(fuse, &(struct fuse_loop_config){
		.clone_fd = false,
		.max_idle_threads = 5,
	});
	rv = rv_ok;
	if (loop_rv != 0 && !main_quit_called) {
		rv = rv_fs_error;
	}

out_fuse_newed_and_mounted_and_signals_handled:
	g_main_thread = 0;
	fuse_remove_signal_handlers(se);
out_fuse_newed_and_mounted:
	fuse_unmount(fuse);
out_fuse_newed:
	fuse_destroy(fuse);
out_no_fuse:
	one_true_exit();
	lua_deinit();
	attention_deinit();
	cli_input_deinit();
	click_deinit();
	reloader_deinit();
	free(opts.mountpoint);
	fuse_opt_free_args(&args);
	if (0 == unlink(".cfgfs_reexec")) {
		setenv("CFGFS_RESTARTED", "1", 1);
		execvp(argv[0], argv);
		perror("cfgfs: exec");
		rv = rv_cfgfs_reexec_failed;
	}
	return (int)rv;

}
