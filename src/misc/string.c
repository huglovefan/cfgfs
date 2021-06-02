#include "string.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define L(x) (likely(x))
#define U(x) (unlikely(x))

static bool string_try_resize(struct string *self, size_t sz);

struct string string_new(size_t sz) {
	struct string self;
	char *p = malloc(sz);
	if U (!p) sz = 0;
	self.data = p;
	self.length = 0;
	self.capacity = sz;
	self.autogrow = 0;
	self.overflow = 0;
	self.resizable = 1;
	if L (sz != 0) p[0] = '\0';
	return self;
}

struct string string_new_empty_from_stkbuf(char *buf, size_t sz) {
	struct string self;
	self.data = buf;
	self.length = 0;
	self.capacity = sz;
	self.autogrow = 0;
	self.overflow = 0;
	self.resizable = 0;
	if L (sz != 0) buf[0] = '\0';
	return self;
}

void string_free(struct string *self) {

	self->capacity = 0;
	self->length = 0;

	if (self->resizable) {
		free(self->data);
		self->data = NULL;
	}

}

static bool string_try_resize(struct string *self, size_t sz) {

	if (sz == self->capacity) return true;

	char *p;
	if L (self->resizable) {
		p = realloc(self->data, sz);
	} else {
		if L (!self->autogrow) return false;
		p = malloc(sz);
		if L (p || sz == 0) self->resizable = 1;
		if L (p && sz != 0) {
			if L (self->length != 0) {
				size_t copylen = self->length;
				if U (copylen > sz-1) {
					self->overflow = 1;
					copylen = sz-1;
				}
				memcpy(p, self->data, copylen);
				p[copylen] = '\0';
			} else {
				p[0] = '\0';
			}
		}
	}
	if U (!p && sz != 0) return false;
	self->data = p;
	self->capacity = sz;

	// did the shrinking affect the contents?
	if U (sz <= self->length) {
		if L (self->length != 0) {
			self->overflow = 1;
		}
		if L (sz != 0) {
			self->data[sz-1] = '\0';
			self->length = sz-1;
		} else {
			if L (self->data != NULL) self->data[0] = '\0';
			self->length = 0;
		}
	}

	return true;

}

void string_clear(struct string *self) {

	self->length = 0;
	self->overflow = 0;

	if (self->data != NULL) {
		self->data[0] = '\0';
	}

}

void string_append_from_buf(struct string *self, const char *buf, size_t sz) {

	if U (sz == 0) return;

	if U (self->capacity-self->length <= sz) {
		if U (!string_try_resize(self, self->length+sz+1)) {
			self->overflow = 1;
			sz = self->capacity-self->length;
			if L (sz != 0) sz--;
			else return;
		}
	}

	char *p = self->data+self->length;
	memcpy(p, buf, sz);
	p[sz] = '\0';
	self->length += sz;

}

__attribute__((format(printf, 2, 3)))
void string_append_from_fmt(struct string *self, const char *fmt, ...) {
	bool resized = false;
again:;
	va_list args;
	va_start(args, fmt);
	 int rv = vsnprintf(self->data+self->length, self->capacity-self->length, fmt, args);
	va_end(args);

	if L (rv >= 0 && (size_t)rv < self->capacity-self->length) {
		self->length += (size_t)rv;
		return;
	}
	if U (rv == -1 || rv == 0) {
		return;
	}

	if (!resized && string_try_resize(self, self->length+(size_t)rv+1)) {
		resized = true;
		goto again;
	} else {
		self->length = (self->capacity) ? self->capacity-1 : 0;
		self->overflow = 1;
	}
}

void string_remove_range(struct string *self, size_t start, size_t len) {
	if U (start >= self->length) return;
	if U (len > self->length-start) len = self->length-start;
	size_t end = start+len;
	memmove(self->data+start, self->data+end, self->length-end);
	self->length -= len;
	self->data[self->length] = '\0';
}

// -----------------------------------------------------------------------------

#if defined(WITH_D) && (defined(__clang__) || defined(__GNUC__))
 #define TEST(x) __attribute__((constructor)) static void test_##x(void)
#else
 #define TEST(x) __attribute__((unused)) static void test_##x(void)
#endif

