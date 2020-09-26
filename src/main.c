#include "main.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wdocumentation"
 #pragma GCC diagnostic ignored "-Wpadded"
  #include <fuse.h>
  #include <fuse_lowlevel.h>
#pragma GCC diagnostic pop

#include "lua.h"
#include "buffer_list.h"
#include "macros.h"
#include "buffers.h"
#include "reload_thread.h"
#include "logtail.h"

const size_t max_line_length = 510;
const size_t max_cfg_size = 1048576;
const size_t max_argc = 63;

const size_t reported_cfg_size = 1024;

// -----------------------------------------------------------------------------

static char *g_mountpoint;
static struct fuse_session *g_fuse_instance;

__attribute__((cold))
void main_stop(void) {
	if (g_fuse_instance != NULL) {
		fuse_session_exit(g_fuse_instance);
		close(open(g_mountpoint, O_RDONLY)); // wake up
	}
}

// -----------------------------------------------------------------------------

static lua_State *g_L;
static lua_State *get_state(void) {
	return g_L;
}

// -----------------------------------------------------------------------------

// have_file: check if a file exists (and get its type)

#define Eq(s) (strcmp(path, s) == 0)
#define St(s) (strncmp(path, s, strlen(s)) == 0)

#define Dir(s) \
	({ \
		bool rv = false; \
		if (St(s)) { \
			if (likely(*(path+strlen(s)) == '/')) { path += strlen(s)+1; rv = true; } \
			if (likely(*(path+strlen(s)) == '\0')) { return 0xd; } \
		} \
		rv; \
	})

static int have_file(const char *path) {

	if (unlikely(Eq("/"))) return 0xd;
	path += 1;

	if (likely(Dir("cfgfs"))) {
		// br*h
		const char *c = strrchr(path, '.');
		if (likely(c && strcmp(c+1, "cfg") == 0)) return 0xf;
		else return 0xd;
	}

	// basename contains no dot = assume directory
	const char *lastslash = strrchr(path, '/');
	const char *lastdot = strrchr(unlikely(lastslash != NULL) ? lastslash : path, '.');
	if (unlikely(lastdot == NULL)) {
		return 0xd;
	}

	LUA_LOCK();
	lua_State *L = get_state();

	 lua_getglobal(L, "unmask_next");
	  lua_getfield(L, -1, path);
	  int64_t cnt = lua_tointeger(L, -1);
	 lua_pop(L, 1);
	 if (unlikely(cnt != 0)) {
		  lua_pushstring(L, path);
		   if (likely(cnt != 1)) lua_pushinteger(L, cnt-1);
		   else lua_pushnil(L);
		 lua_settable(L, -3);
		lua_pop(L, 1);
D		assert(stack_is_clean(L));
		LUA_UNLOCK();
		return 0x0;
	 }
	lua_pop(L, 1);

	 lua_getglobal(L, "cfgfs");
	  lua_getfield(L, -1, "intercept_blacklist");
	   lua_getfield(L, -1, path);
	   bool blacklisted = lua_toboolean(L, -1);
	lua_pop(L, 3);
D	assert(stack_is_clean(L));
	if (unlikely(blacklisted)) {
		LUA_UNLOCK();
		return 0x0;
	}

	LUA_UNLOCK();

	if (likely(strcmp(lastdot+1, "cfg") == 0)) {
		return 0xf;
	}

	return 0x0;

}

#undef Eq
#undef St
#undef Dir

// -----------------------------------------------------------------------------

__attribute__((hot))
static int cfgfs_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
	(void)fi;
V	Debug1("%s", path);

	switch (have_file(path)) {
	case 0xf:
		stbuf->st_mode = 0400|S_IFREG;
		break;
	case 0xd:
		stbuf->st_mode = 0400|S_IFDIR;
		break;
	default:
		return -ENOENT;
	}

	stbuf->st_size = reported_cfg_size;

	// these affects how much is read() at once (details unclear)
	stbuf->st_blksize = reported_cfg_size;
	stbuf->st_blocks = 1;

	return 0;
}

__attribute__((hot))
static int cfgfs_open(const char *path, struct fuse_file_info *fi) {
	(void)fi;
V	Debug1("%s", path);

	switch (have_file(path)) {
	case 0xf:
	case 0xd:
		return 0;
	default:
		return -ENOENT;
	}
}

#define starts_with(this, that) (strncmp(this, that, strlen(that)) == 0)

__attribute__((hot))
static int cfgfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
	(void)fi;
V	Debug1("%s (size=%lu, offset=%lu)", path, size, offset);

	if (unlikely(offset != 0)) return 0; // not the first read() call?
	assert(size >= reported_cfg_size); // won't need a second read() call

	// /unmask_next/ must not return buffer contents (can mess up the order)
	bool silent = starts_with(path, "/cfgfs/unmask_next/");

	LUA_LOCK();

	struct buffer fakebuf;
	bool faked = false;
	if (likely(!silent)) faked = buffer_list_maybe_unshift_fake_buf(&buffers, &fakebuf, buf);

	lua_State *L = get_state();
	 lua_pushvalue(L, GET_CONTENTS_IDX);
	  lua_pushstring(L, path);
	lua_call(L, 1, 0);

	if (unlikely(silent)) {
		size = 0;
		goto end;
	}

	struct buffer *ent = buffer_list_grab_first_nonempty(&buffers);

	if (unlikely(ent == NULL)) {
		if (likely(faked)) buffer_list_remove_fake_buf(&buffers, &fakebuf);
		size = 0;
		goto end;
	}

	size = buffer_get_size(ent);

	if (unlikely(!faked)) {
		buffer_memcpy_to(ent, buf, size);
		buffer_free(ent);
	}
