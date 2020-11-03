#include "main.h"

#include <errno.h>
#include <float.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "logtail.h"
#include "lua.h"
#include "macros.h"
#include "reloader.h"
#include "vcr.h"

const size_t max_line_length = 510;
const size_t max_cfg_size = 1048576;
const size_t max_argc = 63;

// how big we report config files to be.
// one can contain less data than this but not more
// * the lowest safe value is 529 (`max_line_length + strlen("\nexec cfgfs/buffer\n")`)
//   but 1024 is the lowest value that still works well
const size_t reported_cfg_size = 1024;

_Static_assert(sizeof(AGPL_SOURCE_URL) > 1, "AGPL_SOURCE_URL not set to a valid value");

// whether to define these useless functions
//#define WITH_RELEASE
//#define WITH_READDIR

// -----------------------------------------------------------------------------

static _Atomic(struct fuse_session *) g_fuse_instance;
static _Atomic(const char *) g_mountpoint;
static _Atomic(bool) g_reexec;

__attribute__((cold))
void main_stop(void) {
	static _Atomic(bool) called;
	struct fuse_session *fuse_instance = g_fuse_instance;
	const char *mountpoint = g_mountpoint;
	if (fuse_instance != NULL && mountpoint != NULL && !called++) {
		fuse_session_exit(fuse_instance);

		// fuse_session_exit() only sets a flag, need to wake it up
		int fd = open(mountpoint, O_RDONLY);
		if (fd != -1) close(fd);
	}
}

__attribute__((cold))
static void hup_handler(int signal) {
	(void)signal;
	g_reexec = true;

	// call main_stop() but safely
	cli_input_manual_eof();
	// raise(SIGTERM) doesn't work either since it needs to be woken up
}

// -----------------------------------------------------------------------------

static _Atomic(lua_State *) g_L;

static lua_State *get_state(void) {
	return g_L;
}

// -----------------------------------------------------------------------------

// have_file: check if a file exists (and get its type)
// i regret making this unreadable for a 0.00001% performance improvement

