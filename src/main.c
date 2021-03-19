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
#include "immediate_entry.h"
#include "lua.h"
#include "macros.h"
#include "reloader.h"

// -----------------------------------------------------------------------------

static pthread_t g_main_thread;

// true if main_quit() has been called (checked during exit)
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
	sft_message = 3,
};
union sft_ptr {
	uint64_t fh; // fuse_file_info::fh
	enum special_file_type sft;
};

#define FILE_TYPE_BITS      0b01111
#define ORDINARY_CONFIG_BIT 0b10000

#define FH_GET_TYPE(x) (enum special_file_type)((x) & FILE_TYPE_BITS)
#define FH_IS_ORDINARY(x) (((x) & ORDINARY_CONFIG_BIT) != 0)

#define MESSAGE_DIR_PREFIX "/message/"

static int l_lookup_path(const char *restrict path, bool is_open);

#define starts_with(self, other) \
	(0 == memcmp(self, other, strlen(other)))

#define ends_with(self, other) \
	(0 == memcmp(self+length-strlen(other), other, strlen(other)))

#define equals(self, other) \
	(length == strlen(other) && \
	 0 == memcmp(self, other, length))

__attribute__((always_inline))
static int lookup_path(const char *restrict path,
                       union sft_ptr *type,
                       bool is_open) {
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

	// party trick:
	// if the filename part doesn't contain a dot, we can guess that the
	//  path is probably meant to be a directory
	if (unlikely(*slashdot == '/')) {
		return 0xd;
	}

	// not a config? (source won't load configs not ending with .cfg)
	if (unlikely(!ends_with(path, ".cfg"))) {
		if (unlikely(length > strlen(MESSAGE_DIR_PREFIX) &&
		             starts_with(path, MESSAGE_DIR_PREFIX))) {
			if (type) type->sft = sft_message;
			return 0xf;
		}
		if (unlikely(equals(path, "/console.log"))) {
			if (type) type->sft = sft_console_log;
			return 0xf;
		}
		if (unlikely(equals(path, "/.control"))) {
			if (type) type->sft = sft_control;
			return 0xf;
		}
		return -ENOENT;
	}

	// (checked so far: it's a file whose name ends with .cfg)

	// is it in the cfgfs directory?
	if (likely(starts_with(path, "/cfgfs/"))) {
		return 0xf;
	}

	// it's probably a real config
	// check with the lua function if we should intercept it or not
	if (type) type->fh |= ORDINARY_CONFIG_BIT;
	return l_lookup_path(path, is_open);
}

#undef starts_with
#undef ends_with
#undef equals

// name of the last "ordinary" config that was opened, and how many times it was opened
// the purpose of this is to detect the case when the game manually reads config.cfg at startup
// we don't want to call into lua or return any buffer contents in that case because it'll be discarded
static char last_opened_config_name[32];
static unsigned int last_opened_config_cnt;

__attribute__((noinline))
static int l_lookup_path(const char *restrict path, bool is_open) {
	lua_State *L = lua_get_state("lookup_path");
	if (unlikely(L == NULL)) return -errno;
	 lua_getglobal(L, "_lookup_path");
	  lua_pushstring(L, path+1); // skip "/"
	 lua_call(L, 1, 1);
	 int rv = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);
