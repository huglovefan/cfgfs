#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------

struct buffer {
	size_t         size;
	bool           full;
	void          *data;
	struct buffer *next;
};

static inline size_t buffer_get_size(const struct buffer *self) {
	return self->size;
}

static inline void buffer_memcpy_to(const struct buffer *self,
                                    char *buf,
                                    size_t sz) {
	memcpy(buf, self->data, sz);
}

static inline void buffer_free(struct buffer *self) {
	free(self);
}

void buffer_make_full(struct buffer *self);

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

static inline bool buffer_list_is_empty(const struct buffer_list *self) {
	return (self->first == NULL || self->first->size == 0);
	// note: the only time we have a first one with size == 0 is when
	//  cfgfs_read() uses buffer_list_maybe_unshift_fake_buf()
}

// -----------------------------------------------------------------------------

void buffer_list_write_line(struct buffer_list *self, const char *s, size_t len);

char *buffer_list_get_write_buffer(struct buffer_list *self, size_t len);
void buffer_list_commit_write(struct buffer_list *self, size_t sz);