static int have_file(const char *restrict path) {
	size_t len = strlen(path);

	if (likely(len >= strlen("/cfgfs"))) {
		if (likely(*(path+strlen("/cfgfs")) == '/')) {
			if (memcmp(path+len-strlen(".cfg"), ".cfg", sizeof(".cfg")) == 0) {
				return 0xf;
			} else {
				return 0xd;
			}
		}
		if (likely(*(path+strlen("/cfgfs")) == '\0')) return 0xd;
	} else if (unlikely(len == 1)) {
		return 0xd;
	}

	// basename contains no dot = assume directory
	// todo:
	// could this just check if it ends with .cfg?
	// has this thing ever worked for intercepting non-cfg files?
	// it doesn't sound that useful
	{
	const char *p = path+len;
	do {
		switch (*--p) {
		case '.': goto basename_contains_dot;
		case '/': return 0xd;
		}
	} while (p != path);
	}
basename_contains_dot:

	goto do_lua_check;
after_lua_check:

	if (len >= strlen("/")+strlen(".cfg") &&
	    memcmp(path+len-strlen(".cfg"), ".cfg", sizeof(".cfg")) == 0) {
		return 0xf;
	}

	return 0x0;
do_lua_check:

	LUA_LOCK();
	lua_State *L = get_state();

	 lua_pushvalue(L, UNMASK_NEXT_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  int64_t cnt = lua_tointeger(L, -1);
	  if (unlikely(cnt != 0)) {
		   cnt -= 1;
		   lua_pushstring(L, path+1); // skip "/"
		    lua_pushinteger(L, cnt);
		  lua_rawset(L, -4);
		lua_pop(L, 2);
D		assert(stack_is_clean(L));
//D		eprintln("unmask_next[%s]: %ld -> %ld", path+1, cnt+1, cnt);

		LUA_UNLOCK();
		return 0x0;
	  }
//	lua_pop(L, 2); // combined below

	 lua_pushvalue(L, CFG_BLACKLIST_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  bool blacklisted = lua_toboolean(L, -1);
	lua_pop(L, 2+2);
D	assert(stack_is_clean(L));
	if (unlikely(blacklisted)) {
		LUA_UNLOCK();
		return 0x0;
	}

	LUA_UNLOCK();
	goto after_lua_check;
}

// -----------------------------------------------------------------------------

__attribute__((hot))
static int cfgfs_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
	(void)fi;
V	eprintln("cfgfs_getattr: %s", path);
	int rv = 0;
	double tm = vcr_get_timestamp();

	switch (have_file(path)) {
	case 0xf:
		stbuf->st_mode = 0400|S_IFREG;
		break;
	case 0xd:
		stbuf->st_mode = 0404|S_IFDIR;
		break;
	default:
		rv = -ENOENT;
		goto end;
	}

	// these affect how much is read() at once (details unclear)
	// less reads may be better, most reads output very little stuff
	// this logic is in glibc (lua file reads work the same)
	stbuf->st_size = reported_cfg_size;
	stbuf->st_blksize = (reported_cfg_size-1);
	stbuf->st_blocks = 1;

	// with size=1024 blksize=1023 blocks=(any),
	// the first read is 1024 and the second is 512
	// second is 512 when "blksize >= 512 && blksize < 1024"
	// for some reason
end:
	vcr_event {
		vcr_add_string("what", "getattr");
		vcr_add_string("path", path);
		//vcr_add_integer("fd", (fi != NULL) ? (long long)fi->fh : -1);
		vcr_add_integer("rv", (long long)rv);
		vcr_add_double("timestamp", tm);
	}
	return rv;
}

// ~

__attribute__((hot))
static int cfgfs_open(const char *path, struct fuse_file_info *fi) {
V	eprintln("cfgfs_open: %s", path);
	int rv = 0;
	double tm = vcr_get_timestamp();

	switch (have_file(path)) {
	case 0xf:
	case 0xd:
#if defined(WITH_VCR)
		{
		static uint64_t handle = 0;
		fi->fh = ++handle;
		}
#endif
		break;
	default:
		rv = -ENOENT;
		break;
	}
	vcr_event {
		vcr_add_string("what", "open");
		vcr_add_string("path", path);
		if (rv == 0) {
			vcr_add_integer("fd", (long long)fi->fh);
		}
		vcr_add_integer("rv", (long long)rv);
		vcr_add_double("timestamp", tm);
	}
	return rv;
}

// ~

#define starts_with(this, that) (strncmp(this, that, strlen(that)) == 0)

__attribute__((hot))
static int cfgfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
V	eprintln("cfgfs_read: %s (size=%lu, offset=%lu)", path, size, offset);
	int rv = 0;
	double tm;

	// the first call to read() is with size=(block) offset=0
	// then if rv > 0, it makes a second call like size=(block) offset=(rv)
	// we can detect that second call and do nothing since normally one read is enough
	if (likely(offset == 0)) {
		assert(size >= reported_cfg_size); // fits in one read
	} else {
		tm = vcr_get_timestamp();
		goto end_unlocked;
	}

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = starts_with(path, "/cfgfs/unmask_next/");

	LUA_LOCK();
	tm = vcr_get_timestamp();

	struct buffer fakebuf;
	bool faked = false;
	if (likely(!silent)) faked = buffer_list_maybe_unshift_fake_buf(&buffers, &fakebuf, buf);

	lua_State *L = get_state();
	 lua_pushvalue(L, GET_CONTENTS_IDX);
	  lua_pushstring(L, path);
	lua_call(L, 1, 0);

	if (unlikely(silent)) {
		goto end_locked;
	}

	struct buffer *ent = buffer_list_grab_first_nonempty(&buffers);

	if (unlikely(ent == NULL)) {
		if (likely(faked)) buffer_list_remove_fake_buf(&buffers, &fakebuf);
		goto end_locked;
	}

	size_t outsize = buffer_get_size(ent);
	rv = (int)outsize;

	if (unlikely(!faked)) {
		buffer_memcpy_to(ent, buf, outsize);
		buffer_free(ent);
	}
end_locked:
	LUA_UNLOCK();
end_unlocked:
	vcr_event {
		// we need it null-terminated for a bit
		char tmp = exchange(buf[(size_t)rv], '\0');
		vcr_add_string("what", "read");
		vcr_add_integer("size", (long long)size);
		vcr_add_integer("offset", (long long)offset);
		vcr_add_integer("fd", (long long)fi->fh);
		vcr_add_integer("rv", (long long)rv);
		vcr_add_string("data", buf);
		vcr_add_double("timestamp", tm);
		buf[(size_t)rv] = tmp;
	}
	VV {
		if (rv >= 0) {
			char tmp = exchange(buf[(size_t)rv], '\0');
			eprintln("data=[[%s]] rv=%d", buf, rv);
			buf[(size_t)rv] = tmp;
		} else {
			eprintln("data=(null) rv=%d", rv);
		}
	}
	return rv;
}

#undef starts_with

// ~

#if defined(WITH_VCR) && !defined(WITH_RELEASE)
 #define WITH_RELEASE
#endif

#if defined(WITH_RELEASE)

static int cfgfs_release(const char *path, struct fuse_file_info *fi) {
V	eprintln("cfgfs_release: %s", path);
	double tm = vcr_get_timestamp();
	vcr_event {
		vcr_add_string("what", "release");
		vcr_add_integer("fd", (long long)fi->fh);
		vcr_add_double("timestamp", tm);
	}
	return 0;
}