TEST (zero_init_stk) {
	char tmp1[2], tmp2[2];
	struct string s1, s2;
	memset(&s1, 0b10101010, sizeof(s1));
	memset(&s2, 0b01010101, sizeof(s2));
	s1 = string_new_empty_from_stkbuf(tmp1, sizeof(tmp1));
	s2 = string_new_empty_from_stkbuf(tmp2, sizeof(tmp2));
	s1.data = NULL; s1.capacity = 0;
	s2.data = NULL; s2.capacity = 0;

	assert(s1.length == s2.length);
	assert(s1.resizable == s2.resizable);
	assert(s1.overflow == s2.overflow);
	assert(s1.autogrow == s2.autogrow);
}

TEST (append_from_buf_stk) {
	char c[4];
	struct string s = string_new_empty_from_stkbuf(c, sizeof(c));
	assert(s.data == c && s.capacity == sizeof(c));

	string_append_from_buf(&s, "x", 1); string_clear(&s); assert(s.length == 0 && !s.overflow);
	string_append_from_buf(&s, "", 0); assert(s.length == 0 && !s.overflow);

	string_clear(&s); string_append_from_buf(&s, "a", 1); assert(s.length == 1 && !s.overflow && 0 == strcmp(s.data, "a"));
	string_clear(&s); string_append_from_buf(&s, "ab", 2); assert(s.length == 2 && !s.overflow && 0 == strcmp(s.data, "ab"));
	string_clear(&s); string_append_from_buf(&s, "abc", 3); assert(s.length == 3 && !s.overflow && 0 == strcmp(s.data, "abc"));
	string_clear(&s); string_append_from_buf(&s, "abcd", 4); assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "abc"));
	assert(s.data == c && s.capacity == sizeof(c));

	// overflow with existing data
	string_clear(&s); string_append_from_buf(&s, "a", 1); string_append_from_buf(&s, "1", 1);
	assert(s.length == 2 && !s.overflow && 0 == strcmp(s.data, "a1"));
	string_clear(&s); string_append_from_buf(&s, "a", 1); string_append_from_buf(&s, "12345", 5);
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "a12"));
	string_clear(&s); string_append_from_buf(&s, "ab", 2); string_append_from_buf(&s, "12345", 5);
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "ab1"));
	string_clear(&s); string_append_from_buf(&s, "abc", 3); string_append_from_buf(&s, "12345", 5);
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "abc"));
}

TEST (append_from_fmt_stk) {
	char c[4];
	struct string s = string_new_empty_from_stkbuf(c, sizeof(c));
	assert(s.data == c && s.capacity == sizeof(c));

	string_append_from_fmt(&s, "x"); string_clear(&s); assert(s.length == 0 && !s.overflow);
	string_append_from_fmt(&s, ""); assert(s.length == 0 && !s.overflow);

	string_clear(&s); string_append_from_fmt(&s, "a"); assert(s.length == 1 && !s.overflow && 0 == strcmp(s.data, "a"));
	string_clear(&s); string_append_from_fmt(&s, "ab"); assert(s.length == 2 && !s.overflow && 0 == strcmp(s.data, "ab"));
	string_clear(&s); string_append_from_fmt(&s, "abc"); assert(s.length == 3 && !s.overflow && 0 == strcmp(s.data, "abc"));
	string_clear(&s); string_append_from_fmt(&s, "abcd"); assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "abc"));
	assert(s.data == c && s.capacity == sizeof(c));

	// overflow with existing data
	string_clear(&s); string_append_from_fmt(&s, "a"); string_append_from_fmt(&s, "1");
	assert(s.length == 2 && !s.overflow && 0 == strcmp(s.data, "a1"));
	string_clear(&s); string_append_from_fmt(&s, "a"); string_append_from_fmt(&s, "12345");
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "a12"));
	string_clear(&s); string_append_from_fmt(&s, "ab"); string_append_from_fmt(&s, "12345");
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "ab1"));
	string_clear(&s); string_append_from_fmt(&s, "abc"); string_append_from_fmt(&s, "12345");
	assert(s.length == 3 && s.overflow && 0 == strcmp(s.data, "abc"));
}

