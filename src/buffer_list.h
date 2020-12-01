#pragma once

#include <stdbool.h>
#include <stddef.h>

// -----------------------------------------------------------------------------

struct buffer {
	size_t        size;
	bool          full;
	void          *data;
	struct buffer *next;
};

size_t buffer_get_size(const struct buffer *self);
void buffer_memcpy_to(const struct buffer *self, char *buf, size_t sz);
void buffer_free(struct buffer *self);

// -----------------------------------------------------------------------------

struct buffer_list {
	struct buffer *nonfull;
	struct buffer *first;
	struct buffer *last;
};

bool buffer_list_maybe_unshift_fake_buf(struct buffer_list *self, struct buffer *buf, char *data);
void buffer_list_remove_fake_buf(struct buffer_list *self, struct buffer *buf);
struct buffer *buffer_list_grab_first(struct buffer_list *self);

void buffer_list_append_from_that_to_this(struct buffer_list *self, const struct buffer_list *that);

void buffer_list_reset(struct buffer_list *self);
void buffer_list_swap(struct buffer_list *self, struct buffer_list *bl);

bool buffer_list_is_empty(const struct buffer_list *self);

// -----------------------------------------------------------------------------

void buffer_list_write_line(struct buffer_list *self, const char *s, size_t sz);
