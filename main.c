/*
 * TODO:
 * - try to return accurate length
 *   - can i compute content in stat()? does it actually stat without reading
 *     when execing them ingame?
 *   - return length if it's a string
 */

#undef NDEBUG

#define FUSE_USE_VERSION 31

#define _GNU_SOURCE /* strchrnul() */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fuse.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#define STACK_CHECK_INIT(L) \
	int __top = lua_gettop(L)

#define STACK_CHECK(L, n) \
	if (lua_gettop(L) != __top + (n)) { \
		STACK_CHECK_FAIL(L, (n)); \
	}

#define STACK_CHECK_FAIL(L, n) \
	fprintf(stderr, "%s:%d: Stack check failed!\n", __FILE__, __LINE__); \
	fprintf(stderr, "Found %d too %s items\n", \
	                abs((__top + (n)) - lua_gettop(L)), \
	                (((__top + (n)) < lua_gettop(L)) ? "many" : "few")); \
	dbg_print_stack(L); \
	abort();

#define nodiscard __attribute__((warn_unused_result))
#define nonnull(...) __attribute__((nonnull(__VA_ARGS__)))
#define returns_nonnull __attribute__((returns_nonnull))

/* registry keys */
static int k_fs_table;		/* table returned by script */
static int k_fh_to_file;	/* file handle pointer -> whatever is in the table */
static int k_fh_to_content;	/* file handle pointer -> string */

enum ct_item_type {
	CT_NONE = 0,
	CT_FILE,
	CT_DIR,
};

static void nonnull(1)
dbg_print_stack(lua_State *L)
{
	int top = lua_gettop(L);

	fprintf(stderr, "Lua stack (%d):\n", top);
	for (int i = 1; i <= top; i++)
		fprintf(stderr, "%3d: %s\n", i, lua_typename(L, lua_type(L, i)));
}

static _Bool nodiscard nonnull(1,2)
get_path_component(const char **path, char out[PATH_MAX])
{
	const char *sep;
	size_t len;

	while (**path == '/')
		++*path;
	if (**path == '\0')
		return 0;

	sep = strchrnul(*path, '/');
	len = sep - *path;
	assert(len < PATH_MAX);

	memcpy(out, *path, len);
	out[len] = '\0';

	*path = sep + (*sep == '/' ? 1 : 0);

	return 1;
}

static void nonnull(1)
ct_get(lua_State *L)
{
	STACK_CHECK_INIT(L);
	lua_pushlightuserdata(L, &k_fs_table);
	lua_rawget(L, LUA_REGISTRYINDEX);
	STACK_CHECK(L, 1);
}

static void nonnull(1,2)
ct_cd(lua_State *L, const char *key)
{
	STACK_CHECK_INIT(L);
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	STACK_CHECK(L, 0);
}

static enum ct_item_type nodiscard nonnull(1)
ct_get_item_type(lua_State *L)
{
	switch (lua_type(L, -1)) {
	case LUA_TTABLE:
		return CT_DIR;
	case LUA_TFUNCTION:
	case LUA_TSTRING:
		return CT_FILE;
	default:
		return CT_NONE;
	}
}

static void *
cfgfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	lua_State *L;
	const char *script;
	(void)conn;
	(void)cfg;

	script = getenv("CFGFS_SCRIPT");
	assert(script != NULL);

	L = luaL_newstate();
	assert(L != NULL);

	STACK_CHECK_INIT(L);

	luaL_openlibs(L);

	lua_pushlightuserdata(L, &k_fs_table);

	luaL_loadfile(L, script);
	lua_call(L, 0, 1);
	assert(lua_type(L, -1) == LUA_TTABLE);
	lua_rawset(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, &k_fh_to_file);
	lua_newtable(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, &k_fh_to_content);
	lua_newtable(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	STACK_CHECK(L, 0);

	return L;
}

static void
cfgfs_destroy(void *private_data)
{
	lua_State *L = private_data;
	lua_close(L);
}

static int nodiscard nonnull(1,2)
ct_get_path(lua_State *L, const char *path)
{
	char comp[PATH_MAX];
	enum ct_item_type type;
	int rv = 0;
	STACK_CHECK_INIT(L);

	ct_get(L);
	type = CT_DIR;
	while (get_path_component(&path, comp)) {
		ct_cd(L, comp);
		type = ct_get_item_type(L);
		if (type == CT_NONE)
			break;
		if (type == CT_FILE && *path != '\0') {
			rv = -ENOTDIR;
			goto pop_out;
		}
	}
	if (type == CT_NONE) {
		rv = -ENOENT;
		goto pop_out;
	}
out:
	STACK_CHECK(L, (rv >= 0) ? 1 : 0);
	return rv;
pop_out:
	lua_pop(L, 1);
	goto out;
}