#endif

// ~

#if defined(WITH_READDIR)

#define add(name, mode) \
	({ \
		struct stat st = {0}; \
		st.st_mode = mode; \
		if (!failed) failed += !!filler(buf, name, &st, 0, (enum fuse_fill_dir_flags)0); \
	})
#define add_dir(name) add(name, S_IFDIR)
#define add_file(name) add(name, S_IFREG)

// this is pretty useless
// the whole filesystem is useless outside the game
// the game doesn't do anything useful with this

__attribute__((cold))
static int cfgfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {
V	eprintln("cfgfs_readdir: %s", path);
	(void)fi;
	(void)flags;
	bool failed = false;
	if (offset != 0) return -EINVAL;
	add_dir(".");
	add_dir("..");
	if (strcmp(path, "/") == 0) {
		add_dir("cfgfs");
		return 0;
	} else if (strcmp(path, "/cfgfs") == 0) {
		add_dir("alias");
		add_dir("keys");
		add_dir("resume");
		add_dir("unmask_next");
		add_file("buffer.cfg");
		add_file("init.cfg");
		add_file("license.cfg");
		return 0;
	} else if (strcmp(path, "/cfgfs/alias") == 0) {
		LUA_LOCK();
		lua_State *L = get_state();
		lua_getglobal(L, "cmd");
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			lua_pop(L, 1); // don't need the value
			if (lua_type(L, -1) == LUA_TSTRING) {
				char namebuf[max_line_length+1];
				*namebuf = '\0';
				snprintf(namebuf, sizeof(namebuf), "%s.cfg", lua_tostring(L, -1));
				if (*namebuf) add_file(namebuf);
			}
		}
		lua_pop(L, 1);
		assert(stack_is_clean(L));
		LUA_UNLOCK();
		return 0;
	} else if (strcmp(path, "/cfgfs/keys") == 0) {
		return -ENOSYS;
	} else if (strcmp(path, "/cfgfs/resume") == 0) {
		return -ENOSYS;
	} else if (strcmp(path, "/cfgfs/unmask_next") == 0) {
		return -ENOSYS;
	} else {
		return -ENOENT;
	}
}

#undef add
#undef add_dir
#undef add_file

#endif

// ~

__attribute__((cold))
static void *cfgfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	lua_State *L = get_state();

	// https://github.com/libfuse/libfuse/blob/0105e06/include/fuse_common.h#L421
	(void)conn;

	// https://github.com/libfuse/libfuse/blob/f54eb86/include/fuse.h#L93
	cfg->set_gid = true;
	cfg->gid = getegid();
	cfg->set_uid = true;
	cfg->uid = geteuid();

	// important but i forgot why
	// it seemed to work with this accidentally removed (but all reads were with size=4096)
	cfg->direct_io = true;

	cfg->entry_timeout = DBL_MAX; // cache timeout for names (default 1s)
	cfg->negative_timeout = 0; // cache timeout for deleted names (default 0s)
	cfg->attr_timeout = DBL_MAX; // cache timeout for attributes (default 1s)
	// these were previously set to defaults but DBL_MAX seems to work the same
	// "negative_timeout = DBL_MAX" breaks unmask_next
	// with "entry_timeout = 0", unmask_next needs to set 4 instead of 3

	 lua_getglobal(L, "cwd");
	 const char *cwd = lua_tostring(L, -1);
	 check_minus1(
	     chdir(cwd),
	     "cfgfs_init: chdir",
	     goto err);
	lua_pop(L, 1);

	cli_input_init(L);
	logtail_init(L);
	reloader_init(L);
	attention_init(L);

	return (void *)L;
err:
	raise(SIGTERM);
	return NULL;
}

// -----------------------------------------------------------------------------

static const struct fuse_operations cfgfs_oper = {
	.getattr = cfgfs_getattr,
	.open = cfgfs_open,
	.read = cfgfs_read,
#if defined(WITH_RELEASE)
	.release = cfgfs_release,
#endif
#if defined(WITH_READDIR)
	.readdir = cfgfs_readdir,
#endif
	.init = cfgfs_init,
};

