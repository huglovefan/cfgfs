#define _GNU_SOURCE // strchrnul()
#include "cfg.h"

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "macros.h"

// -----------------------------------------------------------------------------

enum wordflags {
	wf_none = 0,
	wf_needs_quotes = 1,
	// https://s2.desu-usergeneratedcontent.xyz/int/image/1461/31/1461314883561.jpg
	wf_contains_evil_char = 2,
};

static unsigned char charflags[256];

#define set(c, flag) charflags[c] |= flag
#define set_range(f, t, flag) for (int c = f; c <= t; c++) set(c, flag)

__attribute__((constructor))
static void _init_badchars(void) {
	set_range(0, 31, wf_needs_quotes);
	set(9, wf_needs_quotes);
	set(32, wf_needs_quotes);
	set_range(39, 41, wf_needs_quotes);
	set(47, wf_needs_quotes);
	set_range(58, 59, wf_needs_quotes);
	set(123, wf_needs_quotes);
	set_range(125, 255, wf_needs_quotes);

	// 0: null byte, game can't read this -> replaced with 0x7F (DEL)
	// 1-31 excluding 9: game treats these as newlines -> replaced with 0x7F (DEL)
	// 34: double quote, can't be quoted itself -> replaced with a single quote
	set_range(0, 8, wf_contains_evil_char);
	set_range(10, 31, wf_contains_evil_char);
	set(34, wf_contains_evil_char);
}

#undef set
#undef set_range

// ~

struct worddata {
	const char *s;
	size_t len;
	enum wordflags flags;
};

enum quoting_mode {
	// ordinary command
	// quotes just what's required
	//   cmd.alias('test', 'cmd1; cmd2') -> [[alias test"cmd1; cmd2]]
	qm_default = 0,
	// say command
	// it does its own command line parsing which works best when there's one argument in quotes
	//   cmd.say('hello', 'world') -> [[say"hello world"]]
	//   cmd.say('this can contain "quotes"!') -> [[say"this can contain "quotes"!"]]
	qm_say = 1,
	// echo
	// arguments are pasted into one to reduce jumbling
	//   cmd.echo('hello', 'world') -> [[echo"hello world]]
	qm_echo = 2,
};

static size_t cmd_preparse(lua_State *L, int argc, struct worddata *words);
static enum quoting_mode cmd_get_quoting_mode(int argc, const struct worddata *words);
static size_t cmd_get_outsize(int argc, const struct worddata *words, size_t total_len, enum quoting_mode mode);
static size_t cmd_stringify(char *buf, int argc, const struct worddata *words, enum quoting_mode mode);

static int l_cmd(lua_State *L) {
	int argc = lua_gettop(L)-1; // first arg is the cmd table
	if (unlikely(!(argc >= 1))) return 0;
	struct worddata words[argc];
	size_t total_len = cmd_preparse(L, argc, words);
	if (unlikely(total_len == (size_t)-1)) goto err_invalid;
	enum quoting_mode mode = cmd_get_quoting_mode(argc, words);
	size_t outsize = cmd_get_outsize(argc, words, total_len, mode);
	if (unlikely(outsize > max_line_length)) goto err_toolong;
	char *buf = buffer_list_get_write_buffer(&buffers, outsize);
	size_t wrote = cmd_stringify(buf, argc, words, mode);
	assert(wrote == outsize);
	buf[wrote++] = '\n';
	buffer_list_commit_write(&buffers, wrote);
	return 0;
err_invalid:
	return luaL_error(L, "argument is not a string");
err_toolong:
	return luaL_error(L, "command too long");
}

__attribute__((minsize))
static int l_cmd_stringify(lua_State *L) {
	const char *errmsg;
	int argc = lua_gettop(L);
	if (unlikely(argc == 0)) goto err_empty;
	if (unlikely(argc > max_argc)) goto err_toomany;
	struct worddata *words = alloca(sizeof(struct worddata)*(size_t)argc);
	size_t total_len = cmd_preparse(L, argc, words);
	if (unlikely(total_len == (size_t)-1)) goto err_invalid;
	enum quoting_mode mode = cmd_get_quoting_mode(argc, words);
	size_t outsize = cmd_get_outsize(argc, words, total_len, mode);
	if (unlikely(outsize > max_line_length)) goto err_toolong;
	char *buf = alloca(outsize);
	size_t wrote = cmd_stringify(buf, argc, words, mode);
	assert(wrote == outsize);
	lua_pushlstring(L, buf, wrote);
	return 1;
err_empty:
	errmsg = "empty command";
	goto error;
err_toomany:
	errmsg = "too many arguments";
	goto error;
err_invalid:
	errmsg = "argument is not a string";
	goto error;
err_toolong:
	errmsg = "command too long";
	goto error;
error:
	lua_pushnil(L);
	lua_pushstring(L, errmsg);
	return 2;
}

/*
 * gets the words from lua, finds out flags for each one and sums up their total lengths
 */
static size_t cmd_preparse(lua_State *restrict L,
                           int argc,
                           struct worddata *restrict words) {
	size_t total_len = 0;
	for (int i = 0; i < argc; i++) {
		size_t len;
		const char *s = lua_tolstring(L, i-argc, &len);
		if (unlikely(!s)) goto err;
		enum wordflags flags;
		if (likely(len > 0)) {
			flags = 0;
			for (const char *p = s; p != s+len; p++) {
				flags |= charflags[(unsigned char)*p];
			}
		} else {
			flags = wf_needs_quotes;
		}
		words->s = s;
		words->len = len;
		words->flags = flags;
		words++;
		total_len += len;
	}
	return total_len;
err:
	return (size_t)-1;
}

