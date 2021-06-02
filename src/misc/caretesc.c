#include "caretesc.h"

#include <stddef.h>

#define needsesc(c) ((unsigned char)(c) <= 31 || (unsigned char)(c) == 127)

static unsigned char getesc(unsigned char c) {
	if (c == 0) return '@';
	if (c <= 26) return c-1+'A';
	if (c <= 31) return (unsigned char)"[\\]^_"[c-27];
	if (c == 127) return '?';
	return c;
}

size_t caretesc(const char *restrict s, char *buf) {
	char *bufstart = buf;
	while (*s) {
		if (!needsesc(*s)) {
			*buf++ = *s++;
		} else {
			*buf++ = '^';
			*buf++ = (char)getesc((unsigned char)*s++);
		}
	}
	*buf = '\0';
	return (size_t)(buf-bufstart);
}

#include "../macros.h"

__attribute__((constructor))
static void _init_test_caretesc(void) {
	assert_compiler_knows(getesc('\0')   == '@');
	assert_compiler_knows(getesc('\x01') == 'A');
	assert_compiler_knows(getesc('\x1a') == 'Z');
#if !defined(SANITIZER)
	assert_compiler_knows(getesc('\x1b') == '[');
	assert_compiler_knows(getesc('\x1e') == '^');
	assert_compiler_knows(getesc('\x1f') == '_');
#else
	assert(getesc('\x1b') == '[');
	assert(getesc('\x1e') == '^');
	assert(getesc('\x1f') == '_');
#endif
	assert_compiler_knows(getesc('\x7f') == '?');
	assert_compiler_knows(!needsesc('\xe3') && getesc((unsigned char)'\xe3') == (unsigned char)'\xe3');
	assert_compiler_knows(!needsesc('\x81') && getesc((unsigned char)'\x81') == (unsigned char)'\x81');
	assert_compiler_knows(!needsesc('\x82') && getesc((unsigned char)'\x82') == (unsigned char)'\x82');
}