TEST (append_from_buf_resizable) {
	struct string s = string_new(4);

	string_append_from_buf(&s, "x", 1); assert(s.length == 1 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "x"));
	string_clear(&s); assert(s.length == 0 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, ""));
	string_clear(&s); string_append_from_buf(&s, "a", 1); assert(s.length == 1 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "a"));
	string_clear(&s); string_append_from_buf(&s, "ab", 2); assert(s.length == 2 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "ab"));
	string_clear(&s); string_append_from_buf(&s, "abc", 3); assert(s.length == 3 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "abc"));
	string_clear(&s); string_append_from_buf(&s, "abcd", 4); assert(s.length == 4 && s.capacity == 5 && !s.overflow && 0 == strcmp(s.data, "abcd"));
	string_free(&s);
}

TEST (append_from_fmt_resizable) {
	struct string s = string_new(4);

	string_append_from_fmt(&s, "x"); assert(s.length == 1 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "x"));
	string_clear(&s); assert(s.length == 0 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, ""));
	string_clear(&s); string_append_from_fmt(&s, "a"); assert(s.length == 1 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "a"));
	string_clear(&s); string_append_from_fmt(&s, "ab"); assert(s.length == 2 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "ab"));
	string_clear(&s); string_append_from_fmt(&s, "abc"); assert(s.length == 3 && s.capacity == 4 && !s.overflow && 0 == strcmp(s.data, "abc"));
	string_clear(&s); string_append_from_fmt(&s, "abcd"); assert(s.length == 4 && s.capacity == 5 && !s.overflow && 0 == strcmp(s.data, "abcd"));
	string_free(&s);
}

TEST(autogrow_append) {
	// autogrow: appending works
	char c[3];
	struct string s = string_new_empty_from_stkbuf(c, sizeof(c));
	s.autogrow = 1;

	assert(s.capacity == 3 && s.length == 0 && !s.overflow && !s.resizable && 0 == strcmp(s.data, ""));
	assert(s.data == c);

	string_append_from_buf(&s, "a", 1); assert(s.capacity == 3 && s.length == 1 && !s.overflow && !s.resizable && 0 == strcmp(s.data, "a"));
	assert(s.data == c);
	string_append_from_buf(&s, "b", 1); assert(s.capacity == 3 && s.length == 2 && !s.overflow && !s.resizable && 0 == strcmp(s.data, "ab"));
	assert(s.data == c);
	string_append_from_buf(&s, "c", 1); assert(s.capacity == 4 && s.length == 3 && !s.overflow && s.resizable && 0 == strcmp(s.data, "abc"));
	assert(s.data != c);
	string_append_from_buf(&s, "d", 1); assert(s.capacity == 5 && s.length == 4 && !s.overflow && s.resizable && 0 == strcmp(s.data, "abcd"));
	assert(s.data != c);

	string_free(&s);
}

TEST(autogrow_shrink) {
	// autogrow: shrinking reallocates and truncates
	char b[3];
	struct string s = string_new_empty_from_stkbuf(b, sizeof(b));
	string_set_contents_from_fmt(&s, "hi");
	s.autogrow = 1;

	assert(s.data == b);
	assert(string_try_resize(&s, 2));
	assert(s.data != b);

	assert(s.overflow && s.length == 1 && 0 == strcmp(s.data, "h"));

	assert(string_try_resize(&s, 1));
	assert(s.overflow && s.length == 0 && 0 == strcmp(s.data, ""));

	string_free(&s);
}

TEST (remove_range) {
	char b[16];
	struct string s = string_new_empty_from_stkbuf(b, sizeof(b));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 0, 1);
	assert(s.length == 2 && 0 == strcmp(s.data, "bc"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 1, 1);
	assert(s.length == 2 && 0 == strcmp(s.data, "ac"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 2, 1);
	assert(s.length == 2 && 0 == strcmp(s.data, "ab"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 3, 1);
	assert(s.length == 3 && 0 == strcmp(s.data, "abc"));

	// ~

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 0, 2);
	assert(s.length == 1 && 0 == strcmp(s.data, "c"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 1, 2);
	assert(s.length == 1 && 0 == strcmp(s.data, "a"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 2, 2);
	assert(s.length == 2 && 0 == strcmp(s.data, "ab"));

	// ~

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 2, 100);
	assert(s.length == 2 && 0 == strcmp(s.data, "ab"));

	string_set_contents_from_fmt(&s, "abc");
	string_remove_range(&s, 999, 1);
	assert(s.length == 3 && 0 == strcmp(s.data, "abc"));
}
