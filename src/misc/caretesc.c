#include "caretesc.h"

#include <stddef.h>

#define needsesc(c) ((c) <= 31 || (c) == 127)

static char getesc(char c) {
	if (c == 0) return '@';
	if (c <= 26) return c-1+'A';
	if (c <= 31) return "[\\]^_"[c-27];
	if (c == 127) return '?';
	return -1;
}

size_t caretesc(const char *restrict s, char *buf) {
	char *bufstart = buf;
	while (*s) {
		if (!needsesc(*s)) {
			*buf++ = *s++;
		} else {
			*buf++ = '^';
			*buf++ = getesc(*s++);
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
	assert_compiler_knows(getesc('\x1b') == '[');
	assert_compiler_knows(getesc('\x1e') == '^');
	assert_compiler_knows(getesc('\x1f') == '_');
	assert_compiler_knows(getesc('\x7f') == '?');
}
