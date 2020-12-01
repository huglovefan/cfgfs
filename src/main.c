#include "main.h"

#include <errno.h>
#include <float.h>
#include <pthread.h>
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

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lauxlib.h>
#endif

#include "attention.h"
#include "buffer_list.h"
#include "buffers.h"
#include "cli_input.h"
#include "cli_output.h"
#include "click.h"
#include "logtail.h"
#include "lua.h"
#include "macros.h"
#include "reloader.h"

_Static_assert(sizeof(AGPL_SOURCE_URL) > 1, "AGPL_SOURCE_URL not set to a valid value");

#if defined(TEST_SKIP_CLICK_IF_READ_WAITING_FOR_LOCK)
 _Atomic(_Bool) read_waiting_for_lock_;
#endif

// -----------------------------------------------------------------------------

static lua_State *g_L;

static lua_State *get_state(void) {
	return g_L;
}

// -----------------------------------------------------------------------------

enum main_quittingness {
	nothing = 0,
	quit_softly = 1,
};

static pthread_t g_main_thread;
static enum main_quittingness quitstate;

__attribute__((cold))
void main_quit(void) {
	pthread_t main_thread = g_main_thread;
	if (main_thread) {
		pthread_kill(main_thread, SIGINT);
		quitstate = quit_softly;
	}
}

// -----------------------------------------------------------------------------

// have_file: check if a file exists (and get its type)
// returns 0xd (dir), 0xf (file) or a negative errno value

static int l_have_file(const char *restrict path);

#define starts_with(self, other) \
	(memcmp(self, other, strlen(other)) == 0)

#define ends_with(self, other) \
	(memcmp(self+length-strlen(other), other, strlen(other)) == 0)

__attribute__((always_inline))
static int have_file(const char *restrict path) {
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
		return -ENOENT;
	}

	if (likely(starts_with(path, "/cfgfs/"))) {
		return 0xf;
	}

	return l_have_file(path);
}

#undef starts_with
#undef ends_with