static int nodiscard nonnull(1,2)
ct_get_path_with_type(lua_State *L, const char *path, enum ct_item_type want_type)
{
	enum ct_item_type type;
	int rv;
	STACK_CHECK_INIT(L);

	if ((rv = ct_get_path(L, path)) < 0)
		goto out;

	type = ct_get_item_type(L);
	if (type != want_type) {
		switch (type) {
		case CT_DIR:	rv = -EISDIR; goto pop_out;
		case CT_FILE:	rv = -ENOTDIR; goto pop_out;
		default:	rv = -EIO; goto pop_out;
		}
	}
out:
	STACK_CHECK(L, (rv >= 0) ? 1 : 0);
	return rv;
pop_out:
	lua_pop(L, 1);
	goto out;
}

static int nodiscard
ct_file_get_contents(lua_State *L, void *fh)
{
	int rv = 0;
	STACK_CHECK_INIT(L);

	lua_pushlightuserdata(L, &k_fh_to_content);		/* ctk */
	lua_rawget(L, LUA_REGISTRYINDEX);			/* ct */

	lua_pushlightuserdata(L, fh);				/* ct fck */
	STACK_CHECK(L, 2);
	lua_rawget(L, -2);					/* ct fc */

	if (lua_isnil(L, -1)) { /* not cached, generate content */
		lua_pop(L, 1);					/* ct */

		lua_pushlightuserdata(L, &k_fh_to_file);	/* ct ftk */
		lua_rawget(L, LUA_REGISTRYINDEX);		/* ct ft */

		lua_pushlightuserdata(L, fh);			/* ct ft ffk */
		lua_rawget(L, -2);				/* ct ft ff */
		lua_remove(L, -2);				/* ct ff */

		switch (lua_type(L, -1)) {
		case LUA_TFUNCTION:
			/* i am lazy */
			luaL_loadstring(L, "local f = ({...})[1]\n"
			                   "local t = {}\n"
			                   "for s in coroutine.wrap(f) do\n"
			                   "    io.stderr:write('> ', s, '\\n')\n"
			                   "    t[#t+1] = s\n"
			                   "    t[#t+1] = '\\n'\n"
			                   "end\n"
			                   "return table.concat(t, '')");
			lua_insert(L, -2);
			lua_call(L, 1, 1);
			break;
		case LUA_TSTRING:
			break;
		default:
			fprintf(stderr, "ct_file_get_contents: unknown type\n");
			lua_pop(L, 2);
			rv = -EIO;
			goto out;
		}
		STACK_CHECK(L, 2);		/* ct fc */

		/* store it */
		lua_pushlightuserdata(L, fh);	/* ct fc fck */
		lua_insert(L, -2);		/* ct fck fc */
		lua_rawset(L, -3);		/* ct */
		/* get it */
		lua_pushlightuserdata(L, fh);	/* ct fck */
		lua_rawget(L, -2);		/* ct fc */
	}
	lua_remove(L, -2);			/* fc */
out:
	STACK_CHECK(L, (rv >= 0) ? 1 : 0);
	if (rv >= 0)
		assert(lua_type(L, -1) == LUA_TSTRING);
	return rv;
}

static ssize_t nodiscard
ct_file_get_length_cached(lua_State *L, void *fh)
{
	ssize_t rv = -ENOENT;
	STACK_CHECK_INIT(L);

	lua_pushlightuserdata(L, &k_fh_to_content);		/* ctk */
	lua_rawget(L, LUA_REGISTRYINDEX);			/* ct */

	lua_pushlightuserdata(L, fh);				/* ct fck */
	STACK_CHECK(L, 2);
	lua_rawget(L, -2);					/* ct fc */

	lua_remove(L, -2);					/* fc */

	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);					/* */

		lua_pushlightuserdata(L, &k_fh_to_content);	/* ftk */
		lua_rawget(L, LUA_REGISTRYINDEX);		/* ft */

		lua_pushlightuserdata(L, fh);			/* ft ffk */
		STACK_CHECK(L, 2);
		lua_rawget(L, -2);				/* ft ff */

		lua_remove(L, -2);				/* ff */
	}

	if (lua_type(L, -1) != LUA_TSTRING)
		rv = ENOENT;
	else
		rv = lua_objlen(L, -1);
	lua_pop(L, 1);

	STACK_CHECK(L, 0);
	return rv;
}

static void nonnull(1,2)
ct_file_release(lua_State *L, void *fh)
{
	STACK_CHECK_INIT(L);
	lua_pushlightuserdata(L, &k_fh_to_content);	/* fck */
	lua_rawget(L, LUA_REGISTRYINDEX);		/* FC */
	lua_pushlightuserdata(L, fh);			/* FC ptr */
	lua_pushnil(L);					/* FC ptr nil */
	lua_rawset(L, -3);				/* FC */
	lua_pop(L, 1);					/* */
	STACK_CHECK(L, 0);
}