D	assert(rv == 0 || rv == 0xd || rv == 0xf);
	if (rv == 0) {
		rv = -ENOENT;
	} else if (rv == 0xf && is_open) {
		if (0 == strcmp(path, last_opened_config_name)) {
			last_opened_config_cnt += 1;
		} else {
			snprintf(last_opened_config_name, sizeof(last_opened_config_name), "%s", path);
			last_opened_config_cnt = 0;
		}
	}
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

	int rv = lookup_path(path, NULL, false);

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

	int rv = lookup_path(path, (union sft_ptr *)&fi->fh, true);

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

	// check that it's not some special file. we only support reading configs here
	if (unlikely(FH_GET_TYPE(fi->fh) != sft_none)) {
V		eprintln("cfgfs_read: can't read this type of file!");
		return -EOPNOTSUPP;
	}

	// catch the game manually reading config.cfg at startup so we don't
	//  lose any buffer contents to that
	if (
		unlikely(FH_IS_ORDINARY(fi->fh)) &&
		unlikely(last_opened_config_cnt <= 1) &&
		likely(0 == strcmp(path, last_opened_config_name))
	) {
		last_opened_config_cnt = 0;
		if (unlikely(0 != strcmp(path, "/config.cfg"))) {
			eprintln("cfgfs_read: warning: ignoring manual read of %s",
			    path+1);
		}
		return 0;
	}

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = false;
	if (unlikely(starts_with(path, "/cfgfs/unmask_next/"))) {
		silent = true;
	}

	lua_State *L = lua_get_state("cfgfs_read");
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
D		assert(ent != NULL); // should be fakebuf or a real one

		// is this even a good idea
		if (
			likely(!ent->full) &&
			unlikely(expecting_immediate_entry > 0)
		) {
			lua_release_state_no_click(L);
			buffer_make_full(ent);
			rv = (int)buffer_get_size(ent);
			if (unlikely(ent != &fakebuf)) {
				buffer_memcpy_to(ent, buf, buffer_get_size(ent));
				buffer_free(ent);
			}
			wait_immediate_entries();
			goto out_unlocked;
		}

		rv = (int)buffer_get_size(ent);
		if (unlikely(ent != &fakebuf)) {
			buffer_memcpy_to(ent, buf, buffer_get_size(ent));
			buffer_free(ent);
		}
	}
	lua_release_state_no_click(L);
out_unlocked:
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

__attribute__((hot))
static int cfgfs_write(const char *restrict path,
                       const char *restrict data,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *restrict fi) {
	(void)fi;
VV	eprintln("cfgfs_write: %s (size=%lu, offset=%lu)", path, size, offset);

	switch (FH_GET_TYPE(fi->fh)) {
	case sft_console_log: {
		bool complete = (size >= 2 && data[size-1] == '\n');
		lua_State *L = lua_get_state("cfgfs_write/sft_console_log");
		if (unlikely(L == NULL)) {
			return -errno;
		}
		 lua_pushvalue(L, GAME_CONSOLE_OUTPUT_IDX);
		  lua_pushlstring(L, data, likely(complete) ? size-1 : size);
		   lua_pushboolean(L, complete);
		lua_call(L, 2, 0);
		lua_release_state(L);
		return (int)size;
	}
	case sft_control:
		return cfgfs_write_control(data, size);
	case sft_message: {
		assert(0 == strncmp(path, MESSAGE_DIR_PREFIX, strlen(MESSAGE_DIR_PREFIX)));
		lua_State *L = lua_get_state("cfgfs_write/sft_message");
		if (unlikely(L == NULL)) return -errno;
		 lua_getglobal(L, "_message");
		  lua_pushstring(L, path+strlen(MESSAGE_DIR_PREFIX));
		   lua_pushlstring(L, data, size);
		lua_call(L, 2, 0);
		lua_release_state(L);
		return (int)size;
	}
	case sft_none:
		return -ENOTSUP;
	}
	assert_unreachable();
}

#undef log_catcher_got_line

// ~

static void control_do_line(const char *line, size_t size);

__attribute__((cold))
__attribute__((noinline))
static int cfgfs_write_control(const char *data, size_t size) {
	if (size == 8192) return -EMSGSIZE; // might be truncated
	if (!lua_lock_state("cfgfs_write/sft_control")) return -errno;

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

static int cfgfs_release(const char *path, struct fuse_file_info *fi) {
V	eprintln("cfgfs_release: %s", path);
	switch (FH_GET_TYPE(fi->fh)) {
	case sft_none:
		return 0;
	case sft_message: {
		lua_State *L;
		assert(0 == strncmp(path, MESSAGE_DIR_PREFIX, strlen(MESSAGE_DIR_PREFIX)));
		if (!(L = lua_get_state("cfgfs_release/sft_message"))) return -errno;
		 lua_getglobal(L, "_message");
		  lua_pushstring(L, path+strlen(MESSAGE_DIR_PREFIX));
		   lua_pushnil(L);
		lua_call(L, 2, 0);
		lua_release_state(L);
		return 0;
	}
	case sft_console_log:
	case sft_control:
		return 0;
	}
	assert_unreachable();
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

	lua_State *L = lua_get_state("cfgfs_init");
	if (L != NULL) {
		 lua_getglobal(L, "_fire_startup");
		lua_call(L, 0, 0);
		lua_release_state(exchange(L, NULL));
	}

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
	.release = cfgfs_release,
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
		{"CFGFS_MOUNTPOINT", "path to the directory cfgfs was mounted in"},
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
