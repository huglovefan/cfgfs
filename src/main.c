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
 #pragma GCC diagnostic ignored "-Wdocumentation"
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
static bool main_quit_called;

__attribute__((cold))
void main_quit(void) {
	pthread_t main_thread = g_main_thread;
	if (main_thread) {
		pthread_kill(main_thread, SIGINT);
		main_quit_called = true;
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
	const char *slashdot;
	slashdot = *&slashdot; // not uninitialized
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
	int rv = 0xf;

	lua_State *L = lua_get_state();

	 lua_pushvalue(L, UNMASK_NEXT_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  lua_Integer cnt = lua_tointeger(L, -1);
	  if (unlikely(cnt != 0)) {
		   cnt -= 1;
		   lua_pushstring(L, path+1); // skip "/"
		    lua_pushinteger(L, cnt);
		  lua_rawset(L, -4);
		lua_pop(L, 2);
VV		eprintln("unmask_next[%s]: %lld -> %lld", path+1, cnt+1, cnt);
		rv = -ENOENT;
		goto out_locked;
	  }
	//lua_pop(L, 2); // combined below

	 lua_pushvalue(L, CFG_BLACKLIST_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  bool blacklisted = lua_toboolean(L, -1);
	lua_pop(L, 2+2);
	if (unlikely(blacklisted)) {
		rv = -ENOENT;
		goto out_locked;
	}
out_locked:
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
	assume(size >= reported_cfg_size);

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = starts_with(path, "/cfgfs/unmask_next/");

	lua_State *L = lua_get_state();

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

static int cfgfs_write_control(const char *data, size_t size, off_t offset);

#define log_catcher_got_line(p, sz) \
	({ \
		if (!lua_locked++) lua_lock_state(); \
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

	switch (*(enum special_file_type *)&fi->fh) {
	case sft_console_log: {
		static pthread_mutex_t logcatcher_lock = PTHREAD_MUTEX_INITIALIZER;
		static char logbuffer[8192];
		static char *p = logbuffer;
		bool lua_locked = false;
		size_t insize = size;

		// """in theory""" this shouldn't need a lock since tf2 will only write one thing at a time
D		assert(0 == pthread_mutex_trylock(&logcatcher_lock));
		if (likely(size >= 2 && data[size-1] == '\n')) {
			// writing a full line in one go
			// may contain embedded newlines
			log_catcher_got_line(data, size-1);
		} else do {
			// idiot writing a line in multiple parts
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
		if (lua_locked) lua_unlock_state();
D		pthread_mutex_unlock(&logcatcher_lock);
		return (int)insize;
	}
	case sft_control:
		return cfgfs_write_control(data, size, offset);
	case sft_none:
		return -ENOTSUP;
	}
}

#undef log_catcher_got_line

// ~

static void control_do_line(const char *line, size_t size);

__attribute__((cold))
__attribute__((noinline))
static int cfgfs_write_control(const char *data,
                               size_t size,
                               off_t offset) {
	if (size == 8192) return -EMSGSIZE; // might be truncated
	if (offset != 0) return -EINVAL;
	lua_lock_state();

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
V	eprintln("control_do_line: line=[%s]", line);
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
void fuse_log(enum fuse_log_level level, const char *fmt, ...) {
	(void)level;
	cli_lock_output();
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
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

int main(int argc, char **argv) {

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

	// ~ init fuse ~

	enum fuse_main_rv res;

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_cmdline_opts opts;
	if (0 != fuse_parse_cmdline(&args, &opts)) {
		return rv_invalid_argument;
	}
	if (opts.show_version) {
		fuse_lowlevel_version();
		res = rv_ok;
		goto out_no_fuse;
	}
	if (opts.show_help) {
		if (args.argv[0][0] != '\0') {
			println("usage: %s [options] <mountpoint>", args.argv[0]);
		}
		println("FUSE options:");
		fuse_cmdline_help();
		fuse_lib_help(&args);
		res = rv_ok;
		goto out_no_fuse;
	}
	if (!opts.mountpoint) {
		eprintln("error: no mountpoint specified");
		res = rv_missing_mountpoint;
		goto out_no_fuse;
	}

	opts.foreground = true;

	struct fuse *fuse = fuse_new(&args, &cfgfs_oper, sizeof(cfgfs_oper), NULL);
	if (fuse == NULL) {
		res = rv_fuse_setup_failed;
		goto out_no_fuse;
	}
	if (0 != fuse_mount(fuse, opts.mountpoint)) {
		res = rv_mount_failed;
		goto out_fuse_newed;
	}
	struct fuse_session *se = fuse_get_session(fuse);
	if (0 != fuse_set_signal_handlers(se)) {
		res = rv_signal_failed;
		goto out_fuse_newed_and_mounted;
	}
	g_main_thread = pthread_self();

	// ~ init other things ~

	if (!lua_init()) {
		res = rv_cfgfs_lua_failed;
		goto out_fuse_newed_and_mounted_and_signals_handled;
	}
	attention_init();
	cli_input_init();
	click_init();
	reloader_init();

	// ~ boot up fuse ~

	int loop_rv = fuse_loop_mt(fuse, &(struct fuse_loop_config){
		.clone_fd = false,
		.max_idle_threads = 5,
	});
	res = rv_ok;
	if (loop_rv != 0 && !main_quit_called) {
		res = rv_fs_error;
	}

out_fuse_newed_and_mounted_and_signals_handled:
	g_main_thread = 0;
	fuse_remove_signal_handlers(se);
out_fuse_newed_and_mounted:
	fuse_unmount(fuse);
out_fuse_newed:
	fuse_destroy(fuse);
out_no_fuse:
	lua_deinit();
	attention_deinit();
	cli_input_deinit();
	click_deinit();
	reloader_deinit();
	free(opts.mountpoint);
	fuse_opt_free_args(&args);
	if (0 == unlink(".cfgfs_reexec")) {
		execvp(argv[0], argv);
		perror("cfgfs: exec");
		return rv_cfgfs_reexec_failed;
	}
	return (int)res;

}