static int
cfgfs_getattr(const char *path,
              struct stat *stbuf,
              struct fuse_file_info *fi)
{
	lua_State *L = fuse_get_context()->private_data;
	enum ct_item_type type;
	int rv;
	(void)fi;
	STACK_CHECK_INIT(L);

	fprintf(stderr, "getattr %s\n", path);

	if ((rv = ct_get_path(L, path)) < 0)
		goto out;

	type = ct_get_item_type(L);
	stbuf->st_nlink = 1;
	switch (type) {
	case CT_FILE:
		stbuf->st_mode = S_IFREG | 0644;
		if (lua_type(L, -1) == LUA_TSTRING)
			stbuf->st_size = lua_objlen(L, -1) + 1;
		else
			stbuf->st_size = 1024*1024;
		break;
	case CT_DIR:
		stbuf->st_mode = S_IFDIR | 0755;
		break;
	default:
		rv = -EIO;
		break;
	}
	lua_pop(L, 1);
out:
	STACK_CHECK(L, 0);
	return rv;
}

static int
cfgfs_open(const char *path,
           struct fuse_file_info *fi)
{
	lua_State *L = fuse_get_context()->private_data;
	int rv;
	STACK_CHECK_INIT(L);

	fprintf(stderr, "open    %s\n", path);

	fi->direct_io = 1;
	fi->fh = (uint64_t) malloc(1);
	if ((void *)fi->fh == NULL) {
		rv = -errno;
		goto out;
	}

	lua_pushlightuserdata(L, &k_fh_to_file);			/* ftk */
	lua_rawget(L, LUA_REGISTRYINDEX);				/* ft */

	lua_pushlightuserdata(L, (void *)fi->fh);			/* ft fh */

	if ((rv = ct_get_path_with_type(L, path, CT_FILE)) < 0) {	/* ft fh */
		lua_pop(L, 2);						/* */
		goto out;
	}								/* ft fh f */
	lua_rawset(L, -3);						/* ft */
	lua_pop(L, 1);							/* */
out:
	STACK_CHECK(L, 0);
	return rv;
}

static int
cfgfs_read(const char *path,
           char *buf,
           size_t size,
           off_t offset,
           struct fuse_file_info *fi)
{
	lua_State *L = fuse_get_context()->private_data;
	const char *cnt;
	size_t len;
	int rv;
	STACK_CHECK_INIT(L);
	assert(offset >= 0);

	fprintf(stderr, "read    %s\n", path);

	if ((rv = ct_file_get_contents(L, (void *)fi->fh)) < 0)
		goto out;

	cnt = lua_tostring(L, -1);
	len = lua_objlen(L, -1);
	len += 1; /* null byte */

	fprintf(stderr, "wants bytes %lu-%lu of %lu\n",
	    offset, offset + size, len);

	offset = MIN(offset, len);

	len -= offset;
	cnt += offset;

	len = MIN(len, size);

	memcpy(buf, cnt, len);
	rv = len;

	fprintf(stderr, "wrote bytes %lu-%lu (%lu)\n",
	    offset, offset + len, offset + len - offset);

	lua_pop(L, 1);
out:
	STACK_CHECK(L, 0);
	return rv;
}

static int
cfgfs_readdir(const char *path,
              void *buf,
              fuse_fill_dir_t filler,
              off_t offset,
              struct fuse_file_info *file,
              enum fuse_readdir_flags flags)
{
	lua_State *L = fuse_get_context()->private_data;
	const char *name;
	int rv;
	STACK_CHECK_INIT(L);
	(void)offset; /* TODO */
	(void)file;
	(void)flags;

	fprintf(stderr, "readdir %s\n", path);

	if ((rv = ct_get_path_with_type(L, path, CT_DIR)) < 0)
		goto out;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		lua_pop(L, 1);
		if (lua_type(L, -1) != LUA_TSTRING)
			continue;
		name = lua_tostring(L, -1);
		filler(buf, name, NULL, 0, 0);
	}
	lua_pop(L, 1);
out:
	STACK_CHECK(L, 0);
	return rv;
}

static int
cfgfs_release(const char *path, struct fuse_file_info *fi)
{
	lua_State *L = fuse_get_context()->private_data;

	fprintf(stderr, "release %s\n", path);
	ct_file_release(L, (void *)fi->fh);
	free((void *)fi->fh);
	return 0;
}

static struct fuse_operations fs_oper = {
	.init    = cfgfs_init,
	.destroy = cfgfs_destroy,
	.getattr = cfgfs_getattr,
	.readdir = cfgfs_readdir,
	.read    = cfgfs_read,
	.open    = cfgfs_open,
	.release = cfgfs_release,
};

int
main(int argc, char **argv)
{
	return fuse_main(argc, argv, &fs_oper, NULL);
}