/*
 * gets the quoting mode for the words
 */
static enum quoting_mode cmd_get_quoting_mode(int argc,
                                              const struct worddata *restrict words) {
	enum quoting_mode mode = qm_default;
	if (argc >= 2) {
		if (words[0].len == 3 && memcmp(words[0].s, "say", 3) == 0) mode = qm_say;
		if (words[0].len == 4 && memcmp(words[0].s, "echo", 4) == 0) mode = qm_echo;
		if (words[0].len == 8 && memcmp(words[0].s, "say_team", 8) == 0) mode = qm_say;
	}
	return mode;
}

/*
 * gets the required buffer size for converting the words to a string with the specified quoting mode
 */
static size_t cmd_get_outsize(int argc,
                              const struct worddata *restrict words,
                              size_t total_len,
                              enum quoting_mode mode) {
	switch (mode) {
	case qm_default:
		for (int i = 0; i < argc; i++) {
			if (words[i].flags&wf_needs_quotes) {
				// word needs quotes around it.
				// if it's the last one, the closing one can be skipped
				total_len += 2 - (i == argc-1);
			} else if (i > 0 && !(words[i-1].flags&wf_needs_quotes)) {
				// this word isn't quoted and neither is the previous one.
				// a space is needed only in this case
				total_len += 1;
			}
		}
		break;
	case qm_say:
		// word1Qword2Sword3Q
		//      ^     ^     ^
		// (there's one added separator for each word)
		total_len += (size_t)argc;
		break;
	case qm_echo:
		// same as qm_say but without the last quote
D		assert(argc >= 1);
		total_len += (size_t)(argc-1);
		break;
	}
	return total_len;
}

__attribute__((cold))
__attribute__((noinline))
static void cmd_replace_evil_chars(char *restrict buf,
                                   size_t sz,
                                   enum quoting_mode mode) {
	switch (mode) {
	case qm_default:
	case qm_echo:
		for (size_t i = 0; i < sz; i++) {
			if (buf[i] < 32 && buf[i] != 9) buf[i] = /*DEL*/ 0x7f;
			if (buf[i] == '"') buf[i] = '\'';
		}
		break;
	case qm_say:
		for (size_t i = 0; i < sz; i++) {
			if (buf[i] < 32 && buf[i] != 9) buf[i] = /*DEL*/ 0x7f;
		}
		break;
	}
}

static size_t cmd_stringify(char *restrict buf,
                            int argc,
                            const struct worddata *restrict words,
                            enum quoting_mode mode) {
	assume(argc >= 1);
	char *bufstart = buf;
	switch (mode) {
	case qm_default:
		for (int i = 0; i < argc; i++) {
			if (!(words[i].flags&wf_needs_quotes)) {
				if (i > 0 && *(buf-1) != '"') {
					*buf++ = ' ';
				}
				memcpy(buf, words[i].s, words[i].len);
				buf += words[i].len;
			} else {
				*buf++ = '"';
				memcpy(buf, words[i].s, words[i].len);
				buf += words[i].len;
				if (i != argc-1) {
					*buf++ = '"';
				}
			}
			if (unlikely(words[i].flags&wf_contains_evil_char)) {
				char *buf_ = buf-words[i].len;
				if (i != argc-1 && words[i].flags&wf_needs_quotes) {
					buf_ -= 1; // closing quote
				}
				cmd_replace_evil_chars(buf_, words[i].len, mode);
			}
		}
		break;
	case qm_say:
	case qm_echo: {
		char sep = '"';
		for (int i = 0; i < argc; i++) {
			memcpy(buf, words[i].s, words[i].len);
			if (unlikely(words[i].flags&wf_contains_evil_char)) {
				cmd_replace_evil_chars(buf, words[i].len, mode);
			}
			buf += words[i].len;
			*buf++ = sep;
			sep = ' ';
		}
		buf -= 1; // last separator
		if (mode == qm_say) *buf++ = '"';
		break;
	}
	}
	return (size_t)(buf-bufstart);
}

// -----------------------------------------------------------------------------

static void cfg(lua_State *L, const char *s, size_t sz) {
	if (unlikely(sz == 0)) return;
	if (unlikely(sz > max_line_length)) goto toolong;
	buffer_list_write_line(&buffers, s, sz);
	return;
toolong:
	luaL_error(L, "line too long");
	__builtin_unreachable();
}

static int l_cfg(lua_State *L) {
	size_t sz;
	const char *s = lua_tolstring(L, 1, &sz);
	if (unlikely(s == NULL)) goto typeerr;
	if (likely(sz <= max_line_length)) {
		cfg(L, s, sz);
		return 0;
	} else {
		// doesn't fit, is it multiple lines?
		for (;;) {
			const char *nl = strchrnul(s, '\n');
			cfg(L, s, (size_t)(nl-s));
			if (!*nl) break;
			s = nl+1;
		}
		return 0;
	}
typeerr:
	return luaL_error(L, "argument is not a string");
}

// -----------------------------------------------------------------------------

void cfg_init_lua(void *L) {
	 lua_pushcfunction(L, l_cfg);
	lua_setglobal(L, "_cfg");
	 lua_pushcfunction(L, l_cmd);
	lua_setglobal(L, "_cmd");
	 lua_pushcfunction(L, l_cmd_stringify);
	lua_setglobal(L, "cmd_stringify");
}
