#include "cfg.h"

#include <stdlib.h>
#include <string.h>

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lauxlib.h>
#endif

#include "buffers.h"
#include "cli_output.h"
#include "lua.h"
#include "macros.h"

// ~

enum wordflags {
	wf_none = 0,
	wf_needs_quotes = 1,
	// https://s2.desu-usergeneratedcontent.xyz/int/image/1461/31/1461314883561.jpg
	wf_contains_evil_char = 2,
};

static unsigned char charflags[256];

static enum wordflags check_word(const char *s, size_t len) {
	if (len > 0) {
		enum wordflags flags = wf_none;
		for (size_t i = 0; i < len; i++) {
			flags |= charflags[(unsigned char)s[i]];
		}
		return flags;
	} else {
		return wf_needs_quotes;
	}
}

#define set(c, flag) charflags[c] |= flag
#define set_range(f, t, flag) for (int c = f; c <= t; c++) set(c, flag)

__attribute__((constructor))
static void _init_badchars(void) {
	//set_range(0, 31, wf_needs_quotes); // replaced with ? which doesn't need quotes
	set(9, wf_needs_quotes);
	set(32, wf_needs_quotes);
	set_range(39, 41, wf_needs_quotes);
	set(47, wf_needs_quotes);
	set_range(58, 59, wf_needs_quotes);
	set(123, wf_needs_quotes);
	set_range(125, 255, wf_needs_quotes);

	// all characters below 32 (except for 9) work like a newline
	// 34 is double quote which can't be quoted itself (replaced with single quote)
	set_range(0, 8, wf_contains_evil_char);
	set_range(10, 31, wf_contains_evil_char);
	set(34, wf_contains_evil_char);
}

#undef set
#undef set_range

// -----------------------------------------------------------------------------

// it looks less bad if you reduce your tab width and font size

static int l_cmd(lua_State *L) {
	int top = lua_gettop(L);
	int cnt = top-1;
	struct {
		const char *s;
		size_t len;
		enum wordflags flags;
	} words[cnt];
	size_t total_len = 0;
	enum quoting_rules {
		qr_default = 0,
		// special quoting mode for the say command's manual command line parsing
		qr_say = 1,
	} rules = qr_default;
	if (unlikely(cnt == 0)) {
		return 0;
	}
	for (int i = 0; i < cnt; i++) {
		words[i].s = lua_tolstring(L, i+2, &words[i].len);
		if (unlikely(words[i].s == NULL)) {
			return luaL_error(L, "non-string argument passed to cmd");
		}
		if (i == 0 && cnt > 1) {
			// only use special rules if there's at least one argument
			if (words[0].len == 3 && strncmp(words[0].s, "say", 3) == 0) rules = qr_say;
			if (words[0].len == 8 && strncmp(words[0].s, "say_team", 8) == 0) rules = qr_say;
		}
		words[i].flags = check_word(words[i].s, words[i].len);
		total_len += words[i].len;
		switch (rules) {
		case qr_default:
			if (words[i].flags&wf_needs_quotes) {
				if (i == cnt-1) {
					// last one can skip the closing quote
					total_len += 1;
				} else {
					total_len += 2;
				}
			}
			if (i > 0 &&
				 !(words[i].flags&wf_needs_quotes) &&
				 !(words[i-1].flags&wf_needs_quotes)) {
				// spaces are only needed between two non-quoted words
				total_len += 1;
			}
			break;
		case qr_say:
			// [SAY"ARG1"] -> [SAY"][ARG1"]
			// [SAY"ARG1 ARG2"] -> [SAY"][ARG1 ][ARG2"]
			// what i mean to say it's always the word length + 1
			total_len += words[i].len+1;
			break;
		}
	}
	if (total_len > 0) {
		if (total_len <= max_line_length) {
			char *buf = buffer_list_get_write_buffer(&buffers, total_len);
			char *bufstart = buf;
			switch (rules) {
			case qr_default:
				for (int i = 0; i < cnt; i++) {
					if (i > 0 && !(*(buf-1) == '"' || words[i].flags&wf_needs_quotes)) {
						*buf++ = ' ';
					}
					if (!(words[i].flags&wf_needs_quotes)) {
						memcpy(buf, words[i].s, words[i].len);
						buf += words[i].len;
					} else {
						*buf++ = '"';
						memcpy(buf, words[i].s, words[i].len);
						buf += words[i].len;
						if (i != top-2) {
							*buf++ = '"';
						}
					}
					if (unlikely(words[i].flags&wf_contains_evil_char)) {
						char *buf_ = buf-(words[i].len+((i!=top-2&&words[i].flags&wf_needs_quotes)?1:0));
						for (size_t j = 0; j < words[i].len; j++) {
							if (buf_[j] < 32 && buf_[j] != 9) buf_[j] = '?';
							if (buf_[j] == '"') buf_[j] = '\'';
						}
					}
				}
				break;
			case qr_say:
				memcpy(buf, words[0].s, words[0].len);
				buf += words[0].len;
				*buf++ = '"';
				for (int i = 1; i < cnt; i++) {
					if (i >= 2) {
						*buf++ = ' ';
					}
					memcpy(buf, words[i].s, words[i].len);
					buf += words[i].len;
					if (unlikely(words[i].flags&wf_contains_evil_char)) {
						char *buf_ = buf-words[i].len;
						for (size_t j = 0; j < words[i].len; j++) {
							if (buf_[j] < 32 && buf_[j] != 9) buf_[j] = '?';
						}
					}
				}
				*buf++ = '"';
				break;
			}
			*buf++ = '\n';
			buffer_list_commit_write(&buffers, (size_t)(buf-bufstart));
		} else {
			return luaL_error(L, "command too long");
		}
	} else {
		return luaL_error(L, "empty command");
	}
	return 0;
}

// -----------------------------------------------------------------------------

static int l_cfg(lua_State *L) {
	size_t sz;
	const char *s = lua_tolstring(L, 1, &sz);
	if (unlikely(s == NULL)) goto typeerr;
	if (unlikely(sz == 0)) return 0;
	if (unlikely(sz > max_line_length)) goto toolong; // assume it's not multiple lines
	buffer_list_write_line(&buffers, s, sz);
	return 0;
typeerr:
	return luaL_error(L, "non-string argument passed to cfg()");
toolong:
	;
	_Static_assert(max_line_length >= 40);
	eprintln("warning: discarding too-long line: %.40s ...", s);
	return lua_print_backtrace(L);
}

// -----------------------------------------------------------------------------

void cfg_init_lua(void *L_) {
	lua_State *L = (lua_State *)L_;
	 lua_pushcfunction(L, l_cfg);
	lua_setglobal(L, "_cfg");
	 lua_pushcfunction(L, l_cmd);
	lua_setglobal(L, "_cmd");
}