end:
	LUA_UNLOCK();
	return (int)size;
}

__attribute__((cold))
static void *cfgfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	(void)conn;
	lua_State *L = get_state();

	// https://libfuse.github.io/doxygen/structfuse__config.html
	cfg->direct_io = 1;
	cfg->set_gid = 1000;
	cfg->set_uid = 1000;

	 lua_getglobal(L, "cwd");
	 const char *cwd = lua_tostring(L, -1);
	 if (chdir(cwd) != 0) {
	 	perror("chdir");
	 	goto abort;
	 }
	lua_pop(L, 1);

	cli_thread_start(L);

	 lua_getglobal(L, "gamedir");
	 logtail_start(lua_tostring(L, -1));
	lua_pop(L, 1);

	reload_thread_start(L);

	return (void *)L;
abort:
	raise(SIGTERM);
	return NULL;
}

// -----------------------------------------------------------------------------

static const struct fuse_operations cfgfs_oper = {
	.getattr = cfgfs_getattr,
	.open = cfgfs_open,
	.read = cfgfs_read,
	.init = cfgfs_init,
};

__attribute__((cold))
int main(int argc, char **argv) {
	set_thread_name("cfgfs_main");

#ifdef SANITIZER
	fprintf(stderr, "note: cfgfs was built with %s\n", SANITIZER);
#endif

	// PGO message needs to be carefully printed not to invalidate the profile
	{
		char *msg;
#if defined(PGO) && PGO == 1
		msg = "note: this is a PGO profiling build, rebuild with PGO=2 when finished\n";
#else
		msg = "\r";
#endif
		fprintf(stderr, "%s", msg);
	}

D	fprintf(stderr, "note: debug code is enabled\n");
V	fprintf(stderr, "note: verbose messages are enabled\n");
VV	fprintf(stderr, "note: very verbose messages are enabled\n");

	signal(SIGCHLD, SIG_IGN); // <-- what was this for?
	// ^ something about lua
	// did it make these signals? did they affect cfgfs?

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
		goto out1;
	}

	if (opts.show_help) {
		if (args.argv[0][0] != '\0') {
			printf("usage: %s [options] <mountpoint>\n\n", args.argv[0]);
		}
		printf("FUSE options:\n");
		fuse_cmdline_help();
		fuse_lib_help(&args);
		res = 0;
		goto out1;
	}

	if (!opts.show_help && !opts.mountpoint) {
		fprintf(stderr, "error: no mountpoint specified\n");
		res = 2;
		goto out1;
	}

	opts.foreground = true;

	// ---------------------------------------------------------------------

	// boot up lua

	lua_State *L = lua_init();
	g_L = L;

	 lua_pushstring(L, opts.mountpoint);
	lua_setglobal(L, "mountpoint");

	if (luaL_loadfile(L, "./builtin.lua") != LUA_OK) {
		fprintf(stderr, "error: %s\n", lua_tostring(L, -1));
		fprintf(stderr, "failed to load builtin.lua!\n");
		res = 9;
		goto out1;
	}
	 lua_call(L, 0, 1);
	 if (!lua_toboolean(L, -1)) { res = 9; goto out1; }
	lua_pop(L, 1);
	assert(lua_gettop(L) == 0);

	lua_pushnil(L);
	assert(lua_gettop(L) == HAVE_FILE_IDX);

	lua_getglobal(L, "_get_contents");
	assert(lua_gettop(L) == GET_CONTENTS_IDX);

	buffer_list_swap(&buffers, &init_cfg);

	 lua_getglobal(L, "_fire_startup");
	lua_call(L, 0, 0);

	assert(stack_is_clean(L));

	// ---------------------------------------------------------------------

	fuse = fuse_new(&args, &cfgfs_oper, sizeof(cfgfs_oper), L);
	if (fuse == NULL) {
		res = 3;
		goto out1;
	}

	if (fuse_mount(fuse, opts.mountpoint) != 0) {
		res = 4;
		goto out2;
	}

	if (fuse_daemonize(opts.foreground) != 0) {
		res = 5;
		goto out3;
	}

	struct fuse_session *se = fuse_get_session(fuse);
	if (fuse_set_signal_handlers(se) != 0) {
		res = 6;
		goto out3;
	}

	 lua_getglobal(L, "cfgfs");
	  lua_getfield(L, -1, "fuse_singlethread");
	  opts.singlethread = lua_toboolean(L, -1);
	lua_pop(L, 2);

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

	g_mountpoint = NULL;
	g_fuse_instance = NULL;

	fuse_remove_signal_handlers(se);
out3:
	fuse_unmount(fuse);
out2:
	fuse_destroy(fuse);
out1:
	reload_thread_stop();
	logtail_stop();
	cli_thread_stop();
	if (g_L != NULL) {
		if (LUA_TRYLOCK()) {
			lua_close((lua_State *)g_L);
			LUA_UNLOCK();
		} else {
			fprintf(stderr, "warning: failed to lock lua state for closing!\n");
		}
	}
	free(opts.mountpoint);
	fuse_opt_free_args(&args);
	return res;

}
