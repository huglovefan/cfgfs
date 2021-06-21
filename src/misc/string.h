#pragma once

#include <stddef.h>

struct string {
	char *data;
	size_t capacity;
	size_t length;
	unsigned int resizable : 1;
	unsigned int overflow : 1;
	unsigned int autogrow : 1;
};

struct string string_new(size_t sz);
struct string string_new_empty_from_stkbuf(char *buf, size_t sz);

void string_clear(struct string *self);
void string_free(struct string *self);

void string_append_from_buf(struct string *self, const char *buf, size_t sz);

__attribute__((format(printf, 2, 3)))
void string_append_from_fmt(struct string *self, const char *fmt, ...);

void string_remove_range(struct string *self, size_t start, size_t len);

#define string_set_contents_from_buf(s, buf, sz) \
	({ \
		string_clear(s); \
		string_append_from_buf(s, buf, sz); \
	})

#define string_set_contents_from_fmt(s, fmt, ...) \
	({ \
		string_clear(s); \
		string_append_from_fmt(s, fmt, ##__VA_ARGS__); \
	})

_Bool string_equals_cstring(struct string *self, const char *s);
_Bool string_equals_buf(struct string *self, const char *s, size_t sz);