__attribute__((noinline))
static int l_have_file(const char *restrict path) {
	int rv = 0xf;

	if (unlikely(strcmp(path, "/.control") == 0)) {
		return 0xf;
	}

	if (unlikely(!LUA_TRYLOCK())) {
		if (!unlikely(LUA_TIMEDLOCK(1.0))) {
			return -EBUSY;
		}
	}

	lua_State *L = get_state();
	int64_t cnt;
	bool blacklisted;

	 lua_pushvalue(L, UNMASK_NEXT_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  cnt = lua_tointeger(L, -1);
	  if (unlikely(cnt != 0)) {
		   cnt -= 1;
		   lua_pushstring(L, path+1); // skip "/"
		    lua_pushinteger(L, cnt);
		  lua_rawset(L, -4);
		lua_pop(L, 2);
VV		eprintln("unmask_next[%s]: %ld -> %ld", path+1, cnt+1, cnt);
		rv = -ENOENT;
		goto out_locked;
	  }
	lua_pop(L, 2);

	 lua_pushvalue(L, CFG_BLACKLIST_IDX);
	  lua_getfield(L, -1, path+1); // skip "/"
	  blacklisted = lua_toboolean(L, -1);
	lua_pop(L, 2);
	if (unlikely(blacklisted)) {
		rv = -ENOENT;
		goto out_locked;
	}
out_locked:
D	assert(stack_is_clean(L));
	LUA_UNLOCK();
	return rv;
}

// -----------------------------------------------------------------------------

__attribute__((hot))
static int cfgfs_getattr(const char *restrict path,
                         struct stat *restrict stbuf,
                         struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_getattr: %s", path);

	int rv = have_file(path);
	if (unlikely(rv < 0)) return rv;

	stbuf->st_size = reported_cfg_size;

	if (rv == 0xf) {
		stbuf->st_mode = 0444|S_IFREG;
		return 0;
	} else if (rv == 0xd) {
		stbuf->st_mode = 0555|S_IFDIR;
		return 0;
	} else {
D		assert(0);
		__builtin_unreachable();
	}
}

// ~

__attribute__((hot))
static int cfgfs_open(const char *restrict path,
                      struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_open: %s", path);

	int rv = have_file(path);
	if (rv == 0xf) return 0;
	if (rv < 0) return rv;
	if (rv == 0xd) return -EISDIR;
D	assert(0);
	__builtin_unreachable();
}

// ~

#define starts_with(this, that) (strncmp(this, that, strlen(that)) == 0)

__attribute__((hot))
static int cfgfs_read(const char *restrict path,
                      char *restrict buf,
                      size_t size,
                      off_t offset,
                      struct fuse_file_info *restrict fi) {
	(void)fi;
V	eprintln("cfgfs_read: %s (size=%lu, offset=%lu)", path, size, offset);
	int rv = 0;

	// not known to happen
	if (unlikely(!(size >= reported_cfg_size && offset == 0))) {
D		eprintln("cfgfs_read: invalid argument! size=%zu offset=%zu", size, offset);
		return -EINVAL;
	}

#if defined(TEST_SKIP_CLICK_IF_READ_WAITING_FOR_LOCK)
	read_waiting_for_lock_ = 1;
#endif

	LUA_LOCK();

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = starts_with(path, "/cfgfs/unmask_next/");

	struct buffer fakebuf;
	if (likely(!silent)) {
		buffer_list_maybe_unshift_fake_buf(&buffers, &fakebuf, buf);
	}

#if defined(TEST_SKIP_CLICK_IF_READ_WAITING_FOR_LOCK)
	read_waiting_for_lock_ = 0;
#endif

	lua_State *L = get_state();
	 lua_pushvalue(L, GET_CONTENTS_IDX);
	  lua_pushstring(L, path);
	lua_call(L, 1, 0);

	if (likely(!silent)) {
D		assert(buffers.first != NULL); // nothing should've removed it
		assume(buffers.first != NULL); // optimize away the check
		struct buffer *ent = buffer_list_grab_first(&buffers);
		if (likely(ent != NULL)) {
			size_t outsize = buffer_get_size(ent);
			rv = (int)outsize;
			if (unlikely(ent != &fakebuf)) {
				buffer_memcpy_to(ent, buf, outsize);
				buffer_free(ent);
			}
		}
	}
	LUA_UNLOCK();
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

// maximum value for size seems to be 8192
// if the contents are bigger than that, it's written in multiple 8192-byte parts
// don't want to paste them together so just use it as a size limit
// should find out where the number comes from if it's system-dependent

static void control_do_line(const char *, size_t);

__attribute__((cold))
static int cfgfs_write(const char *path,
                       const char *data,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *fi) {
	(void)fi;
V	eprintln("cfgfs_write: %s (size=%lu, offset=%lu)", path, size, offset);

	if (strcmp(path, "/.control") != 0) return -ENOTSUP;
	if (size == 8192) return -EMSGSIZE; // might be truncated
	if (offset != 0) return -EINVAL;
	if (!LUA_TIMEDLOCK(3.0)) return -EBUSY;

	const char *p = data;
	for (;;) {
		const char *c = strchr(p, '\n');
		if (c == NULL) break;
		control_do_line(p, (size_t)(c-p));
		p = c+1;
	}

	opportunistic_click_and_unlock();

	return (int)size;
}

static void control_do_line(const char *line, size_t size) {
V	eprintln("cfgfs_write: line=[%s]", line);
	lua_State *L = get_state();
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

__attribute__((cold))
int main(int argc, char **argv) {

#if defined(SANITIZER)
	eprintln("NOTE: cfgfs was built with %s", SANITIZER);
#endif

#if defined(PGO) && PGO == 1
	eprintln("NOTE: this is a PGO profiling build, rebuild with PGO=2 when finished");
#endif

D	eprintln("NOTE: debug code is enabled");
V	eprintln("NOTE: verbose messages are enabled");
VV	eprintln("NOTE: very verbose messages are enabled");

	lua_State *L;

	// ~ init fuse ~

	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse *fuse;
	struct fuse_cmdline_opts opts;
	struct fuse_session *se;
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

	fuse = fuse_new(&args, &cfgfs_oper, sizeof(cfgfs_oper), NULL);
	if (fuse == NULL) {
		res = 3;
		goto out_no_fuse;
	}
	if (fuse_mount(fuse, opts.mountpoint) != 0) {
		res = 4;
		goto out_fuse_newed;
	}
	se = fuse_get_session(fuse);
	if (fuse_set_signal_handlers(se) != 0) {
		res = 6;
		goto out_fuse_newed_and_mounted;
	}
	g_main_thread = pthread_self();

	// ~ boot up lua ~

	L = lua_init();
	g_L = L;

	lua_pushstring(L, AGPL_SOURCE_URL); lua_setglobal(L, "agpl_source_url");
	lua_pushstring(L, opts.mountpoint); lua_setglobal(L, "mountpoint");

	if (luaL_loadfile(L, "./builtin.lua") != LUA_OK) {
		eprintln("error: %s", lua_tostring(L, -1));
		eprintln("failed to load builtin.lua!");
		res = 9;
		goto out_fuse_newed_and_mounted_and_signals_handled;
	}
	 lua_call(L, 0, 1);
	 if (!lua_toboolean(L, -1)) { res = 9; goto out_fuse_newed_and_mounted_and_signals_handled; }
	lua_pop(L, 1);

	// put these on the stack
	// see lua.h

	 lua_getglobal(L, "_get_contents");
	 assert(lua_gettop(L) == GET_CONTENTS_IDX);
	 assert(lua_type(L, GET_CONTENTS_IDX) == LUA_TFUNCTION);
	  lua_getglobal(L, "unmask_next");
	  assert(lua_gettop(L) == UNMASK_NEXT_IDX);
	  assert(lua_type(L, UNMASK_NEXT_IDX) == LUA_TTABLE);
	   lua_getglobal(L, "_game_console_output");
	   assert(lua_gettop(L) == GAME_CONSOLE_OUTPUT_IDX);
	   assert(lua_type(L, GAME_CONSOLE_OUTPUT_IDX) == LUA_TFUNCTION);
	    lua_getglobal(L, "cfgfs");
	     lua_getfield(L, -1, "intercept_blacklist");
	     lua_rotate(L, -2, 1);
	    lua_pop(L, 1);
	    assert(lua_gettop(L) == CFG_BLACKLIST_IDX);
	    assert(lua_type(L, CFG_BLACKLIST_IDX) == LUA_TTABLE);

	buffer_list_swap(&buffers, &init_cfg);

	 lua_getglobal(L, "_fire_startup");
	lua_call(L, 0, 0);

	assert(stack_is_clean(L));

	// ~ boot up threads ~

	attention_init(L);
	cli_input_init(L);
	click_init();
	logtail_init(L);
	reloader_init(L);

	// ~ boot up fuse ~

	res = fuse_loop_mt(fuse, &(struct fuse_loop_config){
		.clone_fd = false,
		.max_idle_threads = 5,
	});
	if (res) {
		res = 7;
	}
	if (quitstate == quit_softly) {
		res = 0;
	}

out_fuse_newed_and_mounted_and_signals_handled:
	g_main_thread = 0;
	fuse_remove_signal_handlers(se);
out_fuse_newed_and_mounted:
	fuse_unmount(fuse);
out_fuse_newed:
	fuse_destroy(fuse);
out_no_fuse:
	attention_deinit();
	cli_input_deinit();
	click_deinit();
	logtail_deinit();
	reloader_deinit();
	if (g_L != NULL) lua_close(exchange(g_L, NULL));
	free(opts.mountpoint);
	fuse_opt_free_args(&args);
	if (unlink(".cfgfs_reexec") == 0) {
		execvp(argv[0], argv);
		perror("cfgfs: exec");
		return 8;
	}
	return res;

}