__attribute__((cold))
int main(int argc, char **argv) {

#ifdef SANITIZER
	fprintf(stderr, "NOTE: cfgfs was built with %s\n", SANITIZER);
#endif

#if defined(PGO) && PGO == 1
	fprintf(stderr, "NOTE: this is a PGO profiling build, rebuild with PGO=2 when finished\n");
#endif

D	fprintf(stderr, "NOTE: debug code is enabled\n");
V	fprintf(stderr, "NOTE: verbose messages are enabled\n");
VV	fprintf(stderr, "NOTE: very verbose messages are enabled\n");

#if defined(WITH_VCR)
	fprintf(stderr, "NOTE: events are being logged to vcr.log\n");
#endif

	signal(SIGCHLD, SIG_IGN); // zombie killer
	signal(SIGHUP, hup_handler);

	// ---------------------------------------------------------------------

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse *fuse;
	struct fuse_cmdline_opts opts;
	int res;

	if (fuse_parse_cmdline(&args, &opts) != 0) {
		return 1;
	}

	if (opts.show_version) {
		fuse_lowlevel_version();
		res = 0;
		goto out_no_fuse;
	}

	if (opts.show_help) {
		if (args.argv[0][0] != '\0') {
			println("usage: %s [options] <mountpoint>", args.argv[0]);
		}
		println("FUSE options:");
		fuse_cmdline_help();
		fuse_lib_help(&args);
		res = 0;
		goto out_no_fuse;
	}

	if (!opts.show_help && !opts.mountpoint) {
		eprintln("error: no mountpoint specified");
		res = 2;
		goto out_no_fuse;
	}

	opts.foreground = true;

	// ---------------------------------------------------------------------

	// boot up lua

	lua_State *L = lua_init();
	g_L = L;

	lua_pushstring(L, AGPL_SOURCE_URL); lua_setglobal(L, "agpl_source_url");
	lua_pushstring(L, opts.mountpoint); lua_setglobal(L, "mountpoint");

	if (luaL_loadfile(L, "./builtin.lua") != LUA_OK) {
		eprintln("error: %s", lua_tostring(L, -1));
		eprintln("failed to load builtin.lua!");
		res = 9;
		goto out_no_fuse;
	}
	 lua_call(L, 0, 1);
	 if (!lua_toboolean(L, -1)) { res = 9; goto out_no_fuse; }
	lua_pop(L, 1);

	// put some useful values on the stack
	// constants defined in lua.h

	 lua_getglobal(L, "_get_contents");
	assert(lua_gettop(L) == GET_CONTENTS_IDX);

	  lua_getglobal(L, "unmask_next");
	assert(lua_gettop(L) == UNMASK_NEXT_IDX);

	   lua_getglobal(L, "cfgfs");
	    lua_getfield(L, -1, "intercept_blacklist");
	    lua_rotate(L, -2, 1);
	   lua_pop(L, 1);
	assert(lua_gettop(L) == CFG_BLACKLIST_IDX);

	buffer_list_swap(&buffers, &init_cfg);

	 lua_getglobal(L, "_fire_startup");
	lua_call(L, 0, 0);

	 lua_getglobal(L, "cfgfs");
	  lua_getfield(L, -1, "fuse_singlethread");
	  opts.singlethread = lua_toboolean(L, -1);
	lua_pop(L, 2);

	assert(stack_is_clean(L));

	// ---------------------------------------------------------------------

	fuse = fuse_new(&args, &cfgfs_oper, sizeof(cfgfs_oper), L);
	if (fuse == NULL) {
		res = 3;
		goto out_no_fuse;
	}

	if (fuse_mount(fuse, opts.mountpoint) != 0) {
		res = 4;
		goto out_fuse_inited;
	}

	if (fuse_daemonize(opts.foreground) != 0) {
		res = 5;
		goto out_fuse_inited_and_mounted;
	}

	struct fuse_session *se = fuse_get_session(fuse);
	if (fuse_set_signal_handlers(se) != 0) {
		res = 6;
		goto out_fuse_inited_and_mounted;
	}

	g_mountpoint = opts.mountpoint;
	g_fuse_instance = se;

	if (opts.singlethread) {
		res = fuse_loop(fuse);
	} else {
		struct fuse_loop_config loop_config;
		loop_config.clone_fd = opts.clone_fd;
		loop_config.max_idle_threads = opts.max_idle_threads;
		res = fuse_loop_mt(fuse, &loop_config);
	}
	if (res) {
		res = 7;
	}

	fuse_remove_signal_handlers(se);
out_fuse_inited_and_mounted:
	fuse_unmount(fuse);
out_fuse_inited:
	fuse_destroy(fuse);
out_no_fuse:
	vcr_save();
	g_mountpoint = NULL;
	g_fuse_instance = NULL;
	cli_input_deinit();
	attention_deinit();
	reloader_deinit();
	logtail_deinit();
	if (g_L != NULL) lua_close(g_L);
	free(opts.mountpoint);
	fuse_opt_free_args(&args);
	if (g_reexec) {
		execvp(argv[0], argv);
		eprintln("cfgfs: exec: %s", strerror(errno));
		return 1;
	}
	return res;

}
